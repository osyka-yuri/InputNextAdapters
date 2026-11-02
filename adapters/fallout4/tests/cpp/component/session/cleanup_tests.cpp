#include "fake_output.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/session.hpp>
#include <input_next_core.h>
#include <input_next_effects.h>

#include <chrono>
#include <cstddef>
#include <memory>

namespace fallout4_adapter::session_tests {
namespace {

void seed_pending_cleanup(FakeOutput& output, Session& session, Ticket ticket, MonoTime now) {
    output.query_context_status = INPUT_NEXT_STATUS_REVOKED;
    output.query_context_state = INPUT_NEXT_EFFECTS_CONTEXT_REVOKED;
    session.tick(now + 2ms);

    output.query_context_status = INPUT_NEXT_STATUS_OK;
    output.query_context_state = INPUT_NEXT_EFFECTS_CONTEXT_OPEN;
    session.tick(now + 3ms);
    require(output.open_calls == 2 && output.contexts.size() == 2,
        "a fresh target qualification opens a replacement after context revocation");

    require(session.publish(ticket, observation(now + 4ms), now + 4ms),
        "replacement context accepts a fresh observation");
    session.tick(now + 4ms);
    require(!output.triggers.empty() && !output.bodies.empty(),
        "replacement binding is live before admission pauses");
    require(output.close_calls == 3 && output.close_receipts.empty(),
        "reconciliation and delivery retain cleanup through transient close failures");
}

struct OutputActivity {
    std::size_t target_calls{}, open_calls{}, trigger_submissions{}, body_submissions{};
    unsigned trigger_clears{}, body_clears{};
};

void require_cleanup_calls_are_isolated(const FakeOutput& output, const OutputActivity& before) {
    require(output.target_calls == before.target_calls && output.open_calls == before.open_calls,
        "paused cleanup does not resolve a target or open a context");
    require(output.triggers.size() == before.trigger_submissions &&
                output.bodies.size() == before.body_submissions,
        "paused cleanup does not submit trigger or body effects");
    require(output.trigger_clears == before.trigger_clears &&
                output.body_clears == before.body_clears,
        "paused cleanup does not repeat the one-time output withdrawal");
}

} // namespace

TEST_CASE("fallout4.session.cleanup_advances_during_focus_loss_with_bounded_calls",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    output.close_status_script = {
        INPUT_NEXT_STATUS_BUSY,
        INPUT_NEXT_STATUS_UNAVAILABLE,
        INPUT_NEXT_STATUS_BUSY,
        INPUT_NEXT_STATUS_BUSY,
        INPUT_NEXT_STATUS_UNAVAILABLE,
        INPUT_NEXT_STATUS_REVOKED,
    };
    output.ack_close_status_script = {
        INPUT_NEXT_STATUS_BUSY,
        INPUT_NEXT_STATUS_UNAVAILABLE,
        INPUT_NEXT_STATUS_OK,
    };
    const auto session_owner = std::make_unique<Session>(output, Config{});
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    const auto retired_context = output.contexts.front().first;
    const auto retired_target = output.contexts.front().second;
    seed_pending_cleanup(output, session, ticket, now);

    const auto target_calls = output.target_calls;
    const auto open_calls = output.open_calls;
    const auto trigger_submissions = output.triggers.size();
    const auto body_submissions = output.bodies.size();
    const auto closes_before_pause = output.close_calls;
    const auto queries_before_pause = output.query_context_calls;
    const auto acks_before_pause = output.ack_close_calls;

    session.control(Control::focus_lost, now + 5ms);
    session.tick(now + 5ms);
    require(output.close_calls == closes_before_pause + 1 &&
                output.query_context_calls == queries_before_pause + 1 &&
                output.ack_close_calls == acks_before_pause,
        "the first paused tick performs one early close-maintenance pass");
    require(output.target_calls == target_calls && output.open_calls == open_calls &&
                output.triggers.size() == trigger_submissions &&
                output.bodies.size() == body_submissions,
        "focus-loss cleanup does not resolve, open, or deliver effects");
    const auto trigger_clears_after_withdrawal = output.trigger_clears;
    const auto body_clears_after_withdrawal = output.body_clears;

    session.tick(now + 6ms);
    require(output.close_calls == closes_before_pause + 2 &&
                output.query_context_calls == queries_before_pause + 2 &&
                output.ack_close_calls == acks_before_pause,
        "a BUSY close remains pending for one retry on the next paused tick");
    session.tick(now + 7ms);
    require(output.close_calls == closes_before_pause + 3 &&
                output.query_context_calls == queries_before_pause + 3 &&
                output.ack_close_calls == acks_before_pause + 1,
        "an UNAVAILABLE close retains the context until an exact receipt is acknowledged");
    session.tick(now + 8ms);
    require(output.ack_close_calls == acks_before_pause + 2,
        "a BUSY ACK retains the same exact receipt");
    session.tick(now + 9ms);
    require(output.ack_close_calls == acks_before_pause + 3,
        "an UNAVAILABLE ACK retains the same exact receipt");
    session.tick(now + 10ms);
    require(output.ack_close_calls == acks_before_pause + 3,
        "one cleanup record is serviced at most once per paused tick");
    require_cleanup_calls_are_isolated(output,
        OutputActivity{.target_calls = target_calls,
            .open_calls = open_calls,
            .trigger_submissions = trigger_submissions,
            .body_submissions = body_submissions,
            .trigger_clears = trigger_clears_after_withdrawal,
            .body_clears = body_clears_after_withdrawal});
    require(output.close_requests.size() == closes_before_pause + 3 &&
                output.close_receipts.size() == 1 &&
                output.acknowledged_close_receipts.size() == acks_before_pause + 3,
        "paused cleanup respects exact close and ACK call budgets");
    for (const auto& close_request : output.close_requests) {
        require(close_request.struct_size == sizeof(close_request) && close_request.reserved == 0 &&
                    same_context(close_request.context, retired_context) &&
                    close_request.close_request_id == retired_context.id,
            "every retry keeps the retired context identity and stable close request ID");
    }
    const auto& receipt = output.close_receipts.back();
    require(receipt.struct_size == sizeof(receipt) &&
                receipt.disposition == INPUT_NEXT_EFFECTS_CLOSE_NORMAL &&
                same_context(receipt.context, retired_context) &&
                same_target(receipt.target, retired_target) &&
                receipt.close_request_id == retired_context.id &&
                receipt.publication_revision == retired_context.id + 1,
        "cleanup retains the complete receipt for the exact retired context and target");
    for (const auto& acknowledged : output.acknowledged_close_receipts) {
        require(same_receipt(acknowledged, receipt),
            "all ACK retries carry the complete exact close receipt");
    }
    require(output.contexts.size() == 1 &&
                !same_context(output.contexts.front().first, retired_context),
        "only successful ACK retires the old context");

    session.control(Control::focus_gained, now + 11ms);
    const auto recovery_ticket = session.ticket();
    require(recovery_ticket.has_value(), "focus recovery issues a fresh session ticket");
    if (recovery_ticket) {
        require(session.publish(*recovery_ticket, observation(now + 12ms), now + 12ms),
            "a fresh focused observation is admitted after cleanup recovery");
        session.tick(now + 12ms);
        require(output.target_calls == target_calls + 1 && output.open_calls == open_calls,
            "normal target reconciliation resumes only after focus returns");
        require(output.triggers.size() > trigger_submissions &&
                    output.bodies.size() > body_submissions,
            "fresh focused data resumes ordinary output delivery");
    }
    require(session.close_and_drain(), "focus fixture drains its remaining live context");
}

TEST_CASE("fallout4.session.cleanup_advances_while_observation_is_expired",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    output.close_status_script = {
        INPUT_NEXT_STATUS_BUSY,
        INPUT_NEXT_STATUS_UNAVAILABLE,
        INPUT_NEXT_STATUS_BUSY,
        INPUT_NEXT_STATUS_BUSY,
        INPUT_NEXT_STATUS_UNAVAILABLE,
        INPUT_NEXT_STATUS_REVOKED,
    };
    output.ack_close_status_script = {
        INPUT_NEXT_STATUS_BUSY,
        INPUT_NEXT_STATUS_UNAVAILABLE,
        INPUT_NEXT_STATUS_OK,
    };
    const auto session_owner = std::make_unique<Session>(output, Config{});
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    const auto retired_context = output.contexts.front().first;
    const auto retired_target = output.contexts.front().second;
    seed_pending_cleanup(output, session, ticket, now);

    const auto target_calls = output.target_calls;
    const auto open_calls = output.open_calls;
    const auto trigger_submissions = output.triggers.size();
    const auto body_submissions = output.bodies.size();
    const auto closes_before_expiry = output.close_calls;
    const auto queries_before_expiry = output.query_context_calls;
    const auto acks_before_expiry = output.ack_close_calls;

    session.tick(now + 161ms);
    require(output.close_calls == closes_before_expiry + 1 &&
                output.query_context_calls == queries_before_expiry + 1 &&
                output.ack_close_calls == acks_before_expiry,
        "the expired-observation branch advances one early cleanup pass");
    const auto trigger_clears_after_withdrawal = output.trigger_clears;
    const auto body_clears_after_withdrawal = output.body_clears;

    session.tick(now + 162ms);
    require(output.close_calls == closes_before_expiry + 2 &&
                output.query_context_calls == queries_before_expiry + 2,
        "an UNAVAILABLE cleanup close is retained while observation admission stays expired");
    session.tick(now + 163ms);
    require(output.close_calls == closes_before_expiry + 3 &&
                output.query_context_calls == queries_before_expiry + 3 &&
                output.ack_close_calls == acks_before_expiry + 1,
        "cleanup retries one exact close and begins ACK after a valid receipt");
    session.tick(now + 164ms);
    session.tick(now + 165ms);
    require(output.ack_close_calls == acks_before_expiry + 3,
        "BUSY and UNAVAILABLE ACK results retain the receipt until recovery");
    require_cleanup_calls_are_isolated(output,
        OutputActivity{.target_calls = target_calls,
            .open_calls = open_calls,
            .trigger_submissions = trigger_submissions,
            .body_submissions = body_submissions,
            .trigger_clears = trigger_clears_after_withdrawal,
            .body_clears = body_clears_after_withdrawal});
    require(output.close_requests.size() == closes_before_expiry + 3 &&
                output.close_receipts.size() == 1 &&
                output.acknowledged_close_receipts.size() == acks_before_expiry + 3,
        "expired cleanup uses one close receipt and bounded retry calls");
    for (const auto& close_request : output.close_requests) {
        require(close_request.struct_size == sizeof(close_request) && close_request.reserved == 0 &&
                    same_context(close_request.context, retired_context) &&
                    close_request.close_request_id == retired_context.id,
            "expired cleanup keeps one exact context and close request ID");
    }
    const auto& receipt = output.close_receipts.back();
    require(same_context(receipt.context, retired_context) &&
                same_target(receipt.target, retired_target),
        "expired cleanup retains the exact retired target in its close receipt");
    for (const auto& acknowledged : output.acknowledged_close_receipts) {
        require(same_receipt(acknowledged, receipt),
            "expired cleanup ACK retries preserve every receipt field");
    }

    require(session.publish(ticket, observation(now + 166ms), now + 166ms),
        "a fresh observation renews admission after the stale interval");
    session.tick(now + 166ms);
    require(output.target_calls == target_calls + 1 && output.open_calls == open_calls,
        "fresh observation resumes reconciliation without reopening the surviving context");
    require(output.triggers.size() > trigger_submissions && output.bodies.size() > body_submissions,
        "fresh observation resumes output delivery after cleanup completes");
    require(session.close_and_drain(), "expired-observation fixture drains its live context");
}

} // namespace fallout4_adapter::session_tests
