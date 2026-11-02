#include "fake_output.hpp"
#include "support.hpp"

#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/session.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <inputnext_adapters/led_policy.hpp>

#include <array>
#include <chrono>
#include <memory>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

namespace fallout4_adapter::session_tests {

TEST_CASE("fallout4.session.lifecycle_focus_and_preload_invalidate_ticket",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output);
    Session& session = *session_owner;
    require(!session.ticket(), "no reads before lifecycle");
    const auto ticket = activate(session, now);
    require(!output.triggers.empty(), "fresh gameplay trigger profile");
    PolicyEvent shot{};
    shot.kind = EventKind::shot;
    shot.arrived_at = now + 2ms;
    shot.weapon_identity = 17;
    require(session.enqueue(ticket, shot, shot.arrived_at), "live shot admitted");
    session.control(Control::focus_lost, now + 3ms);
    session.tick(now + 3ms);
    require(output.trigger_clears > 0 && output.body_clears > 0,
        "focus loss withdraws both owned outputs");
    require(!session.enqueue(ticket, shot, now + 3ms), "old epoch event rejected");
    require(!session.publish(ticket, observation(now + 4ms), now + 4ms),
        "old epoch observation rejected");
    session.control(Control::focus_gained, now + 5ms);
    const auto fresh_ticket_opt = session.ticket();
    const auto& fresh_ticket = require_value(fresh_ticket_opt, "focus changes epoch");
    require(fresh_ticket.epoch != ticket.epoch, "focus changes epoch");
    require(session.publish(fresh_ticket, observation(now + 6ms, Mode::vats_selection), now + 6ms),
        "selection observation accepted");
    const auto trigger_count = output.triggers.size();
    session.tick(now + 6ms);
    require(output.triggers.size() == trigger_count, "VATS does not restore weapon triggers");
    require(output.bodies.back().request.fallback_rumble.low_frequency == 0.0F &&
                output.bodies.back().request.fallback_rumble.high_frequency == 0.0F,
        "focus return never replays queued shot");
    session.control(Control::pre_load, now + 7ms);
    session.tick(now + 7ms);
    require(!session.ticket(), "preload closes native reads");
}

TEST_CASE("fallout4.session.audio_only_body_rebound_preserves_led_but_load_withdraws_it",
    "[component][fallout4][session][led]") {
    const auto now = MonoTime{} + 10s;
    FakeOutput output;
    output.configured_capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.configured_capabilities.audio_instance_id = 18U;
    const auto session_owner = std::make_unique<Session>(output);
    Session& session = *session_owner;
    const auto ticket = activate(session, now);

    auto baseline = observation(now + 2ms, Mode::gameplay);
    baseline.led.subject_id = 42U;
    baseline.led.health = inputnext::adapters::LedHealthSample{.current = 80.0F, .maximum = 100.0F};
    REQUIRE(session.publish(ticket, baseline, baseline.sampled_at));
    session.tick(baseline.sampled_at);
    REQUIRE(output.leds.empty());

    auto active = baseline;
    active.sampled_at += 40ms;
    active.led.health->current = 81.0F;
    REQUIRE(session.publish(ticket, active, active.sampled_at));
    session.tick(active.sampled_at);
    REQUIRE(output.leds.size() == 1U);
    REQUIRE(output.led_clears == 0U);

    output.configured_capabilities.audio_instance_id = 19U;
    session.tick(active.sampled_at + 1ms);
    INFO("audio-only body rebound leaves the independent LED lease intact");
    CHECK(output.led_clears == 0U);

    auto after_rebound = active;
    after_rebound.sampled_at += 60ms;
    // Unchanged HP must retain the pre-rebound window rather than need a new HP event.
    REQUIRE(session.publish(ticket, after_rebound, after_rebound.sampled_at));
    session.tick(after_rebound.sampled_at);
    REQUIRE(output.leds.size() >= 2U);

    session.control(Control::pre_load, after_rebound.sampled_at + 1ms);
    session.tick(after_rebound.sampled_at + 1ms);
    INFO("a load boundary withdraws the exact accepted LED owner");
    CHECK(output.led_clears > 0U);
}

