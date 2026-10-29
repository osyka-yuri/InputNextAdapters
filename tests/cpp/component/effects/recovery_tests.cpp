#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "output_session/fake_output.hpp"
#include "output_session/support.hpp"

#include <input_next_core.h>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <chrono>
#include <memory>
#include <optional>

namespace {
using namespace std::chrono_literals;
using namespace inputnext::adapters;
using namespace inputnext::test::output_session;

TEST_CASE("effects.output_session.mandatory_clear_recovery",
    "[component][effects][output_session]") {
    FakeOutput clear_output;
    FakeObserver clear_observer;
    const auto clear_session = std::make_unique<OutputSession>(clear_output, clear_observer, 0);
    TickClock clear_clock{.supplied = MonoTime{} + 80s, .use_steady_clock = false};
    bind_with_fresh_sample(*clear_session, clear_observer, clear_clock);
    OutputFrame owned = body_frame(clear_clock.now(), 111);
    owned.left = trigger();
    INFO("clear recovery body setup failed");
    REQUIRE(clear_session->send(owned, clear_clock.now(), clear_clock));
    clear_output.body_clear_status = INPUT_NEXT_STATUS_BUSY;
    INFO("failed mandatory body clear discarded its still-live owner");
    REQUIRE((!clear_session->clear_body(clear_clock) && clear_session->has_body_owner()));
    clear_clock.supplied += 1ms;
    clear_output.body_clear_status = INPUT_NEXT_STATUS_OK;
    INFO("mandatory body clear did not recover after the output became available");
    REQUIRE((clear_session->clear_body(clear_clock) && !clear_session->has_body_owner()));
    INFO("failed clear changed ownership sequence before success");
    REQUIRE((clear_output.body_clears.size() == 2 &&
             clear_output.body_clears[0].sequence == clear_output.body_clears[1].sequence));
}

TEST_CASE("effects.output_session.admission_expiry", "[component][effects][output_session]") {
    FakeOutput expiry_output;
    FakeObserver expiry_observer;
    const auto expiry_session = std::make_unique<OutputSession>(expiry_output, expiry_observer, 0);
    TickClock expiry_clock{.supplied = MonoTime{} + 90s, .use_steady_clock = false};
    bind_with_fresh_sample(*expiry_session, expiry_observer, expiry_clock);
    OutputFrame expiring = body_frame(expiry_clock.now(), 112);
    expiring.right = trigger();
    INFO("expiry body setup failed");
    REQUIRE(expiry_session->send(expiring, expiry_clock.now(), expiry_clock));
    expiry_output.close_status = INPUT_NEXT_STATUS_PENDING;
    expiry_observer.ready = false;
    expiry_clock.supplied += 1ms;
    INFO("expired admission allowed a gameplay frame");
    REQUIRE_FALSE(expiry_session->send(OutputFrame(), expiry_clock.now(), expiry_clock));
    INFO("admission expiry did not revoke the exact context through reserved cleanup");
    REQUIRE((
        expiry_observer.expired_count == 1 && expiry_output.trigger_clears.empty() &&
        expiry_output.body_clears.empty() && expiry_output.close_calls == 1 &&
        expiry_output.closes.back().close_request_id == expiry_output.closes.back().context.id &&
        !expiry_session->has_body_owner() && !expiry_session->bound()));
    INFO("pending close was treated as a definite revocation receipt");
    REQUIRE(expiry_output.ack_close_calls == 0);
    expiry_output.close_status = INPUT_NEXT_STATUS_REVOKED;
    INFO("pending admission close did not retry and acknowledge without ordinary mutation calls");
    REQUIRE((expiry_session->close_and_drain(expiry_clock) && expiry_output.ack_close_calls == 1 &&
             expiry_output.trigger_clears.empty() && expiry_output.body_clears.empty()));
}

// Trigger clear failure abandons the binding and notifies the observer.
TEST_CASE("effects.output_session.trigger_clear_failure", "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 100s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    OutputFrame frame{};
    frame.left = trigger();
    INFO("initial trigger send failed");
    REQUIRE(session->send(frame, clock.now(), clock));
    INFO("trigger was not submitted");
    REQUIRE(output.triggers.size() == 1);

    output.trigger_clear_status = INPUT_NEXT_STATUS_UNAVAILABLE;
    INFO("withdraw_triggers must return false on clear failure");
    REQUIRE_FALSE(session->withdraw_triggers());
    INFO("failed trigger clear must abandon binding");
    REQUIRE_FALSE(session->bound());
    INFO("observer must be notified of lost binding");
    REQUIRE(observer.lost_count == 1);
    INFO("diagnostic status mismatch");
    REQUIRE(session->diagnostics().last_status == INPUT_NEXT_STATUS_UNAVAILABLE);
    INFO("withdraw_triggers on unbound session must be idempotent and succeed");
    REQUIRE(session->withdraw_triggers());
}

// Deadline boundary conditions when clear_body repeatedly fails.
TEST_CASE("effects.output_session.body_clear_deadline", "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 120s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    OutputFrame const frame = body_frame(clock.now(), 222);
    INFO("body frame send failed");
    REQUIRE(session->send(frame, clock.now(), clock));
    INFO("body owner was not acquired");
    REQUIRE(session->has_body_owner());

    output.body_clear_status = INPUT_NEXT_STATUS_BUSY;
    const auto clear_calls_before = output.body_clears.size();

    // 1 ms before deadline (200 ms TTL)
    clock.supplied += 199ms;
    INFO("clear_body before deadline must fail on BUSY");
    REQUIRE_FALSE(session->clear_body(clock));
    INFO("body owner must remain retained before deadline");
    REQUIRE(session->has_body_owner());
    INFO("clear_body before deadline must invoke output");
    REQUIRE(output.body_clears.size() == clear_calls_before + 1);

    // Exactly at deadline (200 ms TTL)
    clock.supplied += 1ms;
    INFO("clear_body at or past deadline must succeed via TTL expiration");
    REQUIRE(session->clear_body(clock));
    INFO("body owner must be cleared when deadline expires");
    REQUIRE_FALSE(session->has_body_owner());
    INFO("clear_body at deadline must not call output");
    REQUIRE(output.body_clears.size() == clear_calls_before + 1);
}

// Trigger submit failure abandons the binding, and subsequent recovery rebinds.
TEST_CASE("effects.output_session.trigger_submit_recovery",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 140s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    output.trigger_status = INPUT_NEXT_STATUS_UNAVAILABLE;
    OutputFrame frame{};
    frame.left = trigger();
    INFO("send must fail when output submit fails");
    REQUIRE_FALSE(session->send(frame, clock.now(), clock));
    INFO("binding must be abandoned on trigger submit failure");
    REQUIRE_FALSE(session->bound());
    INFO("binding_lost must be reported on submit failure");
    REQUIRE(observer.lost_count == 1);
    INFO("diagnostic status mismatch");
    REQUIRE(session->diagnostics().last_status == INPUT_NEXT_STATUS_UNAVAILABLE);

    // Recovery: output becomes available again
    output.trigger_status = INPUT_NEXT_STATUS_OK;
    clock.supplied += 1ms;
    INFO("recovery target query did not establish a new binding");
    REQUIRE_FALSE(session->reconcile(std::nullopt, clock));
    INFO("session must re-bind successfully after recovery");
    REQUIRE((session->bound() && observer.binding_count == 2));
    clock.supplied += 1ms;
    observer.publish(clock.now());
    INFO("fresh post-recovery observation was not admitted");
    REQUIRE(session->reconcile(clock.now(), clock));
    output.triggers.clear();
    clock.supplied += 1ms;
    INFO("send must succeed after session recovery");
    REQUIRE(session->send(frame, clock.now(), clock));
    INFO("trigger must be submitted after recovery");
    REQUIRE(output.triggers.size() == 1);
}

} // namespace
