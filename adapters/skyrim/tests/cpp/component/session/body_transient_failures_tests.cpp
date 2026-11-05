#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <algorithm>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <input_next_core.h>
#include <input_next_effects.h>
#include <memory>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>
#include <vector>

namespace skyrim_adapter::tests::session {

TEST_CASE("skyrim.session.body_rejected_transient_is_not_replayed",
    "[component][skyrim][session]") {
    const auto failed_owner = std::make_unique<Fixture>();
    auto& failed = *failed_owner;
    failed.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    failed.output.capabilities.audio_instance_id = 123;
    failed.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    failed.settle(idle());
    const auto accepted_lease = failed.output.active_body_lease;

    failed.time += 10ms;
    Action voice{};
    voice.event = raw_event(EventKind::voice_fire, 0x14, failed.time);
    failed.observe(idle(), std::span{&voice, 1});
    failed.output.body_submit_status = INPUT_NEXT_STATUS_UNAVAILABLE;
    failed.coordinator.tick(failed.time);
    const auto failed_request = failed.output.body_submissions.back().request;
    INFO("transient failure fixture did not fail an exact PCM replacement");
    REQUIRE((failed_request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             failed_request.sequence == 2 && failed_request.lease_id == accepted_lease));
    failed.output.body_submit_status = INPUT_NEXT_STATUS_OK;

    failed.advance(idle(), 100ms);
    const auto& fallback_only = failed.output.body_submissions.back().request;
    INFO("non-OK REPLACE mutated accepted owner state or replayed the unchanged transient");
    REQUIRE((fallback_only.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             fallback_only.lease_id == accepted_lease && fallback_only.sequence == 2));
}

TEST_CASE("skyrim.session.body_rejected_second_transient_does_not_replay_first",
    "[component][skyrim][session]") {
    const auto accepted_window = std::make_unique<Fixture>();
    auto& window = *accepted_window;
    window.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    window.output.capabilities.audio_instance_id = 126;
    window.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    window.settle(idle());
    const auto original_lease = window.output.active_body_lease;
    window.time += 10ms;
    Action first_voice{};
    first_voice.event = raw_event(EventKind::voice_fire, 0x14, window.time);
    first_voice.event.sequence = 1;
    window.observe(idle(), std::span{&first_voice, 1});
    window.coordinator.tick(window.time);
    INFO("initial transient A did not install the accepted HD PCM window");
    REQUIRE((window.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             window.output.body_submissions.back().request.lease_id == original_lease));

    window.time += 10ms;
    Action second_voice{};
    second_voice.event = raw_event(EventKind::voice_fire, 0x14, window.time);
    second_voice.event.sequence = 2;
    window.observe(idle(), std::span{&second_voice, 1});
    window.output.body_submit_status = INPUT_NEXT_STATUS_UNAVAILABLE;
    window.coordinator.tick(window.time);
    const auto rejected_plan = window.output.body_submissions.back().request;
    INFO("transient B did not fail against the still-accepted lease after A");
    REQUIRE((rejected_plan.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             rejected_plan.lease_id == original_lease && rejected_plan.sequence == 3));
    window.output.body_submit_status = INPUT_NEXT_STATUS_OK;

    window.advance(idle(), 100ms);
    const auto& keep_after_failure = window.output.body_submissions.back();
    INFO("successful KEEP after failed B changed accepted lease/sequence or replayed PCM");
    REQUIRE((keep_after_failure.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             keep_after_failure.request.lease_id == original_lease &&
             keep_after_failure.request.sequence == rejected_plan.sequence &&
             keep_after_failure.samples.empty()));
    const auto submissions_after_keep = window.output.body_submissions.size();
    window.advance(idle(), 10ms);
    INFO("successful KEEP associated A's old window with unchanged transient B at 110ms");
    REQUIRE(std::none_of(window.output.body_submissions.begin() +
                             static_cast<std::ptrdiff_t>(submissions_after_keep),
        window.output.body_submissions.end(),
        [](const BodySubmission& submission) {
            return submission.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE;
        }));
}

TEST_CASE("skyrim.session.body_new_action_after_rejected_transient_is_delivered",
    "[component][skyrim][session]") {
    const auto semantic_change = std::make_unique<Fixture>();
    auto& fresh = *semantic_change;
    fresh.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    fresh.output.capabilities.audio_instance_id = 124;
    fresh.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    fresh.settle(idle());
    fresh.time += 10ms;
    Action voice{};
    voice.event = raw_event(EventKind::voice_fire, 0x14, fresh.time);
    fresh.observe(idle(), std::span{&voice, 1});
    fresh.output.body_submit_status = INPUT_NEXT_STATUS_UNAVAILABLE;
    fresh.coordinator.tick(fresh.time);
    fresh.output.body_submit_status = INPUT_NEXT_STATUS_OK;
    fresh.time += 20ms;
    auto fire = spell_release(Hand::right, fresh.time, 2, SpellKind::fire);
    fresh.observe(idle(), std::span{&fire, 1});
    fresh.coordinator.tick(fresh.time);
    INFO("a fresh semantic body plan was blocked by the prior failed transient");
    REQUIRE(fresh.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
}

TEST_CASE("skyrim.session.body_expired_lease_after_rejected_transient_acquires_silent",
    "[component][skyrim][session]") {
    const auto expired = std::make_unique<Fixture>();
    auto& reacquire = *expired;
    reacquire.settle(idle());
    const auto old_owner_deadline = reacquire.time + 200ms;
    reacquire.time += 150ms;
    Action live_voice{};
    live_voice.event = raw_event(EventKind::voice_fire, 0x14, reacquire.time);
    live_voice.event.sequence = 7;
    reacquire.observe(idle(), std::span{&live_voice, 1});
    reacquire.output.body_submit_status = INPUT_NEXT_STATUS_UNAVAILABLE;
    reacquire.coordinator.tick(reacquire.time);
    reacquire.output.body_submit_status = INPUT_NEXT_STATUS_OK;
    reacquire.advance(idle(), 100ms);
    const auto& neutral = reacquire.output.body_submissions.back();
    INFO("live failed transient replayed while reacquiring after local ownership expiry");
    REQUIRE((reacquire.time >= old_owner_deadline &&
             reacquire.time - live_voice.event.observed_at < 160ms &&
             neutral.request.lease_id == 0 && neutral.request.sequence == 1 &&
             neutral.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             neutral.samples.empty()));
}

} // namespace skyrim_adapter::tests::session