TEST_CASE("fallout4.session.session_shutdown_closes_exact_context_while_unadmitted",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output);
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    require(!output.triggers.empty() && !output.bodies.empty(),
        "shutdown fixture owns accepted trigger and body output");
    const auto context = output.contexts.back().first;
    const auto target = output.contexts.back().second;
    const auto trigger_submissions = output.triggers.size();
    const auto trigger_clears = output.trigger_clears;
    const auto body_submissions = output.bodies.size();
    const auto body_clears = output.body_clears;

    session.control(Control::focus_lost, now + 2ms);
    require(session.close_and_drain(),
        "shutdown drains the exact context after admission is withdrawn");
    require(output.close_requests.size() == 1 &&
                output.close_requests.back().context.id == context.id &&
                output.close_requests.back().close_request_id == context.id &&
                output.close_requests.back().context.runtime_id[0] == context.runtime_id[0] &&
                output.close_requests.back().context.runtime_id[1] == context.runtime_id[1],
        "shutdown uses the retained context as its stable close key");
    require(output.close_receipts.size() == 1 && output.acknowledged_close_receipts.size() == 1 &&
                output.close_receipts.back().context.id == context.id &&
                output.close_receipts.back().target.device_key == target.device_key &&
                output.close_receipts.back().target.connection_id == target.connection_id &&
                output.acknowledged_close_receipts.back().close_request_id == context.id,
        "shutdown acknowledges only the exact close receipt");
    require(output.triggers.size() == trigger_submissions &&
                output.trigger_clears == trigger_clears &&
                output.bodies.size() == body_submissions && output.body_clears == body_clears,
        "shutdown closes the context without ordinary trigger/body mutations");
    require(!session.ticket() && !session.publish(ticket, observation(now + 3ms), now + 3ms),
        "closed session rejects a stale lifecycle ticket and observation");
    PolicyEvent event{};
    event.kind = EventKind::shot;
    event.arrived_at = now + 3ms;
    event.weapon_identity = 17;
    require(!session.enqueue(ticket, event, event.arrived_at),
        "closed session rejects gameplay events");
    session.tick(now + 3ms);
    require(session.close_and_drain() && output.close_requests.size() == 1 &&
                output.open_calls == 1,
        "repeated close is idempotent and does not reopen the context");
}

TEST_CASE("fallout4.session.session_shutdown_retries_unknown_close_and_ack",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    output.close_status_script = {
        INPUT_NEXT_STATUS_TIMED_OUT_UNKNOWN,
        INPUT_NEXT_STATUS_PENDING,
        INPUT_NEXT_STATUS_REVOKED,
    };
    output.ack_close_status_script = {INPUT_NEXT_STATUS_BUSY, INPUT_NEXT_STATUS_OK};
    const auto session_owner = std::make_unique<Session>(output);
    Session& session = *session_owner;
    static_cast<void>(activate(session, now));
    require(output.open_calls == 1 && !output.triggers.empty() && !output.bodies.empty(),
        "successful open binds and delivers before shutdown begins");
    const auto context = output.contexts.back().first;
    const auto target = output.contexts.back().second;
    const auto trigger_submissions = output.triggers.size();
    const auto trigger_clears = output.trigger_clears;
    const auto body_submissions = output.bodies.size();
    const auto body_clears = output.body_clears;

    require(!session.close_and_drain(), "unknown close remains pending");
    require(!session.close_and_drain(), "pending close remains pending");
    require(!session.close_and_drain(), "failed receipt acknowledgement remains pending");
    require(session.close_and_drain(), "retained exact receipt eventually drains");
    require(output.close_requests.size() == 3 && output.close_receipts.size() == 1 &&
                output.acknowledged_close_receipts.size() == 2,
        "close retries once for unknown state, then retries only the retained receipt "
        "acknowledgement");
    for (const auto& request : output.close_requests) {
        require(request.struct_size == sizeof(request) && request.reserved == 0 &&
                    same_context(request.context, context) &&
                    request.close_request_id == context.id,
            "all retries keep the same successfully opened context and close key");
    }
    const auto& receipt = output.close_receipts.back();
    require(receipt.struct_size == sizeof(receipt) &&
                receipt.disposition == INPUT_NEXT_EFFECTS_CLOSE_NORMAL &&
                same_context(receipt.context, context) && same_target(receipt.target, target) &&
                receipt.close_request_id == context.id &&
                receipt.publication_revision == context.id + 1,
        "close retains the exact complete context, target, and receipt");
    for (const auto& acknowledged : output.acknowledged_close_receipts) {
        require(same_receipt(acknowledged, receipt),
            "ACK retries preserve every field of the exact close receipt");
    }
    require(output.triggers.size() == trigger_submissions &&
                output.trigger_clears == trigger_clears &&
                output.bodies.size() == body_submissions && output.body_clears == body_clears,
        "close recovery does not replay or mutate ordinary output");
    require(!session.ticket(), "shutdown closes lifecycle admission");
    require(session.close_and_drain() && output.close_requests.size() == 3 &&
                output.open_calls == 1,
        "repeated close after ACK is idempotent and does not reopen the context");
}

