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
using namespace std::chrono_literals;
using namespace inputnext::adapters;
using namespace inputnext::test::output_session;

TEST_CASE("effects.output_session.stale_target_clear", "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 150s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    const auto old_target = output.target;
    const auto old_context = output.contexts.front().context;
    const auto old_deadline_origin = clock.now();
    INFO("old target body setup failed");
    REQUIRE(session->send(body_frame(clock.now(), 901), clock.now(), clock));
    INFO("old target body owner was not accepted");
    REQUIRE(session->has_body_owner());

    ++output.target.connection_id;
    const auto old_observation = clock.now();
    clock.supplied += 1ms;
    INFO("replacement target reused the old observation");
    REQUIRE_FALSE(session->reconcile(old_observation, clock));
    INFO("target rebind used ordinary mutations instead of retaining the old context for reserved "
         "cleanup");
    REQUIRE((!session->has_body_owner() && output.body_clears.empty() &&
             output.trigger_clears.empty() && output.close_calls == 0));
    INFO("replacement context did not retain its successful resolve diagnostic");
    REQUIRE(session->diagnostics().last_status == INPUT_NEXT_STATUS_OK);
    INFO("replacement binding did not start before the old lease deadline");
    REQUIRE((
        session->binding_started_at() == clock.now() && clock.now() - old_deadline_origin < 200ms));

    clock.supplied += 1ms;
    observer.publish(clock.now());
    INFO("replacement binding rejected its fresh observation");
    REQUIRE(session->reconcile(clock.now(), clock));
    INFO("target rebind did not close and acknowledge the exact prior context and target");
    REQUIRE((output.close_calls == 1 && output.ack_close_calls == 1 &&
             output.acknowledgements.front().context.id == old_context.id &&
             same_target(output.acknowledgements.front().target, old_target)));
    INFO("replacement body acquisition failed");
    REQUIRE(session->send(body_frame(clock.now(), 902), clock.now(), clock));
    const auto& replacement = output.body_calls.back().request;
    INFO("replacement target did not acquire a fresh lease and sequence");
    REQUIRE((output.body_calls.size() == 2 && same_target(replacement.target, output.target) &&
             replacement.lease_id == 0 && replacement.sequence == 1));
}

TEST_CASE("effects.output_session.uncertain_body_clears", "[component][effects][output_session]") {
    FakeOutput busy_output;
    FakeObserver busy_observer;
    const auto busy_session = std::make_unique<OutputSession>(busy_output, busy_observer, 0);
    TickClock busy_clock{.supplied = MonoTime{} + 160s, .use_steady_clock = false};
    bind_with_fresh_sample(*busy_session, busy_observer, busy_clock);
    INFO("body owner setup failed");
    REQUIRE(busy_session->send(body_frame(busy_clock.now(), 903), busy_clock.now(), busy_clock));
    const auto accepted_at = busy_clock.now();

    busy_output.body_clear_status = INPUT_NEXT_STATUS_BUSY;
    busy_clock.supplied += 1ms;
    INFO("BUSY clear discarded its live owner");
    REQUIRE((!busy_session->clear_body(busy_clock) && busy_session->has_body_owner()));
    busy_clock.supplied += 1ms;
    INFO("BUSY retry changed ownership");
    REQUIRE((!busy_session->clear_body(busy_clock) && busy_session->has_body_owner()));
    INFO("BUSY clear retry advanced ownership or lost its diagnostic");
    REQUIRE((busy_output.body_clears.size() == 2 &&
             busy_output.body_clears[0].sequence == busy_output.body_clears[1].sequence &&
             busy_output.body_clears[0].lease_id == busy_output.body_clears[1].lease_id &&
             busy_session->diagnostics().last_status == INPUT_NEXT_STATUS_BUSY));
    busy_clock.supplied = accepted_at + 200ms;
    INFO("body owner did not expire at its deadline");
    REQUIRE((busy_session->clear_body(busy_clock) && !busy_session->has_body_owner()));
    INFO("expired body owner issued an unbounded clear retry");
    REQUIRE(busy_output.body_clears.size() == 2);

    for (const auto status : {INPUT_NEXT_STATUS_UNAVAILABLE,
             INPUT_NEXT_STATUS_INVALID_ARGUMENT,
             INPUT_NEXT_STATUS_TIMED_OUT_UNKNOWN}) {
        FakeOutput output;
        FakeObserver observer;
        const auto session = std::make_unique<OutputSession>(output, observer, 0);
        TickClock clock{.supplied = MonoTime{} + 161s, .use_steady_clock = false};
        bind_with_fresh_sample(*session, observer, clock);
        INFO("terminal clear setup failed");
        REQUIRE(session->send(body_frame(clock.now(), 904), clock.now(), clock));
        output.body_clear_status = status;
        clock.supplied += 1ms;
        INFO("terminal clear status was reported as success");
        REQUIRE_FALSE(session->clear_body(clock));
        INFO("terminal body-clear status left a mutation owner live");
        REQUIRE(
            (!session->has_body_owner() && !session->bound() && output.body_clears.size() == 1));
        INFO("terminal body-clear status did not settle through exact reserved context cleanup");
        REQUIRE((session->close_and_drain(clock) && output.close_calls == 1 &&
                 output.ack_close_calls == 1 &&
                 output.closes.front().close_request_id == output.closes.front().context.id));
    }

    FakeOutput stale_lease_output;
    FakeObserver stale_lease_observer;
    const auto stale_lease_session =
        std::make_unique<OutputSession>(stale_lease_output, stale_lease_observer, 0);
    TickClock stale_lease_clock{.supplied = MonoTime{} + 162s, .use_steady_clock = false};
    bind_with_fresh_sample(*stale_lease_session, stale_lease_observer, stale_lease_clock);
    INFO("stale lease setup failed");
    REQUIRE(stale_lease_session->send(body_frame(stale_lease_clock.now(), 905),
        stale_lease_clock.now(),
        stale_lease_clock));
    stale_lease_output.body_clear_status = INPUT_NEXT_STATUS_STALE_LEASE;
    stale_lease_clock.supplied += 1ms;
    INFO("STALE_LEASE did not retire only the exact expired body owner");
    REQUIRE((stale_lease_session->clear_body(stale_lease_clock) &&
             !stale_lease_session->has_body_owner() && stale_lease_session->bound() &&
             stale_lease_output.body_clears.size() == 1));
}

