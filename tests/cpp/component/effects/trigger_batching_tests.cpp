#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "output_session/fake_output.hpp"
#include "output_session/support.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <chrono>
#include <cstdint>
#include <memory>

namespace {
using namespace std::chrono_literals;
using namespace inputnext::adapters;
using namespace inputnext::test::output_session;

TEST_CASE("effects.output_session.compatible_trigger_batch",
    "[component][effects][output_session]") {
    FakeOutput output;
    output.disable_body_leases();
    FakeObserver observer;
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    TickClock clock{.supplied = MonoTime{} + 30s, .use_steady_clock = false};
    bind_with_fresh_sample(session, observer, clock);

    auto frame = OutputFrame{};
    frame.left = trigger(160);
    frame.right = trigger(160);
    frame.left->payload.strength = 0.25F;
    frame.right->payload.frequency = 133.0F;
    INFO("compatible trigger batch failed");
    REQUIRE(session.send(frame, clock.now(), clock));

    INFO("compatible triggers were submitted separately");
    REQUIRE(output.triggers.size() == 1);
    const auto& request = output.triggers.front();
    INFO("combined trigger request did not target both channels");
    REQUIRE(request.channel_mask ==
            (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER));
    INFO("combined trigger request did not preserve each channel payload");
    REQUIRE((request.left_trigger.strength == 0.25F && request.right_trigger.frequency == 133.0F));
    INFO("combined trigger request used a different target");
    REQUIRE(same_target(request.target, output.target));
    INFO("combined trigger request changed the common remaining TTL");
    REQUIRE(request.ttl_ms == 160);
}

TEST_CASE("effects.output_session.independent_trigger_cache",
    "[component][effects][output_session]") {
    FakeOutput output;
    output.disable_body_leases();
    FakeObserver observer;
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    TickClock clock{.supplied = MonoTime{} + 31s, .use_steady_clock = false};
    bind_with_fresh_sample(session, observer, clock);

    auto frame = OutputFrame{};
    frame.left = trigger(200);
    frame.right = trigger(200);
    INFO("initial trigger batch failed");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("initial compatible triggers were not batched");
    REQUIRE(output.triggers.size() == 1);

    clock.supplied += 10ms;
    INFO("cached trigger batch failed");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("batched channels refreshed before their refresh window");
    REQUIRE(output.triggers.size() == 1);

    clock.supplied += 90ms;
    INFO("due trigger batch refresh failed");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("batched channels did not refresh at the cache boundary");
    REQUIRE(output.triggers.size() == 2);
    INFO("due channels were not refreshed together");
    REQUIRE(output.triggers.back().channel_mask ==
            (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER));

    frame.left->payload.strength = 0.35F;
    clock.supplied += 1ms;
    INFO("single-channel trigger update failed");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("changed channel was not submitted by itself");
    REQUIRE(output.triggers.size() == 3);
    INFO("unchanged cached channel was co-submitted with the changed channel");
    REQUIRE((output.triggers.back().channel_mask == INPUT_NEXT_EFFECTS_LEFT_TRIGGER &&
             output.triggers.back().right_trigger.mode == INPUT_NEXT_EFFECTS_TRIGGER_OFF));

    clock.supplied += 99ms;
    INFO("independent right-channel refresh failed");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("refreshing one channel extended the other channel's accepted cache lifetime");
    REQUIRE((output.triggers.size() == 4 &&
             output.triggers.back().channel_mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER));
}

TEST_CASE("effects.output_session.different_trigger_ttls", "[component][effects][output_session]") {
    FakeOutput output;
    output.disable_body_leases();
    FakeObserver observer;
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    TickClock clock{.supplied = MonoTime{} + 32s, .use_steady_clock = false};
    bind_with_fresh_sample(session, observer, clock);

    auto frame = OutputFrame{};
    frame.left = trigger(100);
    frame.right = trigger(80);
    std::uint32_t submit_count = 0;
    output.after_trigger_submit = [&] {
        if (++submit_count == 1) {
            clock.supplied += 25ms;
        }
    };
    const auto sampled_at = clock.now();
    INFO("different-TTL trigger delivery failed");
    REQUIRE(session.send(frame, sampled_at, clock));

    INFO("different-TTL channels were combined");
    REQUIRE(output.triggers.size() == 2);
    INFO("first independently submitted trigger had the wrong TTL");
    REQUIRE((output.triggers[0].channel_mask == INPUT_NEXT_EFFECTS_LEFT_TRIGGER &&
             output.triggers[0].ttl_ms == 100));
    INFO("second trigger TTL was not aged from the original sample after blocking I/O");
    REQUIRE((output.triggers[1].channel_mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER &&
             output.triggers[1].ttl_ms == 55));
}

TEST_CASE("effects.output_session.trigger_expiry_during_submit",
    "[component][effects][output_session]") {
    FakeOutput output;
    output.disable_body_leases();
    FakeObserver observer;
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    TickClock clock{.supplied = MonoTime{} + 33s, .use_steady_clock = false};
    bind_with_fresh_sample(session, observer, clock);

    auto frame = OutputFrame{};
    frame.left = trigger(100);
    frame.right = trigger(50);
    output.after_trigger_submit = [&] {
        clock.supplied += 60ms;
    };
    const auto sampled_at = clock.now();
    INFO("expired second trigger disrupted the first accepted trigger");
    REQUIRE(session.send(frame, sampled_at, clock));

    INFO("expired second trigger was submitted after blocking I/O");
    REQUIRE(output.triggers.size() == 1);
    INFO("first submission unexpectedly included an expiring channel");
    REQUIRE(output.triggers.front().channel_mask == INPUT_NEXT_EFFECTS_LEFT_TRIGGER);
}

TEST_CASE("effects.output_session.busy_combined_trigger_retry",
    "[component][effects][output_session]") {
    FakeOutput output;
    output.disable_body_leases();
    FakeObserver observer;
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    TickClock clock{.supplied = MonoTime{} + 34s, .use_steady_clock = false};
    bind_with_fresh_sample(session, observer, clock);

    auto frame = OutputFrame{};
    frame.left = trigger(180);
    INFO("initial left trigger was not accepted");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("initial left trigger submit count mismatch");
    REQUIRE(output.triggers.size() == 1);
    const auto accepted_left = frame;

    frame.left->payload.strength = 0.4F;
    frame.right = trigger(180);
    output.trigger_status = INPUT_NEXT_STATUS_BUSY;
    clock.supplied += 1ms;
    INFO("BUSY combined trigger submit was reported as success");
    REQUIRE_FALSE(session.send(frame, clock.now(), clock));
    INFO("BUSY combined submit abandoned the binding or did not attempt both due channels");
    REQUIRE((session.bound() && observer.lost_count == 0 && output.triggers.size() == 2 &&
             output.triggers.back().channel_mask ==
                 (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER)));

    INFO("BUSY combined submit changed the previously accepted left payload");
    REQUIRE(session.send(accepted_left, clock.now(), clock));
    INFO("restoring the accepted left frame after BUSY required a submit or clear");
    REQUIRE((output.triggers.size() == 2 && output.trigger_clears.empty()));

    auto right_only = OutputFrame{};
    right_only.right = frame.right;
    clock.supplied += 1ms;
    INFO("BUSY right-only follow-up was reported as success");
    REQUIRE_FALSE(session.send(right_only, clock.now(), clock));
    INFO("failed combined submit advanced the unaccepted right channel or lost the accepted left "
         "channel");
    REQUIRE((output.trigger_clears.size() == 1 &&
             output.trigger_clears.front().second == INPUT_NEXT_EFFECTS_LEFT_TRIGGER &&
             output.triggers.size() == 3 &&
             output.triggers.back().channel_mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER));

    output.trigger_status = INPUT_NEXT_STATUS_OK;
    frame.left = trigger(180);
    frame.left->payload.strength = 0.4F;
    clock.supplied += 1ms;
    INFO("combined trigger retry failed");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("failed combined submit poisoned a per-channel cache or accepted mask");
    REQUIRE((output.triggers.size() == 4 &&
             output.triggers.back().channel_mask ==
                 (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER)));

    frame.right.reset();
    clock.supplied += 1ms;
    INFO("single-channel trigger withdrawal failed");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("combined acceptance did not preserve exact per-channel clear accounting");
    REQUIRE((output.trigger_clears.size() == 2 &&
             output.trigger_clears.back().second == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER));
}

TEST_CASE("effects.output_session.unsupported_trigger_batch",
    "[component][effects][output_session]") {
    FakeOutput missing_capability_output;
    missing_capability_output.disable_body_leases();
    missing_capability_output.capabilities.feature_flags &= ~INPUT_NEXT_EFFECTS_CAP_RIGHT_TRIGGER;
    FakeObserver missing_capability_observer;
    const auto missing_capability_session_owner =
        std::make_unique<OutputSession>(missing_capability_output, missing_capability_observer, 0);
    auto& missing_capability_session = *missing_capability_session_owner;
    TickClock missing_capability_clock{.supplied = MonoTime{} + 35s, .use_steady_clock = false};
    bind_with_fresh_sample(missing_capability_session,
        missing_capability_observer,
        missing_capability_clock);

    auto frame = OutputFrame{};
    frame.left = trigger(140);
    frame.right = trigger(140);
    INFO("missing capability blocked supported trigger delivery");
    REQUIRE(missing_capability_session.send(frame,
        missing_capability_clock.now(),
        missing_capability_clock));
    INFO("unsupported channel was included in the trigger request");
    REQUIRE((missing_capability_output.triggers.size() == 1 &&
             missing_capability_output.triggers.front().channel_mask ==
                 INPUT_NEXT_EFFECTS_LEFT_TRIGGER));

    FakeOutput invalid_output;
    invalid_output.disable_body_leases();
    FakeObserver invalid_observer;
    const auto invalid_session_owner =
        std::make_unique<OutputSession>(invalid_output, invalid_observer, 0);
    auto& invalid_session = *invalid_session_owner;
    TickClock invalid_clock{.supplied = MonoTime{} + 36s, .use_steady_clock = false};
    bind_with_fresh_sample(invalid_session, invalid_observer, invalid_clock);
    frame.right->payload.strength = 1.5F;
    INFO("invalid intent blocked valid channel");
    REQUIRE(invalid_session.send(frame, invalid_clock.now(), invalid_clock));
    INFO("invalid trigger intent was included in a compatible batch");
    REQUIRE((invalid_output.triggers.size() == 1 &&
             invalid_output.triggers.front().channel_mask == INPUT_NEXT_EFFECTS_LEFT_TRIGGER));
}

TEST_CASE("effects.output_session.restricted_combined_trigger",
    "[component][effects][output_session]") {
    FakeOutput output;
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 42;
    FakeObserver observer;
    TickClock clock{.supplied = MonoTime{} + 37s, .use_steady_clock = false};
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    bind_with_fresh_sample(session, observer, clock);

    auto frame = body_frame(clock.now(), 970);
    frame.left = trigger(150);
    frame.right = trigger(150);
    output.after_trigger_submit = [&] {
        observer.restriction = FrameRestriction::neutral;
        observer.restore_after_neutral_read = true;
    };
    INFO("restriction during combined submit failed delivery flow");
    REQUIRE(session.send(frame, clock.now(), clock));

    INFO("restriction setup did not use one combined trigger request");
    REQUIRE((output.triggers.size() == 1 &&
             output.triggers.front().channel_mask ==
                 (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER)));
    INFO("restriction after combined submit did not clear both newly accepted channels");
    REQUIRE((output.trigger_clears.size() == 1 &&
             output.trigger_clears.front().second ==
                 (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER)));
    for (const auto& call : output.body_calls) {
        INFO("restriction observed before body delivery allowed a positive body plan");
        REQUIRE((call.request.pcm_command != INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
                 call.request.fallback_rumble.low_frequency == 0.0F &&
                 call.request.fallback_rumble.high_frequency == 0.0F));
    }
}

TEST_CASE("effects.output_session.expired_combined_trigger",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    TickClock clock{.supplied = MonoTime{} + 38s, .use_steady_clock = false};
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    bind_with_fresh_sample(session, observer, clock);

    auto frame = body_frame(clock.now(), 971);
    frame.left = trigger(150);
    frame.right = trigger(150);
    output.after_trigger_submit = [&] {
        observer.ready = false;
    };
    INFO("admission loss during combined submit was accepted");
    REQUIRE_FALSE(session.send(frame, clock.now(), clock));

    INFO("admission-loss setup did not use one combined trigger request");
    REQUIRE((output.triggers.size() == 1 &&
             output.triggers.front().channel_mask ==
                 (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER)));
    INFO("admission loss after combined submit did not use reserved exact-context close");
    REQUIRE(
        (output.trigger_clears.empty() && output.close_calls == 1 && output.ack_close_calls == 1 &&
            output.closes.front().close_request_id == output.closes.front().context.id));
    INFO("admission loss before body delivery attempted or retained a body owner");
    REQUIRE((output.body_clears.empty() && output.body_calls.empty() && !session.has_body_owner() &&
             observer.expired_count == 1));
}

TEST_CASE("effects.output_session.slow_combined_trigger", "[component][effects][output_session]") {
    FakeOutput output;
    output.disable_body_leases();
    FakeObserver observer;
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    TickClock clock{.supplied = MonoTime{} + 39s};
    bind_with_fresh_sample(session, observer, clock);
    OutputFrame frame{};
    frame.left = trigger(200);
    frame.right = trigger(200);
    output.after_trigger_submit = [&clock] {
        clock.supplied += 100ms;
    };
    const auto sampled_at = clock.now();
    INFO("slow combined trigger delivery failed");
    REQUIRE(session.send(frame, sampled_at, clock));
    INFO("slow combined call resubmitted a channel after its refresh interval elapsed");
    REQUIRE((output.triggers.size() == 1 &&
             output.triggers.front().channel_mask ==
                 (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER)));
}

TEST_CASE("effects.output_session.busy_trigger_body_progress",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    TickClock clock{.supplied = MonoTime{} + 40s, .use_steady_clock = false};
    bind_with_fresh_sample(session, observer, clock);

    auto frame = body_frame(clock.now(), 972);
    frame.left = trigger(100);
    frame.right = trigger(80);
    std::uint32_t submit_count = 0;
    output.trigger_status = INPUT_NEXT_STATUS_BUSY;
    output.after_trigger_submit = [&] {
        if (++submit_count == 1) {
            output.trigger_status = INPUT_NEXT_STATUS_OK;
        }
    };

    INFO("independent BUSY trigger outcome was reported as full success");
    REQUIRE_FALSE(session.send(frame, clock.now(), clock));
    INFO("BUSY on one different-TTL channel suppressed the other eligible channel");
    REQUIRE((session.bound() && output.triggers.size() == 2 &&
             output.triggers[0].channel_mask == INPUT_NEXT_EFFECTS_LEFT_TRIGGER &&
             output.triggers[1].channel_mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER));
    INFO("retryable trigger BUSY suppressed a valid body submission");
    REQUIRE(output.body_calls.size() == 1);
    INFO("successful body submission hid the retryable trigger status");
    REQUIRE(session.diagnostics().last_status == INPUT_NEXT_STATUS_BUSY);

    output.after_trigger_submit = {};
    clock.supplied += 1ms;
    INFO("unaccepted trigger channel did not recover independently");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("retry refreshed the channel already accepted after the other channel returned BUSY");
    REQUIRE((output.triggers.size() == 3 &&
             output.triggers.back().channel_mask == INPUT_NEXT_EFFECTS_LEFT_TRIGGER));
}

} // namespace
