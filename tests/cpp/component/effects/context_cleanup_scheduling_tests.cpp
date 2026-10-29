#include <catch2/catch_test_macros.hpp>

#include "output_session/fake_output.hpp"
#include "output_session/support.hpp"

#include <input_next_core.h>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>

namespace {
using namespace std::chrono_literals;
using namespace inputnext::adapters;
using namespace inputnext::test::output_session;

void retain_old_contexts(OutputSession& session,
    FakeOutput& output,
    TickClock& clock,
    std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        ++output.target.connection_id;
        clock.supplied += 1ms;
        REQUIRE_FALSE(session.reconcile(std::nullopt, clock));
    }
}

TEST_CASE("effects.output_session.cleanup_budget_and_fairness",
    "[component][effects][output_session]") {
    constexpr std::size_t pending_count = 32;
    constexpr std::array failures{INPUT_NEXT_STATUS_BUSY, INPUT_NEXT_STATUS_UNAVAILABLE};
    for (const auto failure : failures) {
        FakeOutput output;
        FakeObserver observer;
        const auto session = std::make_unique<OutputSession>(output, observer, 0);
        TickClock clock{.supplied = MonoTime{} + 300s, .use_steady_clock = false};
        bind_with_fresh_sample(*session, observer, clock);
        output.query_context_status = failure;
        output.close_status = failure;
        retain_old_contexts(*session, output, clock, pending_count);
        REQUIRE(output.contexts.size() == pending_count + 1);
        clock.supplied += 1ms;
        observer.publish(clock.now());

        output.target_status = INPUT_NEXT_STATUS_BUSY;
        output.context_queries.clear();
        output.closes.clear();
        for (std::size_t turn = 0; turn < pending_count; ++turn) {
            const auto queries = output.query_context_calls;
            const auto closes = output.close_calls;
            if (turn % 2 == 0) {
                REQUIRE_FALSE(session->send(OutputFrame{}, clock.now(), clock));
            } else {
                REQUIRE_FALSE(session->reconcile(clock.now(), clock));
            }
            REQUIRE(output.query_context_calls == queries + 1);
            REQUIRE(output.close_calls == closes + 1);
            REQUIRE(output.ack_close_calls == 0);
        }

        for (std::size_t index = 0; index < pending_count; ++index) {
            const auto& context = output.contexts[index].context;
            REQUIRE(std::ranges::count_if(output.closes, [&](const auto& request) {
                return FakeOutput::same_context(request.context, context);
            }) == 1);
        }
        REQUIRE((output.triggers.empty() && output.body_calls.empty()));

        output.query_context_status = INPUT_NEXT_STATUS_OK;
        output.close_status = INPUT_NEXT_STATUS_REVOKED;
        REQUIRE_FALSE(session->close_and_drain(clock));
        REQUIRE(session->close_and_drain(clock));
        REQUIRE(output.contexts.empty());
    }
}

TEST_CASE("effects.output_session.cleanup_receipt_budget", "[component][effects][output_session]") {
    constexpr std::size_t pending_count = 4;
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 310s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);
    output.close_status = INPUT_NEXT_STATUS_BUSY;
    retain_old_contexts(*session, output, clock, pending_count);

    output.target_status = INPUT_NEXT_STATUS_BUSY;
    output.close_status = INPUT_NEXT_STATUS_REVOKED;
    output.ack_close_status = INPUT_NEXT_STATUS_BUSY;
    for (std::size_t turn = 0; turn < pending_count; ++turn) {
        const auto queries = output.query_context_calls;
        const auto closes = output.close_calls;
        const auto acknowledgements = output.ack_close_calls;
        REQUIRE_FALSE(session->reconcile(clock.now(), clock));
        REQUIRE(output.query_context_calls == queries + 1);
        REQUIRE(output.close_calls == closes + 1);
        REQUIRE(output.ack_close_calls == acknowledgements + 1);
    }

    const auto queries = output.query_context_calls;
    const auto closes = output.close_calls;
    output.ack_close_status = INPUT_NEXT_STATUS_OK;
    clock.supplied += 1ms;
    observer.publish(clock.now());
    for (std::size_t turn = 0; turn < pending_count; ++turn) {
        const auto acknowledgements = output.ack_close_calls;
        REQUIRE_FALSE(session->send(OutputFrame{}, clock.now(), clock));
        REQUIRE(output.ack_close_calls == acknowledgements + 1);
    }
    REQUIRE((output.query_context_calls == queries && output.close_calls == closes &&
             output.contexts.size() == 1));
    REQUIRE(session->close_and_drain(clock));
    REQUIRE(output.contexts.empty());
}

TEST_CASE("effects.output_session.cleanup_maintenance_preserves_binding",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 320s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);
    output.close_status = INPUT_NEXT_STATUS_BUSY;
    retain_old_contexts(*session, output, clock, 1);
    clock.supplied += 1ms;
    observer.publish(clock.now());
    REQUIRE(session->reconcile(clock.now(), clock));
    const auto current = output.contexts.back().context;
    const auto target_queries = output.target_calls;
    const auto opens = output.open_calls;

    output.close_status = INPUT_NEXT_STATUS_REVOKED;
    output.ack_close_status = INPUT_NEXT_STATUS_BUSY;
    const auto context_queries = output.query_context_calls;
    const auto closes = output.close_calls;
    session->maintain_cleanup();
    REQUIRE(output.query_context_calls == context_queries + 1);
    REQUIRE(output.close_calls == closes + 1);
    REQUIRE(output.ack_close_calls == 1);

    output.ack_close_status = INPUT_NEXT_STATUS_OK;
    session->maintain_cleanup();
    REQUIRE(output.query_context_calls == context_queries + 1);
    REQUIRE(output.close_calls == closes + 1);
    REQUIRE(output.ack_close_calls == 2);
    REQUIRE((output.contexts.size() == 1 &&
             FakeOutput::same_context(output.contexts.front().context, current)));
    REQUIRE((session->bound() && output.target_calls == target_queries &&
             output.open_calls == opens && output.triggers.empty() && output.body_calls.empty()));
    REQUIRE(session->send(body_frame(clock.now(), 920), clock.now(), clock));
    REQUIRE(session->close_and_drain(clock));
}

} // namespace