TEST_CASE("fallout4.session.failed_open_preserves_context_without_allocation",
    "[component][fallout4][session]") {
    constexpr std::array failed_statuses{
        INPUT_NEXT_STATUS_PENDING,
        INPUT_NEXT_STATUS_BUSY,
        INPUT_NEXT_STATUS_UNAVAILABLE,
    };
    for (const auto status : failed_statuses) {
        FakeOutput output;
        output.open_status = status;
        input_next_effects_open_v1 request{};
        request.struct_size = sizeof(request);
        request.target = output.target;
        input_next_effects_context_v1 context{
            .struct_size = sizeof(context),
            .reserved = 21,
            .runtime_id = {22, 23},
            .id = 24,
        };
        const auto original_context = context;
        require(output.open_context(request, context) == status,
            "non-OK open preserves the runtime status");
        require(same_context(context, original_context) && output.contexts.empty() &&
                    output.next_context == 100,
            "PENDING, BUSY, and failure statuses preserve output and reserve no context");
    }

    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    output.open_status = INPUT_NEXT_STATUS_PENDING;
    const auto session_owner = std::make_unique<Session>(output);
    Session& session = *session_owner;
    static_cast<void>(activate(session, now));
    require(output.open_calls == 2 && output.contexts.empty() && output.triggers.empty() &&
                output.bodies.empty(),
        "Session retries a pending open without publishing output or allocating a fake context");
    require(session.close_and_drain() && output.close_requests.empty() && output.close_calls == 0,
        "shutdown has no cleanup work after an unsuccessful open");
}

TEST_CASE("fallout4.session.controller_rebind_discards_history_and_bounds_inbox",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output);
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    PolicyEvent shot{};
    shot.kind = EventKind::shot;
    shot.arrived_at = now + 2ms;
    shot.weapon_identity = 17;
    require(session.enqueue(ticket, shot, shot.arrived_at), "pre-rebind shot admitted initially");
    ++output.target.device_key;
    session.tick(now + 2ms);
    require(session.publish(ticket, observation(now + 3ms), now + 3ms), "fresh rebind observation");
    session.tick(now + 3ms);
    require(output.bodies.back().request.fallback_rumble.low_frequency == 0.0F &&
                output.bodies.back().request.fallback_rumble.high_frequency == 0.0F,
        "controller rebind discards historical shot");
    for (unsigned index = 0; index < 65; ++index) {
        shot.arrived_at = now + 4ms;
        require(session.enqueue(ticket, shot, shot.arrived_at),
            "bounded inbox accepts current event");
    }
    require(session.diagnostics().overflows == 1, "inbox has bounded overflow policy");
}

