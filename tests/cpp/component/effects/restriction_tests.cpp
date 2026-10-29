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
#include <optional>

namespace {
using namespace inputnext::adapters;
using namespace inputnext::test::output_session;

TEST_CASE("effects.output_session.restriction_before_body_submit",
    "[component][effects][output_session]") {
    FakeOutput output;
    advertise_hd(output, 924);
    FakeObserver observer;
    TickClock clock{.supplied = MonoTime{}};
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    bind_with_fresh_sample(session, observer, clock);

    auto frame = body_frame(clock.now(), 701);
    frame.left = trigger();
    frame.rumble = RumbleIntent{
        .payload = {.low_frequency = 0.8F, .high_frequency = 0.6F},
        .ttl_ms = 200,
    };
    observer.neutral_on_restriction_query = 3;
    observer.restore_after_neutral_read = true;

    INFO("pre-submit restriction send failed");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("pre-submit restriction made a stale or duplicate body submission");
    REQUIRE(output.body_calls.size() == 1);
    INFO("pre-submit restriction retained prepared gameplay PCM");
    REQUIRE((output.body_calls.front().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             output.body_calls.front().samples.empty()));
    INFO("pre-submit restriction retained stale fallback rumble");
    REQUIRE((output.body_calls.front().request.fallback_rumble.low_frequency == 0.0F &&
             output.body_calls.front().request.fallback_rumble.high_frequency == 0.0F));
    INFO("pre-submit restriction delivered a trigger");
    REQUIRE(output.triggers.empty());
}

TEST_CASE("effects.output_session.restriction_during_body_submit",
    "[component][effects][output_session]") {
    FakeOutput output;
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 42;
    FakeObserver observer;
    TickClock clock{.supplied = MonoTime{}};
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    bind_with_fresh_sample(session, observer, clock);

    auto frame = body_frame(clock.now(), 702);
    frame.left = trigger();
    output.after_body_submit = [&] {
        observer.restriction = FrameRestriction::neutral;
        observer.restore_after_neutral_read = true;
    };

    INFO("in-flight body restriction send failed");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("accepted in-flight body was not neutralized after its receipt");
    REQUIRE(output.body_calls.size() == 2);
    INFO("body setup did not submit the original PCM plan");
    REQUIRE(output.body_calls[0].request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
    INFO("neutral follow-up did not stop the accepted PCM plan");
    REQUIRE(output.body_calls[1].request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP);
    INFO("neutral follow-up did not use the accepted body lease");
    REQUIRE((output.body_calls[1].request.lease_id == output.body_calls[0].request.lease_id ||
             (output.body_calls[0].request.lease_id == 0 &&
                 output.body_calls[1].request.lease_id == 400)));
    INFO("neutral follow-up did not advance the accepted body sequence");
    REQUIRE(output.body_calls[1].request.sequence == output.body_calls[0].request.sequence + 1);
    INFO("in-flight body restriction did not clear the trigger accepted before body delivery");
    REQUIRE((output.triggers.size() == 1 && output.trigger_clears.size() == 1 &&
             output.trigger_clears.front().second == INPUT_NEXT_EFFECTS_LEFT_TRIGGER));
}

TEST_CASE("effects.output_session.restriction_during_trigger_submit",
    "[component][effects][output_session]") {
    FakeOutput output;
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 42;
    FakeObserver observer;
    TickClock clock{.supplied = MonoTime{}};
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    bind_with_fresh_sample(session, observer, clock);

    auto frame = body_frame(clock.now(), 703);
    frame.left = trigger();
    INFO("trigger restriction setup send failed");
    REQUIRE(session.send(frame, clock.now(), clock));

    frame.left->payload.strength = 0.3F;
    output.after_trigger_submit = [&] {
        observer.restriction = FrameRestriction::neutral;
        observer.restore_after_neutral_read = true;
    };
    INFO("trigger submit restriction send failed");
    REQUIRE(session.send(frame, clock.now(), clock));

    INFO("trigger submit restriction unexpectedly replayed a trigger");
    REQUIRE(output.triggers.size() == 2);
    INFO("trigger accepted during restriction was not withdrawn");
    REQUIRE((output.trigger_clears.size() == 1 &&
             output.trigger_clears.front().second == INPUT_NEXT_EFFECTS_LEFT_TRIGGER));
    INFO("trigger submit restriction did not neutralize the already-sent body");
    REQUIRE((output.body_calls.size() == 2 &&
             output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP));
}

TEST_CASE("effects.output_session.restriction_during_trigger_clear",
    "[component][effects][output_session]") {
    FakeOutput output;
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 42;
    FakeObserver observer;
    TickClock clock{.supplied = MonoTime{}};
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    bind_with_fresh_sample(session, observer, clock);

    auto frame = body_frame(clock.now(), 704);
    frame.left = trigger();
    frame.right = trigger();
    INFO("trigger clear restriction setup send failed");
    REQUIRE(session.send(frame, clock.now(), clock));

    frame.right.reset();
    output.after_trigger_clear = [&] {
        observer.restriction = FrameRestriction::neutral;
        observer.restore_after_neutral_read = true;
    };
    INFO("trigger clear restriction send failed");
    REQUIRE(session.send(frame, clock.now(), clock));

    std::uint32_t cleared_channels = 0;
    for (const auto& clear : output.trigger_clears) {
        cleared_channels |= clear.second;
    }
    INFO("restriction did not age the original frame as neutral after a blocked clear");
    REQUIRE(
        cleared_channels == (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER));
    INFO("neutral original frame was revived after trigger clear");
    REQUIRE(output.triggers.size() == 1);
    INFO("trigger clear restriction did not neutralize the already-sent body");
    REQUIRE((output.body_calls.size() == 2 &&
             output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP));
}

namespace {
void failed_trigger_restriction_still_clears_owned_body(bool fail_clear) {
    FakeOutput output;
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 42;
    FakeObserver observer;
    TickClock clock{.supplied = MonoTime{}};
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    bind_with_fresh_sample(session, observer, clock);

    auto frame = body_frame(clock.now(), 706);
    frame.left = trigger();
    INFO("failed-trigger restriction setup send failed");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("failed-trigger restriction setup did not acquire a body owner");
    REQUIRE(session.has_body_owner());
    const auto accepted_body_request = output.body_calls.front().request;

    const auto restrict_during_failure = [&] {
        observer.restriction = FrameRestriction::neutral;
        observer.restore_after_neutral_read = true;
    };
    if (fail_clear) {
        frame.left.reset();
        output.trigger_clear_status = INPUT_NEXT_STATUS_UNAVAILABLE;
        output.after_trigger_clear = restrict_during_failure;
    } else {
        frame.left->payload.strength = 0.3F;
        output.trigger_status = INPUT_NEXT_STATUS_UNAVAILABLE;
        output.after_trigger_submit = restrict_during_failure;
    }

    INFO("failed trigger operation was reported as successful");
    REQUIRE_FALSE(session.send(frame, clock.now(), clock));
    INFO("trigger failure did not preserve binding-loss handling");
    REQUIRE((!session.bound() && observer.lost_count == 1));
    INFO("trigger failure used ordinary body clear instead of retaining the exact context for "
         "cleanup");
    REQUIRE((output.body_clears.empty() && output.close_calls == 0 && !session.has_body_owner()));
    INFO("context cleanup admission hid the failed trigger operation status");
    REQUIRE(session.diagnostics().last_status == INPUT_NEXT_STATUS_UNAVAILABLE);
    INFO("trigger failure did not close and acknowledge the exact context owning the body lease");
    REQUIRE(
        (session.close_and_drain(clock) && output.close_calls == 1 && output.ack_close_calls == 1 &&
            output.acknowledgements.front().context.id == accepted_body_request.context.id &&
            same_target(output.acknowledgements.front().target, accepted_body_request.target)));
}
} // namespace

TEST_CASE("effects.output_session.failed_trigger_restriction_cleanup",
    "[component][effects][output_session]") {
    failed_trigger_restriction_still_clears_owned_body(false);
    failed_trigger_restriction_still_clears_owned_body(true);
}

namespace {
class DefaultIdentityObserver final : public DeliveryObserver {
public:
    [[nodiscard]] bool admitted([[maybe_unused]] MonoTime now) const noexcept override {
        return true;
    }

    void binding_lost([[maybe_unused]] input_next_status status) noexcept override {}

    void binding_changed([[maybe_unused]] MonoTime started_at) noexcept override {}

    void body_rebound([[maybe_unused]] MonoTime baseline_at) noexcept override {}

    void admission_expired() noexcept override {}
};
} // namespace

TEST_CASE("effects.output_session.default_frame_restriction",
    "[component][effects][output_session]") {
    FakeOutput output;
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 42;
    DefaultIdentityObserver observer;
    TickClock clock{.supplied = MonoTime{}};
    const auto session_owner = std::make_unique<OutputSession>(output, observer, 0);
    auto& session = *session_owner;
    INFO("initial target query unexpectedly completed a binding");
    REQUIRE_FALSE(session.reconcile(std::nullopt, clock));
    clock.supplied += 1ms;
    INFO("default observer did not establish a fresh binding");
    REQUIRE(session.reconcile(clock.now(), clock));

    auto frame = body_frame(clock.now(), 705);
    frame.left = trigger();
    INFO("default identity observer rejected the frame");
    REQUIRE(session.send(frame, clock.now(), clock));
    INFO("default observer did not preserve body delivery");
    REQUIRE((output.body_calls.size() == 1 &&
             output.body_calls.front().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE));
    INFO("default observer did not preserve trigger delivery");
    REQUIRE(output.triggers.size() == 1);
}

} // namespace
