#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "output_session/fake_output.hpp"
#include "output_session/support.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace {
using namespace std::chrono_literals;
using namespace inputnext::adapters;
using namespace inputnext::test::output_session;

TEST_CASE("effects.output_session.unknown_close_retry", "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 125s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);
    auto frame = body_frame(clock.now(), 908);
    frame.left = trigger();
    INFO("cleanup receipt setup failed");
    REQUIRE(session->send(frame, clock.now(), clock));
    const auto expected_context = output.contexts.back().context;

    output.close_status = INPUT_NEXT_STATUS_TIMED_OUT_UNKNOWN;
    observer.ready = false;
    clock.supplied += 1ms;
    INFO("expired admission allowed a frame");
    REQUIRE_FALSE(session->send(OutputFrame{}, clock.now(), clock));
    INFO("unknown close was acknowledged or fell back to ordinary channel cleanup");
    REQUIRE(
        (output.close_calls == 1 && output.ack_close_calls == 0 && output.trigger_clears.empty() &&
            output.body_clears.empty() && !session->bound() && !session->has_body_owner()));
    const auto close_request = output.closes.back();
    INFO("close request did not use the exact context ID as its idempotency key");
    REQUIRE((close_request.close_request_id == close_request.context.id &&
             close_request.context.id == expected_context.id));

    output.close_status = INPUT_NEXT_STATUS_REVOKED;
    output.ack_close_status = INPUT_NEXT_STATUS_BUSY;
    INFO("BUSY close acknowledgement was reported as drained");
    REQUIRE_FALSE(session->close_and_drain(clock));
    INFO("receipt retry did not retain and acknowledge the exact close result");
    REQUIRE((output.close_calls == 2 && output.ack_close_calls == 1 &&
             output.acknowledgements.front().close_request_id == close_request.close_request_id));
    const auto retained_receipt = output.acknowledgements.front();

    output.close_status = INPUT_NEXT_STATUS_UNAVAILABLE;
    output.ack_close_status = INPUT_NEXT_STATUS_OK;
    INFO("retained close receipt did not drain after ACK recovery");
    REQUIRE(session->close_and_drain(clock));
    INFO("ACK recovery repeated close or changed the receipt identity");
    REQUIRE((output.close_calls == 2 && output.ack_close_calls == 2 &&
             same_close_receipt(output.acknowledgements.back(), retained_receipt)));
}