TEST_CASE("effects.output_session.busy_target_queries", "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 170s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    auto frame = body_frame(clock.now(), 904);
    frame.left = trigger();
    INFO("body and trigger setup failed");
    REQUIRE(session->send(frame, clock.now(), clock));
    const auto original_target = output.target;
    const auto original_capabilities = output.capabilities;
    const auto original_binding_started_at = session->binding_started_at();
    const auto original_observation = clock.now();
    const auto body_calls_before = output.body_calls.size();
    const auto trigger_calls_before = output.triggers.size();
    const auto body_clears_before = output.body_clears.size();
    const auto trigger_clears_before = output.trigger_clears.size();

    output.target_status = INPUT_NEXT_STATUS_BUSY;
    for (std::uint64_t attempt = 1; attempt <= 3; ++attempt) {
        output.target.connection_id = original_target.connection_id + attempt;
        output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
        output.capabilities.audio_instance_id = 9000 + attempt;
        clock.supplied += 1ms;
        const auto failed_observation = clock.now() + 1h;
        INFO("BUSY target query was reported as ready");
        REQUIRE_FALSE(session->reconcile(failed_observation, clock));
        INFO("BUSY target query changed binding, owner, observer, or freshness state");
        REQUIRE((session->bound() && session->has_body_owner() && observer.ready &&
                 observer.lost_count == 0 && observer.binding_count == 1 &&
                 observer.body_rebound_count == 0 &&
                 session->binding_started_at() == original_binding_started_at));
        INFO("BUSY target query initiated delivery or cleanup I/O");
        REQUIRE((output.body_calls.size() == body_calls_before &&
                 output.triggers.size() == trigger_calls_before &&
                 output.body_clears.size() == body_clears_before &&
                 output.trigger_clears.size() == trigger_clears_before));
        INFO("BUSY target query did not retain its actual diagnostic");
        REQUIRE(session->diagnostics().last_status == INPUT_NEXT_STATUS_BUSY);
    }

    output.target_status = INPUT_NEXT_STATUS_OK;
    output.target = original_target;
    output.capabilities = original_capabilities;
    clock.supplied += 1ms;
    INFO("same target did not recover after BUSY");
    REQUIRE(session->reconcile(original_observation, clock));
    INFO("accepted delivery cache did not survive BUSY");
    REQUIRE(session->send(frame, clock.now(), clock));
    INFO("recovery repeated accepted output after a transient BUSY query");
    REQUIRE((output.body_calls.size() == body_calls_before &&
             output.triggers.size() == trigger_calls_before &&
             output.body_clears.size() == body_clears_before &&
             output.trigger_clears.size() == trigger_clears_before));
}

