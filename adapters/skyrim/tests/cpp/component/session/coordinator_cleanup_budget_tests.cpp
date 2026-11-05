#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>
#include <skyrim_adapter/model.hpp>

#include <chrono>
#include <memory>

namespace skyrim_adapter::tests::session {
using namespace std::chrono_literals;

TEST_CASE("skyrim.session.coordinator_early_exit_cleanup_budget", "[component][skyrim][session]") {
    const auto retained_receipt = GENERATE(false, true);
    const auto retry_status = GENERATE(INPUT_NEXT_STATUS_BUSY, INPUT_NEXT_STATUS_UNAVAILABLE);
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(idle());
    fixture.output.close_status = INPUT_NEXT_STATUS_BUSY;
    ++fixture.output.target.connection_id;
    fixture.advance(idle(), 1ms);
    fixture.advance(idle(), 1ms);
    REQUIRE(fixture.output.contexts.size() == 2);

    if (retained_receipt) {
        fixture.output.close_status = INPUT_NEXT_STATUS_REVOKED;
        fixture.output.ack_close_status = INPUT_NEXT_STATUS_BUSY;
        fixture.advance(idle(), 1ms);
        REQUIRE(fixture.output.close_receipts.size() == 1);
    } else {
        fixture.output.close_status = retry_status;
    }
    fixture.output.ack_close_status = retry_status;
    fixture.time += 1ms;
    auto observation = idle();
    bool reconcile_expected{true};
    bool context_query_expected{true};

    SECTION("withdrawal before reconciliation") {
        fixture.coordinator.control(Control::focus_lost, fixture.time);
        reconcile_expected = false;
        context_query_expected = false;
    }
    SECTION("failed reconciliation") {
        fixture.output.target_status = INPUT_NEXT_STATUS_BUSY;
        context_query_expected = false;
    }
    SECTION("withdrawal after reconciliation") {
        fixture.output.on_query = [&] {
            fixture.coordinator.control(Control::focus_lost, fixture.time);
        };
    }
    SECTION("rejected delivery context") {
        observation.menus_clear = false;
    }

    fixture.observe(observation);
    const auto target_queries = fixture.output.target_queries;
    const auto context_queries = fixture.output.query_context_calls;
    const auto closes = fixture.output.close_calls;
    const auto acknowledgements = fixture.output.ack_close_calls;
    const auto opens = fixture.output.open_calls;
    const auto trigger_submissions = fixture.output.submissions.size();
    const auto body_submissions = fixture.output.body_submissions.size();
    CAPTURE(retained_receipt,
        retry_status,
        reconcile_expected,
        context_query_expected,
        observation.menus_clear);
    fixture.coordinator.tick(fixture.time);

    REQUIRE(fixture.output.target_queries == target_queries + (reconcile_expected ? 1U : 0U));
    REQUIRE(fixture.output.query_context_calls ==
            context_queries + (retained_receipt ? 0U : 1U) + (context_query_expected ? 1U : 0U));
    REQUIRE(fixture.output.close_calls == closes + (retained_receipt ? 0U : 1U));
    REQUIRE(fixture.output.ack_close_calls == acknowledgements + (retained_receipt ? 1U : 0U));
    REQUIRE(fixture.output.contexts.size() == 2);
    REQUIRE(fixture.output.open_calls == opens);
    REQUIRE(fixture.output.submissions.size() == trigger_submissions);
    REQUIRE(fixture.output.body_submissions.size() == body_submissions);
    if (retained_receipt) {
        const auto& receipt = fixture.output.close_receipts.front();
        const auto& acknowledgement = fixture.output.acknowledged_close_receipts.back();
        REQUIRE(same_effects_context(acknowledgement.context, receipt.context));
        REQUIRE(same_device_target(acknowledgement.target, receipt.target));
        REQUIRE(acknowledgement.close_request_id == receipt.close_request_id);
        REQUIRE(acknowledgement.publication_revision == receipt.publication_revision);
    }

    fixture.output.on_query = {};
    fixture.output.close_status = INPUT_NEXT_STATUS_REVOKED;
    fixture.output.ack_close_status = INPUT_NEXT_STATUS_OK;
    REQUIRE(fixture.coordinator.close_and_drain());
    REQUIRE(fixture.output.contexts.empty());
}
} // namespace skyrim_adapter::tests::session
