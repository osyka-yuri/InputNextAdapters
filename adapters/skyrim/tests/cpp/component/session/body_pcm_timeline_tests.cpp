#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <algorithm>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <input_next_effects.h>
#include <memory>
#include <skyrim_adapter/body_waveform.hpp>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>
#include <vector>

namespace skyrim_adapter::tests::session {

TEST_CASE("skyrim.session.body_pcm_voice_held_renewal_preserves_offset_and_stops_once",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    fixture.output.capabilities.audio_instance_id = 55;
    fixture.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    fixture.settle(casting());

    const auto held_reference = std::make_unique<Fixture>();
    auto& reference = *held_reference;
    reference.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    reference.output.capabilities.audio_instance_id = 56;
    reference.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    reference.settle(casting());

    fixture.time += 10ms;
    Action voice{};
    voice.event = raw_event(EventKind::voice_fire, 0x14, fixture.time);
    fixture.observe(casting(), std::span{&voice, 1});
    fixture.coordinator.tick(fixture.time);
    INFO("body-wide voice did not replace held PCM with the complete active plan");
    REQUIRE(fixture.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
    const auto first_request = fixture.output.body_submissions.back().request;
    const auto first = fixture.output.body_submissions.back().samples;
    INFO("body-wide voice and held concentration were not rendered into stereo PCM");
    REQUIRE(
        (first.size() == 19'200 && channel_peak(first, 0) > 0.0F && channel_peak(first, 1) > 0.0F));

    fixture.advance(casting(), 100ms);
    const auto renewed = fixture.output.body_submissions.back();
    INFO("held concentration did not renew the active voice window at 100ms");
    REQUIRE(renewed.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
    INFO("voice renewal did not retain exact body lease sequencing");
    REQUIRE((renewed.request.sequence == first_request.sequence + 1 &&
             renewed.request.lease_id == first_request.lease_id &&
             renewed.samples.size() == first.size()));
    INFO("renewal restarted the body voice instead of sampling its original cue time");
    REQUIRE(renewed.samples[1] == first[(100 * 48 * 2) + 1]);
    INFO("renewed voice did not retain energy in its original 60ms tail");
    REQUIRE(channel_peak(std::vector<float>(renewed.samples.begin() + 1,
                             renewed.samples.begin() + std::size_t{60} * 48 * 2),
                1) > 0.0F);

    reference.advance(casting(), 110ms);
    const auto& held_only = reference.output.body_submissions.back().samples;
    bool left_contains_voice = false;
    bool left_matches_after_voice = true;
    bool right_silent_after_voice = true;
    for (std::size_t frame = 0; frame < body_pcm_max_frames; ++frame) {
        const auto left_sample = renewed.samples[frame * 2];
        const auto right_sample = renewed.samples[(frame * 2) + 1];
        if (frame < std::size_t{60} * 48U && left_sample != held_only[frame * 2]) {
            left_contains_voice = true;
        }
        if (frame >= std::size_t{60} * 48U && left_sample != held_only[frame * 2]) {
            left_matches_after_voice = false;
        }
        if (frame >= std::size_t{60} * 48U && right_sample != 0.0F) {
            right_silent_after_voice = false;
        }
    }
    INFO("renewed overlapping left channel did not include the body voice contribution");
    REQUIRE(left_contains_voice);
    INFO("body voice was replayed after its original 160ms end");
    REQUIRE((left_matches_after_voice && right_silent_after_voice));

    fixture.advance(idle(), 60ms);
    INFO("expired transient did not stop the body voice");
    REQUIRE(fixture.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_STOP);
    const auto stop_count = std::count_if(fixture.output.body_submissions.begin(),
        fixture.output.body_submissions.end(),
        [](const BodySubmission& item) {
            return item.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP;
        });
    fixture.advance(idle(), 100ms);
    INFO("neutral lease did not continue with KEEP after STOP");
    REQUIRE(fixture.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_KEEP);
    INFO("neutral silence issued STOP repeatedly");
    REQUIRE(std::count_if(fixture.output.body_submissions.begin(),
                fixture.output.body_submissions.end(),
                [](const BodySubmission& item) {
                    return item.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP;
                }) == stop_count);
}

TEST_CASE("skyrim.session.body_pcm_staggered_hand_release_preserves_stereo_channels",
    "[component][skyrim][session]") {
    const auto staggered_owner = std::make_unique<Fixture>();
    auto& staggered = *staggered_owner;
    advertise_hd(staggered.output, 57);
    staggered.settle(idle());
    staggered.time += 10ms;
    auto left = spell_release(Hand::left, staggered.time, 1, SpellKind::fire);
    staggered.observe(idle(), std::span{&left, 1});
    staggered.coordinator.tick(staggered.time);
    const auto left_only = staggered.output.body_submissions.back().samples;
    INFO("left release escaped its stereo position");
    REQUIRE((channel_peak(left_only, 0) > 0.0F && channel_peak(left_only, 1) == 0.0F));
    staggered.time += 30ms;
    auto right = spell_release(Hand::right, staggered.time, 2, SpellKind::shock);
    staggered.observe(idle(), std::span{&right, 1});
    staggered.coordinator.tick(staggered.time);
    const auto both = staggered.output.body_submissions.back().samples;
    INFO("staggered hand releases were not combined into a complete plan");
    REQUIRE((staggered.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             channel_peak(both, 0) > 0.0F && channel_peak(both, 1) > 0.0F));
    staggered.advance(idle(), 30ms);
    const auto surviving = staggered.output.body_submissions.back().samples;
    INFO("left release removal erased or replayed the active right release");
    REQUIRE((staggered.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             channel_peak(surviving, 0) == 0.0F && channel_peak(surviving, 1) > 0.0F));
}

} // namespace skyrim_adapter::tests::session
