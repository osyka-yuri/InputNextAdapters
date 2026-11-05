#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <input_next_core.h>
#include <input_next_effects.h>
#include <memory>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>
#include <vector>

namespace skyrim_adapter::tests::session {

TEST_CASE("skyrim.session.body_initial_busy_retries_fresh_lease", "[component][skyrim][session]") {
    const auto rejected_owner = std::make_unique<Fixture>();
    auto& rejected = *rejected_owner;
    rejected.output.body_submit_status = INPUT_NEXT_STATUS_BUSY;
    rejected.settle(idle());
    INFO("BUSY acquisition was recorded as body ownership");
    REQUIRE(rejected.output.active_body_lease == 0);
    rejected.output.body_submit_status = INPUT_NEXT_STATUS_OK;
    rejected.advance(idle(), 100ms);
    INFO("body acquire did not recover from BUSY with a fresh exact lease");
    REQUIRE((rejected.output.active_body_lease != 0 &&
             rejected.output.body_submissions.back().request.lease_id == 0 &&
             rejected.output.body_submissions.back().request.sequence == 1));
}

TEST_CASE("skyrim.session.body_failed_clear_retries_exact_sequence_until_ttl",
    "[component][skyrim][session]") {
    const auto failed_clear_owner = std::make_unique<Fixture>();
    auto& failed_clear = *failed_clear_owner;
    failed_clear.settle(idle());
    const auto accepted_lease = failed_clear.output.active_body_lease;
    failed_clear.output.body_clear_status = INPUT_NEXT_STATUS_BUSY;
    failed_clear.coordinator.control(Control::focus_lost, failed_clear.time);
    failed_clear.coordinator.tick(failed_clear.time);
    INFO("failed withdrawal did not address the exact finite owner");
    REQUIRE((failed_clear.output.body_clears.size() == 1 &&
             failed_clear.output.body_clears.back().lease_id == accepted_lease &&
             failed_clear.output.body_clears.back().sequence == 2));
    failed_clear.coordinator.tick(failed_clear.time + 100ms);
    INFO("failed body withdrawal was not retried with the same unaccepted sequence");
    REQUIRE((failed_clear.output.body_clears.size() == 2 &&
             failed_clear.output.body_clears.back().sequence == 2));
    failed_clear.coordinator.tick(failed_clear.time + 199ms);
    INFO("body withdrawal stopped retrying before its accepted TTL");
    REQUIRE(failed_clear.output.body_clears.size() == 3);
    failed_clear.coordinator.tick(failed_clear.time + 200ms);
    INFO("failed body ownership outlived its accepted 200ms deadline");
    REQUIRE(failed_clear.output.body_clears.size() == 3);
}

TEST_CASE("skyrim.session.body_hd_route_fallback_recovers_on_held_renewal",
    "[component][skyrim][session]") {
    const auto recovery_owner = std::make_unique<Fixture>();
    auto& recovery = *recovery_owner;
    recovery.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    recovery.output.capabilities.audio_instance_id = 33;
    recovery.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC;
    recovery.settle(idle());
    recovery.advance(casting());
    const auto failed_replace_count = recovery.output.body_submissions.size();
    const auto& fallback_attempt = recovery.output.body_submissions.back();
    const auto fallback_attempt_sequence = fallback_attempt.request.sequence;
    const auto original_held_pcm = fallback_attempt.samples;
    INFO("fallback test did not attempt an authored HD replacement");
    REQUIRE((recovery.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             recovery.output.body_submissions.back().request.route_preference ==
                 INPUT_NEXT_EFFECTS_BODY_ROUTE_PREFER_HD));
    recovery.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    recovery.coordinator.tick(recovery.time + 20ms);
    INFO("capability refresh replayed failed PCM or changed route by itself");
    REQUIRE(recovery.output.body_submissions.size() == failed_replace_count);
    recovery.advance(casting(), 100ms);
    INFO("held feedback did not retry a fallback PCM at elapsed time for explicit recovery");
    REQUIRE((
        recovery.output.body_submissions.size() == failed_replace_count + 1 &&
        recovery.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
        recovery.output.body_submissions.back().request.route_preference ==
            INPUT_NEXT_EFFECTS_BODY_ROUTE_PREFER_HD &&
        recovery.output.body_submissions.back().request.sequence == fallback_attempt_sequence + 1 &&
        recovery.output.body_submissions.back().samples[1] ==
            original_held_pcm[(100 * 48 * 2) + 1]));
}

TEST_CASE("skyrim.session.body_bad_receipt_reacquires_fresh_lease",
    "[component][skyrim][session]") {
    const auto malformed_owner = std::make_unique<Fixture>();
    auto& malformed = *malformed_owner;
    malformed.output.receipt_fault = ReceiptFault::sequence;
    malformed.settle(idle());
    INFO("test fake failed to model the bounded unknown lease after a malformed receipt");
    REQUIRE(malformed.output.active_body_lease != 0);
    malformed.output.receipt_fault = ReceiptFault::none;
    malformed.advance(idle(), 100ms);
    INFO("malformed receipt mutated accepted lease or sequence state");
    REQUIRE((malformed.output.body_submissions.back().request.lease_id == 0 &&
             malformed.output.body_submissions.back().request.sequence == 1));
}

} // namespace skyrim_adapter::tests::session
