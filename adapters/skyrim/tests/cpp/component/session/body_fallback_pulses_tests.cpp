#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <algorithm>
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <input_next_effects.h>
#include <memory>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>
#include <vector>

namespace skyrim_adapter::tests::session {

TEST_CASE("skyrim.session.body_classic_voice_keeps_silent_lease", "[component][skyrim][session]") {
    const auto classic_transient = std::make_unique<Fixture>();
    auto& classic = *classic_transient;
    classic.settle(idle());
    const auto body_submissions_before_event = classic.output.body_submissions.size();
    classic.time += 10ms;
    Action voice{};
    voice.event = raw_event(EventKind::voice_fire, 0x14, classic.time);
    classic.observe(idle(), std::span{&voice, 1});
    classic.coordinator.tick(classic.time);
    const auto body_submissions_after_event = classic.output.body_submissions.size();
    INFO("classic-only transient caused more than one fallback or lease update");
    REQUIRE((body_submissions_after_event == body_submissions_before_event ||
             body_submissions_after_event == body_submissions_before_event + 1));
    for (std::size_t index = body_submissions_before_event; index < body_submissions_after_event;
        ++index) {
        const auto& body = classic.output.body_submissions[index];
        INFO("classic-only transient submitted PCM or lost finite silent ownership");
        REQUIRE(
            (body.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP && body.samples.empty() &&
                body.request.audio_instance_id == 0 && body.request.ttl_ms == hold_ttl_ms));
    }
    classic.advance(idle(), 100ms);
    INFO("classic-only transient changed or replayed PCM instead of refreshing the finite silent "
         "lease");
    REQUIRE((classic.output.body_submissions.size() > body_submissions_before_event &&
             classic.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             classic.output.body_submissions.back().samples.empty() &&
             classic.output.body_submissions.back().request.audio_instance_id == 0 &&
             classic.output.body_submissions.back().request.ttl_ms == hold_ttl_ms &&
             std::ranges::none_of(classic.output.body_submissions.begin() +
                                      static_cast<std::ptrdiff_t>(body_submissions_before_event),
                 classic.output.body_submissions.end(),
                 [](const BodySubmission& body) {
                     return body.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE;
                 })));
}

TEST_CASE("skyrim.session.paired_dual_fire_has_one_classic_onset_and_expiry",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC;
    fixture.settle(idle());
    fixture.time = MonoTime{} + 9010ms;
    Observation current = idle();
    current.dual_cast = true;
    current.hands[0] = {
        .equipped_id = 42,
        .caster_source_id = 42,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = CastMode::fire_and_forget,
        .cast_phase = CastPhase::charging,
        .two_handed = false,
    };
    current.hands[1] = {
        .equipped_id = 42,
        .caster_source_id = 42,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = CastMode::fire_and_forget,
        .cast_phase = CastPhase::casting,
        .two_handed = false,
    };
    Action left = spell_release(Hand::left, MonoTime{} + 9000ms, 900, SpellKind::fire);
    Action right = spell_release(Hand::right, MonoTime{} + 9010ms, 901, SpellKind::fire);
    left.event.source_id = right.event.source_id = 42;
    const std::array<Action, 2> events{left, right};
    fixture.observe(current, events);
    fixture.coordinator.tick(fixture.time);
    const auto& onset = fixture.output.body_submissions.back().request;
    INFO("classic dual-fire session emits one energized fallback onset under the bounded silent "
         "lease");
    REQUIRE((onset.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             fixture.output.body_submissions.back().samples.empty() &&
             onset.fallback_rumble.low_frequency > 0.0F &&
             onset.fallback_rumble.high_frequency > 0.0F && onset.ttl_ms <= 200));
    const auto energized_fallbacks = [&fixture] {
        return std::count_if(fixture.output.body_submissions.begin(),
            fixture.output.body_submissions.end(),
            [](const BodySubmission& submission) {
                return submission.request.fallback_rumble.low_frequency > 0.0F ||
                       submission.request.fallback_rumble.high_frequency > 0.0F;
            });
    };
    INFO("left/right dual fire creates one classic fallback onset");
    REQUIRE(energized_fallbacks() == 1);
    const auto submitted_channel = [&fixture](std::uint32_t channel) {
        return std::ranges::any_of(fixture.output.submissions,
            [channel](const auto& request) { return (request.channel_mask & channel) != 0; });
    };
    INFO("body deduplication preserves both independent per-hand fire triggers");
    REQUIRE((submitted_channel(INPUT_NEXT_EFFECTS_LEFT_TRIGGER) &&
             submitted_channel(INPUT_NEXT_EFFECTS_RIGHT_TRIGGER)));

    fixture.advance(current, 44ms);
    const auto& before_expiry = fixture.output.body_submissions.back().request;
    INFO("classic paired pulse remains energized immediately before 9055 expiry");
    REQUIRE((before_expiry.fallback_rumble.low_frequency > 0.0F ||
             before_expiry.fallback_rumble.high_frequency > 0.0F));
    fixture.advance(current, 1ms);
    const auto& at_expiry = fixture.output.body_submissions.back().request;
    INFO("classic paired fallback clears at the original 55ms onset boundary");
    REQUIRE((at_expiry.fallback_rumble.low_frequency == 0.0F &&
             at_expiry.fallback_rumble.high_frequency == 0.0F));
    const auto energized_at_expiry = energized_fallbacks();
    fixture.advance(current, 1ms);
    const auto& after_expiry = fixture.output.body_submissions.back().request;
    INFO("classic paired fallback remains silent at 9056 without replay");
    REQUIRE((after_expiry.fallback_rumble.low_frequency == 0.0F &&
             after_expiry.fallback_rumble.high_frequency == 0.0F &&
             energized_fallbacks() == energized_at_expiry));
}

TEST_CASE("skyrim.session.body_classic_unknown_hand_pulse_ends_at_55ms",
    "[component][skyrim][session]") {
    const auto classic_pulse = std::make_unique<Fixture>();
    auto& classic = *classic_pulse;
    classic.settle(idle());
    classic.time += 10ms;
    auto fire = spell_release(Hand::unknown, classic.time, 1, SpellKind::fire);
    classic.observe(idle(), std::span{&fire, 1});
    classic.coordinator.tick(classic.time);
    const auto& onset = classic.output.body_submissions.back().request;
    INFO("unknown-hand fire did not update its short classic fallback pulse without PCM "
         "replacement");
    REQUIRE((onset.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             onset.fallback_rumble.high_frequency > 0.0F));
    classic.advance(idle(), 20ms);
    INFO("sampled classic fallback pulse was not kept current before lease renewal");
    REQUIRE((classic.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             classic.output.body_submissions.back().request.fallback_rumble.high_frequency > 0.0F));
    classic.advance(idle(), 35ms);
    const auto& pulse_end = classic.output.body_submissions.back().request;
    INFO("55ms classic fallback pulse remained active past its sampled boundary");
    REQUIRE((pulse_end.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             pulse_end.fallback_rumble.low_frequency == 0.0F &&
             pulse_end.fallback_rumble.high_frequency == 0.0F));
}

TEST_CASE("skyrim.session.body_hd_unknown_hand_pulse_does_not_replace_pcm",
    "[component][skyrim][session]") {
    const auto hd_pulse_owner = std::make_unique<Fixture>();
    auto& hd_pulse = *hd_pulse_owner;
    hd_pulse.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    hd_pulse.output.capabilities.audio_instance_id = 125;
    hd_pulse.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    hd_pulse.settle(idle());
    hd_pulse.time += 10ms;
    auto hd_fire = spell_release(Hand::unknown, hd_pulse.time, 1, SpellKind::fire);
    hd_pulse.observe(idle(), std::span{&hd_fire, 1});
    hd_pulse.coordinator.tick(hd_pulse.time);
    hd_pulse.advance(idle(), 20ms);
    const auto& hd_refresh = hd_pulse.output.body_submissions.back().request;
    INFO("HD-selected fallback-only update replayed PCM or lost the exact ready route");
    REQUIRE((hd_refresh.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             hd_refresh.route_preference == INPUT_NEXT_EFFECTS_BODY_ROUTE_PREFER_HD &&
             hd_pulse.output.body_submissions.back().request.audio_instance_id == 125 &&
             hd_refresh.fallback_rumble.high_frequency > 0.0F));
}

} // namespace skyrim_adapter::tests::session
