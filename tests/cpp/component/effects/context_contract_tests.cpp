#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "output_session/fake_output.hpp"
#include "output_session/support.hpp"

#include <input_next_core.h>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <optional>

namespace {
using namespace std::chrono_literals;
using namespace inputnext::adapters;
using namespace inputnext::test::output_session;

TEST_CASE("effects.output_session.open_requires_committed_context",
    "[component][effects][output_session]") {
    constexpr std::array open_failures{
        INPUT_NEXT_STATUS_PENDING,
        INPUT_NEXT_STATUS_REVOKED,
        INPUT_NEXT_STATUS_BUSY,
    };
    for (const auto status : open_failures) {
        FakeOutput output;
        output.open_status = status;
        FakeObserver observer;
        const auto session = std::make_unique<OutputSession>(output, observer, 0);
        TickClock clock{.supplied = MonoTime{} + 126s, .use_steady_clock = false};

        INFO("non-OK open created or bound an uncommitted context");
        REQUIRE_FALSE(session->reconcile(std::nullopt, clock));
        REQUIRE((!session->bound() && !session->binding_started_at() && output.contexts.empty() &&
                 output.close_calls == 0 && observer.binding_count == 0 &&
                 observer.lost_count == 1 && session->diagnostics().last_status == status));

        output.open_status = INPUT_NEXT_STATUS_OK;
        clock.supplied += 1ms;
        INFO("session did not recover once open committed a context");
        REQUIRE_FALSE(session->reconcile(std::nullopt, clock));
        REQUIRE((session->bound() && output.contexts.size() == 1 && observer.binding_count == 1));
        REQUIRE(session->close_and_drain(clock));
    }
}

TEST_CASE("effects.output_session.context_refresh_defers_without_data",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 127s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);
    const auto expected_context = output.contexts.back().context;
    const auto started_at = session->binding_started_at();
    auto frame = body_frame(clock.now(), 910);
    frame.left = trigger();

    constexpr std::array query_deferrals{INPUT_NEXT_STATUS_PENDING, INPUT_NEXT_STATUS_BUSY};
    for (const auto status : query_deferrals) {
        output.query_context_status = status;
        clock.supplied += 1ms;
        observer.publish(clock.now());
        INFO("data-less context refresh discarded the current binding");
        REQUIRE_FALSE(session->reconcile(clock.now(), clock));
        REQUIRE((session->bound() && session->binding_started_at() == started_at &&
                 observer.binding_count == 1 && observer.lost_count == 0 &&
                 FakeOutput::same_context(output.contexts.back().context, expected_context) &&
                 session->diagnostics().last_status == status));
        const auto queries = output.query_context_calls;
        INFO("deferred reconciliation allowed output or queried the context again during send");
        REQUIRE_FALSE(session->send(frame, clock.now(), clock));
        REQUIRE((output.triggers.empty() && output.body_calls.empty() &&
                 output.query_context_calls == queries && output.close_calls == 0));
    }

    output.query_context_status = INPUT_NEXT_STATUS_OK;
    clock.supplied += 1ms;
    observer.publish(clock.now());
    INFO("valid context refresh failed to resume the binding");
    REQUIRE(session->reconcile(clock.now(), clock));
    REQUIRE((session->bound() && observer.binding_count == 1 && observer.lost_count == 0));
    REQUIRE(session->send(frame, clock.now(), clock));
    REQUIRE((output.triggers.size() == 1 && output.body_calls.size() == 1));

    output.query_context_status = INPUT_NEXT_STATUS_REVOKED;
    clock.supplied += 1ms;
    observer.publish(clock.now());
    INFO("REVOKED context information did not retire the exact binding");
    REQUIRE_FALSE(session->reconcile(clock.now(), clock));
    REQUIRE((!session->bound() && observer.lost_count == 1 &&
             session->diagnostics().last_status == INPUT_NEXT_STATUS_REVOKED));

    output.query_context_status = INPUT_NEXT_STATUS_PENDING;
    INFO("pending context data prevented exact cleanup and acknowledgement");
    REQUIRE(session->close_and_drain(clock));
    REQUIRE((output.close_calls == 1 && output.ack_close_calls == 1 && output.contexts.empty()));
}

TEST_CASE("effects.output_session.deferred_delivery_allows_admission_cleanup",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 129s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);
    REQUIRE(session->send(body_frame(clock.now(), 911), clock.now(), clock));

    output.query_context_status = INPUT_NEXT_STATUS_BUSY;
    REQUIRE_FALSE(session->reconcile(clock.now(), clock));
    observer.ready = false;
    REQUIRE_FALSE(session->send(OutputFrame{}, clock.now(), clock));
    REQUIRE((!session->bound() && !session->has_body_owner() && output.close_calls == 1 &&
             output.ack_close_calls == 1 && output.contexts.empty()));
    REQUIRE(session->close_and_drain(clock));
}

TEST_CASE("effects.output_session.busy_resolution_defers_delivery",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 130s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    output.target_status = INPUT_NEXT_STATUS_BUSY;
    REQUIRE_FALSE(session->reconcile(clock.now(), clock));
    REQUIRE_FALSE(session->send(body_frame(clock.now(), 912), clock.now(), clock));
    REQUIRE((session->bound() && output.body_calls.empty()));

    output.target_status = INPUT_NEXT_STATUS_OK;
    REQUIRE(session->reconcile(clock.now(), clock));
    REQUIRE(session->send(body_frame(clock.now(), 912), clock.now(), clock));
    REQUIRE(output.body_calls.size() == 1);
    REQUIRE(session->close_and_drain(clock));
}

TEST_CASE("effects.output_session.cleanup_does_not_resume_deferred_delivery",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 131s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);
    ++output.target.connection_id;
    clock.supplied += 1ms;
    REQUIRE_FALSE(session->reconcile(std::nullopt, clock));

    clock.supplied += 1ms;
    observer.publish(clock.now());
    output.query_context_status = INPUT_NEXT_STATUS_PENDING;
    output.ack_close_status = INPUT_NEXT_STATUS_BUSY;
    REQUIRE_FALSE(session->reconcile(clock.now(), clock));
    REQUIRE(output.ack_close_calls == 1);

    output.ack_close_status = INPUT_NEXT_STATUS_OK;
    const auto queries = output.query_context_calls;
    REQUIRE_FALSE(session->send(body_frame(clock.now(), 913), clock.now(), clock));
    REQUIRE(output.ack_close_calls == 2);
    REQUIRE(session->diagnostics().last_status == INPUT_NEXT_STATUS_OK);
    REQUIRE((output.body_calls.empty() && output.query_context_calls == queries));

    output.query_context_status = INPUT_NEXT_STATUS_OK;
    REQUIRE(session->reconcile(clock.now(), clock));
    REQUIRE(session->send(body_frame(clock.now(), 913), clock.now(), clock));
    REQUIRE(session->close_and_drain(clock));
}

} // namespace