TEST_CASE("effects.output_session.close_receipt_dispositions",
    "[component][effects][output_session]") {
    const auto begin_close = [](FakeOutput& output, FakeObserver& observer, TickClock& clock) {
        auto session = std::make_unique<OutputSession>(output, observer, 0);
        bind_with_fresh_sample(*session, observer, clock);
        auto frame = body_frame(clock.now(), 908);
        frame.left = trigger();
        INFO("disposition receipt setup failed");
        REQUIRE(session->send(frame, clock.now(), clock));
        observer.ready = false;
        clock.supplied += 1ms;
        INFO("restricted output remained admitted");
        REQUIRE_FALSE(session->send(OutputFrame{}, clock.now(), clock));
        return session;
    };

    {
        FakeOutput output;
        output.close_disposition = INPUT_NEXT_EFFECTS_CLOSE_CANCELLED_BEFORE_CREATION;
        FakeObserver observer;
        TickClock clock{.supplied = MonoTime{} + 220s, .use_steady_clock = false};
        const auto session = begin_close(output, observer, clock);
        INFO("valid cancelled-before-creation receipt was not acknowledged and released");
        REQUIRE(
            (output.close_calls == 1 && output.ack_close_calls == 1 && output.contexts.empty() &&
                output.acknowledgements.front().disposition ==
                    INPUT_NEXT_EFFECTS_CLOSE_CANCELLED_BEFORE_CREATION &&
                output.acknowledgements.front().publication_revision == 0 &&
                session->close_and_drain(clock)));
    }

    struct InvalidReceiptCase {
        std::uint32_t disposition;
        CloseReceiptFault fault;
        const char* description;
    };

    constexpr std::array invalid_receipts{
        InvalidReceiptCase{
            .disposition = INPUT_NEXT_EFFECTS_CLOSE_CANCELLED_BEFORE_CREATION,
            .fault = CloseReceiptFault::nonzero_revision,
            .description = "cancelled receipt with a nonzero revision",
        },
        InvalidReceiptCase{
            .disposition = INPUT_NEXT_EFFECTS_CLOSE_CANCELLED_BEFORE_CREATION,
            .fault = CloseReceiptFault::context,
            .description = "cancelled receipt with a mismatched context",
        },
        InvalidReceiptCase{
            .disposition = INPUT_NEXT_EFFECTS_CLOSE_CANCELLED_BEFORE_CREATION,
            .fault = CloseReceiptFault::target,
            .description = "cancelled receipt with a mismatched target",
        },
        InvalidReceiptCase{
            .disposition = INPUT_NEXT_EFFECTS_CLOSE_CANCELLED_BEFORE_CREATION,
            .fault = CloseReceiptFault::close_request_id,
            .description = "cancelled receipt with a mismatched close key",
        },
        InvalidReceiptCase{
            .disposition = INPUT_NEXT_EFFECTS_CLOSE_NORMAL,
            .fault = CloseReceiptFault::zero_revision,
            .description = "normal receipt with a zero revision",
        },
        InvalidReceiptCase{
            .disposition = INPUT_NEXT_EFFECTS_CLOSE_CONNECTION_RETIRED,
            .fault = CloseReceiptFault::zero_revision,
            .description = "retired receipt with a zero revision",
        },
        InvalidReceiptCase{
            .disposition = INPUT_NEXT_EFFECTS_CLOSE_NORMAL,
            .fault = CloseReceiptFault::unknown_disposition,
            .description = "receipt with an unknown disposition",
        },
        InvalidReceiptCase{
            .disposition = INPUT_NEXT_EFFECTS_CLOSE_NORMAL,
            .fault = CloseReceiptFault::size,
            .description = "receipt with a malformed size",
        },
    };

    std::uint32_t clock_offset = 230;
    for (const auto& invalid : invalid_receipts) {
        FakeOutput output;
        output.close_disposition = invalid.disposition;
        output.close_receipt_fault = invalid.fault;
        FakeObserver observer;
        TickClock clock{.supplied = MonoTime{} + std::chrono::seconds{clock_offset++},
            .use_steady_clock = false};
        const auto session = begin_close(output, observer, clock);
        INFO("malformed close receipt was acknowledged");
        REQUIRE((output.close_calls == 1 && output.ack_close_calls == 0));
        INFO(invalid.description);
        REQUIRE((!session->close_and_drain(clock) && output.close_calls == 2 &&
                 output.ack_close_calls == 0));

        output.close_receipt_fault = CloseReceiptFault::none;
        INFO("invalid close receipt did not retain cleanup until a genuine receipt arrived");
        REQUIRE((session->close_and_drain(clock) && output.close_calls == 3 &&
                 output.ack_close_calls == 1 && output.contexts.empty()));
        const auto& acknowledged = output.acknowledgements.back();
        INFO(
            "recovered close receipt did not preserve disposition-specific revision qualification");
        REQUIRE((acknowledged.disposition == invalid.disposition &&
                 (invalid.disposition == INPUT_NEXT_EFFECTS_CLOSE_CANCELLED_BEFORE_CREATION
                         ? acknowledged.publication_revision == 0
                         : acknowledged.publication_revision != 0)));
    }
}

TEST_CASE("effects.output_session.cleanup_closes_after_pending_query",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 128s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);
    const auto expected_context = output.contexts.back().context;

    output.query_context_status = INPUT_NEXT_STATUS_PENDING;
    INFO("pending query data blocked the idempotent exact close");
    REQUIRE(session->close_and_drain(clock));
    REQUIRE((output.close_calls == 1 && output.ack_close_calls == 1 && output.contexts.empty() &&
             FakeOutput::same_context(output.closes.back().context, expected_context) &&
             output.closes.back().close_request_id == expected_context.id));
}

TEST_CASE("effects.output_session.cleanup_capacity", "[component][effects][output_session]") {
    constexpr std::size_t cleanup_capacity = 32;
    FakeOutput output;
    output.close_status = INPUT_NEXT_STATUS_PENDING;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 126s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    for (std::size_t index = 0; index < cleanup_capacity; ++index) {
        ++output.target.connection_id;
        clock.supplied += 1ms;
        INFO("target rebind unexpectedly reused an old observation");
        REQUIRE_FALSE(session->reconcile(std::nullopt, clock));
    }
    const auto opens_at_capacity = output.open_calls;
    ++output.target.connection_id;
    clock.supplied += 1ms;
    INFO("full cleanup capacity accepted a new target binding");
    REQUIRE_FALSE(session->reconcile(std::nullopt, clock));
    INFO("cleanup saturation discarded an owner or opened a replacement before capacity was freed");
    REQUIRE((output.open_calls == opens_at_capacity && output.close_calls == cleanup_capacity &&
             output.ack_close_calls == 0 && output.contexts.size() == cleanup_capacity + 1));

    output.close_status = INPUT_NEXT_STATUS_REVOKED;
    INFO("drain freed cleanup capacity and closed the retained binding in one pass");
    REQUIRE_FALSE(session->close_and_drain(clock));
    INFO("bounded cleanup did not retry the retained binding after prior receipts drained");
    REQUIRE((session->close_and_drain(clock) && output.ack_close_calls == cleanup_capacity + 1));
}