TEST_CASE("fallout4.session.default_constructor_matches_default_config_construction",
    "[component][fallout4][session]") {
    FakeOutput output_a;
    FakeOutput output_b;
    const auto session_a = std::make_unique<Session>(output_a);
    const auto config = std::make_unique<Config>();
    const auto session_b = std::make_unique<Session>(output_b, *config);

    const auto diag_a = session_a->diagnostics();
    const auto diag_b = session_b->diagnostics();
    require(diag_a.faulted == diag_b.faulted, "fault status matches");
    require(diag_a.accepted_events == diag_b.accepted_events, "accepted event count matches");
    require(diag_a.rejected_events == diag_b.rejected_events, "rejected event count matches");
    require(diag_a.overflows == diag_b.overflows, "overflow count matches");
    require(!session_a->ticket().has_value() && !session_b->ticket().has_value(),
        "unactivated ticket state matches");

    const auto now = MonoTime{} + 1s;
    const auto ticket_a = activate(*session_a, now);
    const auto ticket_b = activate(*session_b, now);
    require(ticket_a.epoch == ticket_b.epoch, "activated ticket epoch matches");
    require(output_a.triggers.size() == output_b.triggers.size(),
        "initial trigger submission size matches");
    require(output_a.triggers.size() == 1, "initial gameplay trigger published");
    require(output_a.triggers.front().right_trigger.mode ==
                output_b.triggers.front().right_trigger.mode,
        "initial right trigger mode matches");
    require(output_a.triggers.front().right_trigger.strength ==
                output_b.triggers.front().right_trigger.strength,
        "initial right trigger strength matches");

    auto sample = observation(now + 40ms, Mode::gameplay);
    sample.health_ratio = 0.8F;
    sample.aiming = true;
    require(session_a->publish(ticket_a, sample, now + 40ms), "session a accepts sample");
    require(session_b->publish(ticket_b, sample, now + 40ms), "session b accepts sample");

    PolicyEvent shot{};
    shot.kind = EventKind::shot;
    shot.arrived_at = now + 42ms;
    shot.weapon_identity = 42U;
    require(session_a->enqueue(ticket_a, shot, now + 42ms), "session a accepts shot");
    require(session_b->enqueue(ticket_b, shot, now + 42ms), "session b accepts shot");

    session_a->tick(now + 42ms);
    session_b->tick(now + 42ms);

    require(output_a.triggers.size() == output_b.triggers.size(),
        "trigger submission counts match after tick");
    require(output_a.triggers.back().right_trigger.mode ==
                output_b.triggers.back().right_trigger.mode,
        "shot right trigger mode matches");
    require(output_a.triggers.back().right_trigger.strength ==
                output_b.triggers.back().right_trigger.strength,
        "shot right trigger strength matches");
    require(output_a.triggers.back().left_trigger.mode ==
                output_b.triggers.back().left_trigger.mode,
        "shot left trigger mode matches");
    require(output_a.triggers.back().left_trigger.strength ==
                output_b.triggers.back().left_trigger.strength,
        "shot left trigger strength matches");

    require(output_a.bodies.size() == output_b.bodies.size(), "body emission counts match");
    if (!output_a.bodies.empty()) {
        require(output_a.bodies.back().request.fallback_rumble.low_frequency ==
                    output_b.bodies.back().request.fallback_rumble.low_frequency,
            "body fallback low frequency matches");
        require(output_a.bodies.back().request.fallback_rumble.high_frequency ==
                    output_b.bodies.back().request.fallback_rumble.high_frequency,
            "body fallback high frequency matches");
    }

    require(output_a.leds.size() == output_b.leds.size(), "led emission counts match");
    if (!output_a.leds.empty()) {
        require(output_a.leds.back().leds.lightbar_rgb == output_b.leds.back().leds.lightbar_rgb,
            "led rgb matches");
    }
}

} // namespace fallout4_adapter::session_tests
