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
#include <utility>

namespace skyrim_adapter::tests::session {
using namespace std::chrono_literals;

TEST_CASE("skyrim.session.coordinator_shutdown_closes_exact_context_while_restricted",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    const auto ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto capture = require_capture(fixture.coordinator.begin_read(ticket, fixture.time));
    const auto context = fixture.output.contexts.back().first;
    const auto target = fixture.output.contexts.back().second;
    const auto trigger_submissions = fixture.output.submissions.size();
    const auto trigger_clears = fixture.output.clears.size();
    const auto body_submissions = fixture.output.body_submissions.size();
    const auto body_clears = fixture.output.body_clears.size();

    fixture.coordinator.control(Control::focus_lost, fixture.time);
    INFO("explicit close drains a restricted active context");
    REQUIRE(fixture.coordinator.close_and_drain());
    INFO("shutdown closes the exact retained context with its reserved close key");
    REQUIRE((
        fixture.output.close_requests.size() == 1 &&
        session_test::same_effects_context(fixture.output.close_requests.back().context, context) &&
        fixture.output.close_requests.back().close_request_id == context.id));
    INFO("shutdown acknowledges only the exact context and target receipt");
    REQUIRE((
        fixture.output.close_receipts.size() == 1 &&
        fixture.output.acknowledged_close_receipts.size() == 1 &&
        session_test::same_effects_context(fixture.output.close_receipts.back().context, context) &&
        session_test::same_device_target(fixture.output.close_receipts.back().target, target) &&
        fixture.output.acknowledged_close_receipts.back().close_request_id == context.id));
    INFO("shutdown uses reserved context cleanup without ordinary trigger or body mutations");
    REQUIRE((fixture.output.submissions.size() == trigger_submissions &&
             fixture.output.clears.size() == trigger_clears &&
             fixture.output.body_submissions.size() == body_submissions &&
             fixture.output.body_clears.size() == body_clears));

    const auto target_queries = fixture.output.target_queries;
    const auto opens = fixture.output.open_calls;
    fixture.coordinator.control(Control::focus_gained, fixture.time + 1ms);
    fixture.coordinator.publish(capture, casting(), {}, fixture.time + 1ms);
    INFO("closed coordinator rejects tickets and stale captures");
    REQUIRE((!fixture.coordinator.native_ticket() &&
             !fixture.coordinator.begin_read(ticket, fixture.time + 1ms)));
    const auto event = raw_event(EventKind::voice_fire, 0x14, fixture.time + 1ms);
    INFO("closed coordinator rejects native events");
    REQUIRE(!fixture.coordinator.enqueue(ticket, event));
    fixture.coordinator.tick(fixture.time + 1ms);
    INFO("repeated shutdown is idempotent and never reopens output");
    REQUIRE(
        (fixture.coordinator.close_and_drain() && fixture.output.close_requests.size() == 1 &&
            fixture.output.target_queries == target_queries && fixture.output.open_calls == opens));
}

TEST_CASE("skyrim.session.coordinator_shutdown_retries_unknown_close_and_ack_after_open",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.observe(idle());
    fixture.coordinator.tick(fixture.time);
    INFO("successful open did not establish exactly one active context");
    REQUIRE((fixture.output.open_calls == 1 && fixture.output.contexts.size() == 1));

    fixture.output.query_context_status = INPUT_NEXT_STATUS_PENDING;
    fixture.output.query_context_state = INPUT_NEXT_EFFECTS_CONTEXT_ATTACHING;
    fixture.time += 1ms;
    fixture.observe(idle());
    fixture.coordinator.tick(fixture.time);
    INFO("PENDING query dropped or replaced the successfully opened context");
    REQUIRE((fixture.output.query_context_calls == 1 && fixture.output.open_calls == 1 &&
             fixture.output.contexts.size() == 1 && fixture.output.close_requests.empty()));

    fixture.output.query_context_status = INPUT_NEXT_STATUS_BUSY;
    fixture.time += 1ms;
    fixture.observe(idle());
    fixture.coordinator.tick(fixture.time);
    INFO("BUSY query dropped or replaced the successfully opened context");
    REQUIRE((fixture.output.query_context_calls == 2 && fixture.output.open_calls == 1 &&
             fixture.output.contexts.size() == 1 && fixture.output.close_requests.empty()));

    fixture.output.query_context_status = INPUT_NEXT_STATUS_OK;
    fixture.output.query_context_state = INPUT_NEXT_EFFECTS_CONTEXT_OPEN;
    fixture.output.close_status_script = {
        INPUT_NEXT_STATUS_TIMED_OUT_UNKNOWN,
        INPUT_NEXT_STATUS_PENDING,
        INPUT_NEXT_STATUS_REVOKED,
    };
    fixture.output.ack_close_status_script = {INPUT_NEXT_STATUS_BUSY, INPUT_NEXT_STATUS_OK};
    const auto context = fixture.output.contexts.back().first;
    const auto target = fixture.output.contexts.back().second;
    INFO("close fixture replayed output before the exact opened context was drained");
    REQUIRE((fixture.output.submissions.empty() && fixture.output.body_submissions.empty()));

    INFO("unknown close remains pending");
    REQUIRE(!fixture.coordinator.close_and_drain());
    INFO("pending close remains pending");
    REQUIRE(!fixture.coordinator.close_and_drain());
    INFO("failed exact receipt acknowledgement remains pending");
    REQUIRE(!fixture.coordinator.close_and_drain());
    INFO("retained exact receipt eventually drains");
    REQUIRE(fixture.coordinator.close_and_drain());
    INFO("unknown close retries until one receipt, then retries only its acknowledgement");
    REQUIRE(
        (fixture.output.close_requests.size() == 3 && fixture.output.close_receipts.size() == 1 &&
            fixture.output.acknowledged_close_receipts.size() == 2));
    for (const auto& request : fixture.output.close_requests) {
        INFO("every retry retains the opened context and its stable close key");
        REQUIRE((session_test::same_effects_context(request.context, context) &&
                 request.close_request_id == context.id));
    }
    for (const auto& receipt : fixture.output.acknowledged_close_receipts) {
        INFO("acknowledgement retries preserve the exact close receipt");
        REQUIRE((session_test::same_effects_context(receipt.context, context) &&
                 session_test::same_device_target(receipt.target, target) &&
                 receipt.close_request_id == context.id));
    }
    INFO("an opened context is closed without submitting or replaying output");
    REQUIRE((fixture.output.submissions.empty() && fixture.output.clears.empty() &&
             fixture.output.body_submissions.empty() && fixture.output.body_clears.empty()));
}

TEST_CASE("skyrim.session.failed_open_does_not_allocate_a_context",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.output.open_status = INPUT_NEXT_STATUS_PENDING;
    fixture.observe(idle());
    fixture.coordinator.tick(fixture.time);

    INFO("PENDING open allocated or advanced a context identity");
    REQUIRE((fixture.output.open_calls == 1 && fixture.output.contexts.empty() &&
             fixture.output.next_context == 100 && fixture.output.close_requests.empty()));
    INFO("failed open emitted a normal effect call");
    REQUIRE((fixture.output.submissions.empty() && fixture.output.clears.empty() &&
             fixture.output.body_submissions.empty() && fixture.output.body_clears.empty()));

    fixture.output.open_status = INPUT_NEXT_STATUS_OK;
    fixture.time += 1ms;
    fixture.observe(idle());
    fixture.coordinator.tick(fixture.time);
    INFO("successful open did not allocate the first context identity exactly once");
    REQUIRE(
        (fixture.output.open_calls == 2 && fixture.output.contexts.size() == 1 &&
            fixture.output.contexts.front().first.id == 100 && fixture.output.next_context == 101));
}

TEST_CASE("skyrim.session.fake_output_commits_only_data_status_outputs",
    "[component][skyrim][session]") {
    FakeOutput output;
    input_next_device_selector_v1 selector{};
    selector.struct_size = sizeof(selector);
    selector.target.struct_size = sizeof(selector.target);
    selector.kind = INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT;
    selector.xinput_slot = 0;
    input_next_device_target_v1 target{};
    target.struct_size = sizeof(target);
    target.device_key = 901;
    input_next_effects_capabilities_v1 capabilities{};
    capabilities.struct_size = sizeof(capabilities);
    capabilities.supported_channels = 0x40;

    output.target_status = INPUT_NEXT_STATUS_BUSY;
    INFO("BUSY target resolution committed output arguments");
    REQUIRE(output.resolve_target(selector, target, capabilities) == INPUT_NEXT_STATUS_BUSY);
    REQUIRE((target.device_key == 901 && capabilities.supported_channels == 0x40));

    output.target_status = INPUT_NEXT_STATUS_OK;
    REQUIRE(output.resolve_target(selector, target, capabilities) == INPUT_NEXT_STATUS_OK);
    input_next_effects_open_v1 request{};
    request.struct_size = sizeof(request);
    request.target = target;
    input_next_effects_context_v1 context{};
    context.struct_size = sizeof(context);
    context.runtime_id[0] = 7;
    context.id = 902;
    const auto original_context = context;
    output.open_status = INPUT_NEXT_STATUS_PENDING;
    INFO("PENDING context open committed output or allocated a context");
    REQUIRE(output.open_context(request, context) == INPUT_NEXT_STATUS_PENDING);
    REQUIRE((same_effects_context(context, original_context) && output.contexts.empty() &&
             output.next_context == 100));

    output.open_status = INPUT_NEXT_STATUS_OK;
    REQUIRE(output.open_context(request, context) == INPUT_NEXT_STATUS_OK);
    input_next_effects_context_info_v1 info{};
    info.struct_size = sizeof(info);
    info.state = 0x40;
    info.context = original_context;
    output.query_context_status = INPUT_NEXT_STATUS_BUSY;
    const auto original_info = info;
    INFO("BUSY context query committed output arguments");
    REQUIRE(output.query_context(context, info) == INPUT_NEXT_STATUS_BUSY);
    REQUIRE((info.struct_size == original_info.struct_size && info.state == original_info.state &&
             same_effects_context(info.context, original_info.context)));

    output.query_context_status = INPUT_NEXT_STATUS_REVOKED;
    INFO("REVOKED context query omitted its available context information");
    REQUIRE(output.query_context(context, info) == INPUT_NEXT_STATUS_REVOKED);
    REQUIRE(
        (info.state == output.query_context_state && same_effects_context(info.context, context)));
}
} // namespace skyrim_adapter::tests::session