TEST_CASE("effects.output_session.persistent_runtime_unavailability",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 200s, .use_steady_clock = false};

    bind_with_fresh_sample(*session, observer, clock);
    auto frame = body_frame(clock.now(), 909);
    frame.left = trigger();
    REQUIRE(session->send(frame, clock.now(), clock));
    REQUIRE(session->bound());
    REQUIRE(!output.contexts.empty());
    const auto expected_context = output.contexts.back().context;

    output.query_context_status = INPUT_NEXT_STATUS_UNAVAILABLE;
    output.close_status = INPUT_NEXT_STATUS_UNAVAILABLE;

    constexpr std::size_t drain_attempts = 5;
    for (std::size_t attempt = 0; attempt < drain_attempts; ++attempt) {
        clock.supplied += 10ms;
        INFO("close_and_drain succeeded while runtime was persistently unavailable");
        REQUIRE_FALSE(session->close_and_drain(clock));
    }

    INFO("close receipt was fabricated or acknowledged during runtime unavailability");
    REQUIRE((output.ack_close_calls == 0 && output.acknowledgements.empty()));
    INFO("close requests did not retain the original context identity");
    REQUIRE(!output.closes.empty());
    for (const auto& close_req : output.closes) {
        REQUIRE(close_req.context.id == expected_context.id);
        REQUIRE(close_req.close_request_id == expected_context.id);
    }
    REQUIRE(!output.contexts.empty());
    REQUIRE(output.contexts.back().context.id == expected_context.id);

    output.query_context_status = INPUT_NEXT_STATUS_OK;
    output.close_status = INPUT_NEXT_STATUS_REVOKED;
    output.ack_close_status = INPUT_NEXT_STATUS_OK;

    clock.supplied += 10ms;
    INFO("close_and_drain did not succeed after runtime recovered");
    REQUIRE(session->close_and_drain(clock));
    INFO("recovered drain did not acknowledge the exact context");
    REQUIRE((output.ack_close_calls == 1 && !output.acknowledgements.empty() &&
             output.acknowledgements.back().context.id == expected_context.id &&
             output.contexts.empty()));
}

TEST_CASE("effects.output_session.retained_receipt_during_ack_failure",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 210s, .use_steady_clock = false};

    bind_with_fresh_sample(*session, observer, clock);
    auto frame = body_frame(clock.now(), 910);
    frame.left = trigger();
    REQUIRE(session->send(frame, clock.now(), clock));
    const auto expected_context = output.contexts.back().context;

    output.close_status = INPUT_NEXT_STATUS_REVOKED;
    output.ack_close_status = INPUT_NEXT_STATUS_UNAVAILABLE;

    clock.supplied += 10ms;
    INFO("close_and_drain succeeded despite acknowledgement unavailability");
    REQUIRE_FALSE(session->close_and_drain(clock));

    INFO("valid close receipt was not obtained from close_context");
    REQUIRE((output.close_calls == 1 && output.ack_close_calls == 1));
    const auto retained_receipt = output.acknowledgements.front();
    REQUIRE(retained_receipt.context.id == expected_context.id);

    constexpr std::size_t extra_ack_attempts = 4;
    for (std::size_t attempt = 0; attempt < extra_ack_attempts; ++attempt) {
        clock.supplied += 10ms;
        INFO("close_and_drain succeeded while ACK remained unavailable");
        REQUIRE_FALSE(session->close_and_drain(clock));
    }

    INFO("unnecessary close request was issued while a valid receipt was retained");
    REQUIRE(output.close_calls == 1);
    REQUIRE(output.ack_close_calls == 1 + extra_ack_attempts);
    for (const auto& ack : output.acknowledgements) {
        REQUIRE(same_close_receipt(ack, retained_receipt));
    }

    output.ack_close_status = INPUT_NEXT_STATUS_OK;
    output.close_status = INPUT_NEXT_STATUS_UNAVAILABLE;

    clock.supplied += 10ms;
    INFO("drain did not complete after ACK recovery");
    REQUIRE(session->close_and_drain(clock));
    INFO("recovery issued a duplicate close or changed receipt identity");
    REQUIRE((output.close_calls == 1 && output.ack_close_calls == 2 + extra_ack_attempts &&
             same_close_receipt(output.acknowledgements.back(), retained_receipt) &&
             output.contexts.empty()));
}

TEST_CASE("effects.output_session.no_outstanding_context", "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 220s, .use_steady_clock = false};

    REQUIRE(!session->bound());
    INFO("close_and_drain failed when no binding or cleanup existed");
    REQUIRE(session->close_and_drain(clock));

    INFO("unnecessary runtime calls were made when draining an unbound session");
    REQUIRE((output.close_calls == 0 && output.ack_close_calls == 0 &&
             output.query_context_calls == 0 && output.target_calls == 0 &&
             output.open_calls == 0 && output.closes.empty() && output.acknowledgements.empty()));

    clock.supplied += 10ms;
    REQUIRE(session->close_and_drain(clock));
    REQUIRE((output.close_calls == 0 && output.ack_close_calls == 0));
}

} // namespace