TEST_CASE("effects.output_session.busy_audio_observation", "[component][effects][output_session]") {
    FakeOutput output;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 700;
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 180s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);
    INFO("initial audio owner setup failed");
    REQUIRE(session->send(body_frame(clock.now(), 905), clock.now(), clock));
    const auto stale_observation = clock.now();

    output.capabilities.audio_instance_id = 701;
    clock.supplied += 1ms;
    INFO("real audio replacement lost its target binding");
    REQUIRE(session->reconcile(stale_observation, clock));
    INFO("real audio replacement did not clear the prior audio owner");
    REQUIRE((observer.body_rebound_count == 1 && output.body_clears.size() == 1 &&
             output.body_clears.back().audio_instance_id == 700));

    clock.supplied += 1ms;
    observer.publish(clock.now());
    output.target_status = INPUT_NEXT_STATUS_BUSY;
    output.target.connection_id += 1;
    output.capabilities.audio_instance_id = 702;
    const auto failed_observation = clock.now() + 1h;
    INFO("BUSY audio query was reported as ready");
    REQUIRE_FALSE(session->reconcile(failed_observation, clock));
    output.target_status = INPUT_NEXT_STATUS_OK;
    output.target.connection_id -= 1;
    output.capabilities.audio_instance_id = 701;

    clock.supplied += 1ms;
    observer.publish(clock.now());
    INFO("failed reconciliation allowed a body send before the fresh-observation wait ended");
    REQUIRE_FALSE(session->send(body_frame(clock.now(), 906), clock.now(), clock));
    INFO("BUSY query observation or output fields bypassed the new audio baseline");
    REQUIRE((output.body_calls.size() == 1 && !session->has_body_owner()));

    clock.supplied += 1ms;
    observer.publish(clock.now());
    INFO("fresh audio observation was not admitted");
    REQUIRE(session->reconcile(clock.now(), clock));
    INFO("fresh audio owner acquisition failed");
    REQUIRE(session->send(body_frame(clock.now(), 906), clock.now(), clock));
    INFO("recovered body did not use the actual replacement audio instance");
    REQUIRE((output.body_calls.size() == 2 &&
             output.body_calls.back().request.audio_instance_id == 701));
}

TEST_CASE("effects.output_session.busy_trigger_channels", "[component][effects][output_session]") {
    FakeOutput output;
    output.disable_body_leases();
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 190s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    OutputFrame left{};
    left.left = trigger();
    INFO("initial left trigger was not accepted");
    REQUIRE(session->send(left, clock.now(), clock));
    INFO("initial left trigger submit count mismatch");
    REQUIRE(output.triggers.size() == 1);

    OutputFrame both = left;
    both.right = trigger();
    output.trigger_status = INPUT_NEXT_STATUS_BUSY;
    clock.supplied += 1ms;
    INFO("BUSY trigger submit was reported as success");
    REQUIRE_FALSE(session->send(both, clock.now(), clock));
    INFO("BUSY trigger submit abandoned binding or changed the already accepted channel");
    REQUIRE((session->bound() && observer.lost_count == 0 && output.triggers.size() == 2 &&
             output.triggers.back().left_trigger.mode == INPUT_NEXT_EFFECTS_TRIGGER_OFF &&
             output.triggers.back().right_trigger.mode != INPUT_NEXT_EFFECTS_TRIGGER_OFF));

    output.trigger_status = INPUT_NEXT_STATUS_OK;
    clock.supplied += 1ms;
    INFO("trigger submit did not recover after BUSY");
    REQUIRE(session->send(both, clock.now(), clock));
    INFO("retry resent an accepted channel or failed to retry the rejected channel");
    REQUIRE((output.triggers.size() == 3 &&
             output.triggers.back().left_trigger.mode == INPUT_NEXT_EFFECTS_TRIGGER_OFF &&
             output.triggers.back().right_trigger.mode != INPUT_NEXT_EFFECTS_TRIGGER_OFF));

    OutputFrame right_only{};
    right_only.right = trigger();
    clock.supplied += 1ms;
    INFO("partial trigger withdrawal failed");
    REQUIRE(session->send(right_only, clock.now(), clock));
    INFO("previously accepted partial trigger was not withdrawn when no longer desired");
    REQUIRE((output.trigger_clears.size() == 1 &&
             output.trigger_clears.back().second == INPUT_NEXT_EFFECTS_LEFT_TRIGGER));

    output.trigger_clear_status = INPUT_NEXT_STATUS_BUSY;
    INFO("BUSY trigger clear was reported as success");
    REQUIRE_FALSE(session->withdraw_triggers());
    INFO("BUSY trigger clear discarded accepted channel accounting");
    REQUIRE((session->bound() && observer.lost_count == 0 && output.trigger_clears.size() == 2 &&
             output.trigger_clears.back().second == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER));
    output.trigger_clear_status = INPUT_NEXT_STATUS_OK;
    INFO("trigger clear did not recover after BUSY");
    REQUIRE(session->withdraw_triggers());
    INFO("trigger clear retry did not target the still accepted channel");
    REQUIRE((output.trigger_clears.size() == 3 &&
             output.trigger_clears.back().second == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER));
}

} // namespace
