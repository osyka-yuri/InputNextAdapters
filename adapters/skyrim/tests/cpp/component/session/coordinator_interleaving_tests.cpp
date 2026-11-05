#include "interleaving_support.hpp"
#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <algorithm>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <input_next_effects.h>
#include <memory>
#include <optional>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>
#include <span>
#include <thread>

namespace skyrim_adapter::tests::session {
using namespace std::chrono_literals;
using namespace interleaving_support;

TEST_CASE("skyrim.session.coordinator_ingress_progress_during_blocked_query",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    fixture.time += 1ms;
    const auto old_ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto trigger_count_before = fixture.output.submissions.size();
    BlockingCall blocked_query;
    fixture.output.on_query = [&] {
        blocked_query.pause_first();
    };

    std::thread delivery{[&] { fixture.coordinator.tick(fixture.time); }};
    const bool query_entered = blocked_query.wait_until_entered();

    struct Result {
        bool ticket_available{};
        bool enqueue_accepted{};
        bool capture_started{};
        bool published{};
        bool epoch_advanced{};
        std::uint64_t overflows{};
    } result;

    Completion ingress_complete;
    std::thread ingress{[&] {
        const auto ticket = fixture.coordinator.native_ticket();
        result.ticket_available = ticket.has_value() && ticket->epoch == old_ticket.epoch;
        if (ticket) {
            result.enqueue_accepted = fixture.coordinator.enqueue(*ticket,
                raw_event(EventKind::voice_fire, 0x14, fixture.time));
            const auto capture = fixture.coordinator.begin_read(*ticket, fixture.time);
            result.capture_started = capture.has_value();
            if (capture) {
                const auto action = voice_action(fixture.time);
                fixture.coordinator.publish(*capture,
                    casting(),
                    std::span{&action, 1},
                    fixture.time);
                result.published = true;
            }
        }
        fixture.coordinator.control(Control::menu_suppressed, fixture.time);
        const auto after = fixture.coordinator.native_ticket();
        result.epoch_advanced = after && after->epoch != old_ticket.epoch;
        result.overflows = fixture.coordinator.diagnostics().inbox_overflows;
        ingress_complete.signal();
    }};
    InterleavingGuard const guard{&blocked_query, &ingress, &delivery};
    const bool ingress_progressed_while_blocked = ingress_complete.wait_for(250ms);
    blocked_query.release();
    ingress.join();
    delivery.join();

    fixture.output.on_query = {};
    fixture.coordinator.tick(fixture.time);
    INFO("target query did not enter the blocking output hook");
    REQUIRE(query_entered);
    INFO("ingress waited for a blocked target query");
    REQUIRE(ingress_progressed_while_blocked);
    INFO("one of native_ticket, enqueue, begin_read, publish, control, or diagnostics failed to "
         "progress");
    REQUIRE((result.ticket_available && result.enqueue_accepted && result.capture_started &&
             result.published && result.epoch_advanced && result.overflows == 0));
    INFO("an action published before the menu fence reached trigger output");
    REQUIRE(fixture.output.submissions.size() == trigger_count_before);
}

TEST_CASE("skyrim.session.coordinator_frozen_context_does_not_rejuvenate_and_keeps_late_action",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 920);
    fixture.time = std::chrono::steady_clock::now();
    fixture.settle(casting());
    const auto old_lease = fixture.output.active_body_lease;
    fixture.time = std::chrono::steady_clock::now();
    const auto old_action = voice_action(fixture.time);
    const auto old_sample_at = fixture.time;
    fixture.observe(casting(), std::span{&old_action, 1});
    const auto trigger_count = fixture.output.submissions.size();

    BlockingCall blocked_submit;
    fixture.output.on_body_submit = [&] {
        blocked_submit.pause_first();
    };
    std::thread delivery{[&] { fixture.coordinator.tick(); }};
    const bool submit_entered = blocked_submit.wait_until_entered();

    // Publish a new live ingress sample while the output call still owns its frozen old context.
    // Leave enough of the 150ms event window for the stale delivery to return and recovery to run.
    static_cast<void>(blocked_submit.wait_until(old_sample_at + 80ms));
    fixture.time = std::chrono::steady_clock::now();
    const auto fresh_sample_at = fixture.time;
    const auto late_action = voice_action(fresh_sample_at);
    Completion ingress_complete;
    std::thread ingress{[&] {
        const auto ticket = fixture.coordinator.native_ticket();
        std::optional<NativeCapture> capture;
        if (ticket) {
            capture = fixture.coordinator.begin_read(*ticket, fixture.time);
        }
        if (capture) {
            fixture.coordinator.publish(*capture, idle(), std::span{&late_action, 1}, fixture.time);
        }
        ingress_complete.signal();
    }};
    InterleavingGuard const guard{&blocked_submit, &ingress, &delivery};
    const bool ingress_progressed_while_blocked = ingress_complete.wait_for(200ms);
    bool held_until_old_context_expired{};
    if (ingress_progressed_while_blocked) {
        held_until_old_context_expired = !blocked_submit.wait_until(old_sample_at + 151ms);
    }
    blocked_submit.release();
    ingress.join();
    delivery.join();

    const auto accepted_request = fixture.output.body_submissions.back().request;
    const auto cleared_old_delivery =
        std::ranges::any_of(fixture.output.body_clears, [&](const BodyClear& clear) {
            return clear.lease_id == accepted_request.lease_id &&
                   same_effects_context(clear.context, accepted_request.context) &&
                   same_device_target(clear.target, accepted_request.target) &&
                   clear.audio_instance_id == accepted_request.audio_instance_id &&
                   clear.sequence == accepted_request.sequence + 1;
        });
    const bool exact_context_closed =
        has_acked_close(fixture.output, accepted_request.context, accepted_request.target);
    INFO("body submit did not enter the frozen-context hook");
    REQUIRE(submit_entered);
    INFO("fresh observation waited for the blocked body output");
    REQUIRE(ingress_progressed_while_blocked);
    INFO("the frozen observation was not held past its 150ms admission age");
    REQUIRE(held_until_old_context_expired);
    INFO("the expired frozen context retained the exact lease accepted by its returning output "
         "call");
    REQUIRE(
        (fixture.output.body_submissions.size() >= 2 && accepted_request.lease_id == old_lease &&
            (cleared_old_delivery || exact_context_closed) &&
            !fixture.coordinator.diagnostics().body_owner_present));
    INFO("the expired frozen context emitted a trigger after fresh ingress arrived");
    REQUIRE(fixture.output.submissions.size() == trigger_count);

    fixture.output.on_body_submit = {};
    const auto recovery_time = std::chrono::steady_clock::now();
    fixture.time = recovery_time;
    fixture.observe(idle(),
        std::span{&late_action, 1}); // This pre-bind event must not cross the new context.
    fixture.coordinator.tick(
        fixture.time); // Reopens one context after the expired observation retired the old one.
    fixture.time += 1ms;
    const auto recovery_action = voice_action(fixture.time);
    fixture.observe(idle(), std::span{&recovery_action, 1});
    const auto before_recovery = fixture.output.body_submissions.size();
    fixture.coordinator.tick(fixture.time);
    const auto recovered_action = fixture.output.body_submissions.back();
    INFO("a fresh post-bind action was not delivered after context recovery");
    REQUIRE((fixture.output.body_submissions.size() == before_recovery + 1 &&
             recovered_action.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             recovered_action.request.lease_id == 0 && !recovered_action.samples.empty() &&
             std::ranges::any_of(recovered_action.samples,
                 [](float sample) { return sample != 0.0F; })));
    const auto after_recovery = fixture.output.body_submissions.size();
    fixture.time = recovery_time + 11ms;
    fixture.observe(idle());
    fixture.coordinator.tick(fixture.time);
    if (fixture.output.body_submissions.size() > after_recovery) {
        const auto& continued_voice = fixture.output.body_submissions.back();
        constexpr auto sample_offset = (static_cast<std::size_t>(10) * 48U * 2U) + 1U;
        INFO("the deferred action restarted instead of continuing from its original transient "
             "window");
        REQUIRE((continued_voice.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
                 continued_voice.samples.size() > 1 &&
                 recovered_action.samples.size() > sample_offset &&
                 continued_voice.samples[1] == recovered_action.samples[sample_offset]));
    }
}

TEST_CASE("skyrim.session.coordinator_overflow_fences_stalled_batch_and_preserves_later_actions",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 921);
    fixture.settle(casting());
    fixture.time += 10ms;
    const auto stale_action = voice_action(fixture.time);
    fixture.observe(casting(), std::span{&stale_action, 1});
    const auto ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto trigger_count = fixture.output.submissions.size();

    BlockingCall blocked_submit;
    fixture.output.on_body_submit = [&] {
        blocked_submit.pause_first();
    };
    std::thread delivery{[&] { fixture.coordinator.tick(fixture.time); }};
    const bool submit_entered = blocked_submit.wait_until_entered();

    struct Result {
        std::uint64_t overflow_count{};
        std::size_t captured_count{static_cast<std::size_t>(-1)};
        bool late_published{};
    } result;

    Completion ingress_complete;
    std::thread ingress{[&] {
        for (std::size_t i = 0; i <= inbox_capacity; ++i) {
            static_cast<void>(fixture.coordinator.enqueue(ticket,
                raw_event(EventKind::voice_fire, 0x14, fixture.time)));
        }
        const auto capture = fixture.coordinator.begin_read(ticket, fixture.time);
        if (capture) {
            result.captured_count = capture->count;
            const auto later_action = voice_action(fixture.time);
            fixture.coordinator.publish(*capture,
                idle(),
                std::span{&later_action, 1},
                fixture.time);
            result.late_published = true;
        }
        result.overflow_count = fixture.coordinator.diagnostics().inbox_overflows;
        ingress_complete.signal();
    }};
    InterleavingGuard const guard{&blocked_submit, &ingress, &delivery};
    const bool ingress_progressed_while_blocked = ingress_complete.wait_for(250ms);
    blocked_submit.release();
    ingress.join();
    delivery.join();

    INFO("body submit did not enter the overflow interleaving hook");
    REQUIRE(submit_entered);
    INFO("inbox overflow waited for a blocked body output");
    REQUIRE(ingress_progressed_while_blocked);
    INFO("overflow did not clear the in-flight burst before accepting its later capture");
    REQUIRE((result.overflow_count == 1 && result.captured_count == 0 && result.late_published));
    INFO("the stale action batch crossed the overflow burst fence");
    REQUIRE((fixture.output.submissions.size() == trigger_count &&
             fixture.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_STOP));

    fixture.output.on_body_submit = {};
    const auto before_recovery = fixture.output.body_submissions.size();
    fixture.coordinator.tick(fixture.time);
    const auto& recovered_action = fixture.output.body_submissions.back();
    INFO("a post-overflow action arriving during delivery was lost instead of deferred");
    REQUIRE((fixture.output.body_submissions.size() == before_recovery + 1 &&
             recovered_action.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             !recovered_action.samples.empty() &&
             std::ranges::any_of(recovered_action.samples,
                 [](float sample) { return sample != 0.0F; })));
    const auto after_recovery = fixture.output.body_submissions.size();
    fixture.coordinator.tick(fixture.time);
    INFO("the post-overflow action replayed after one bounded recovery batch");
    REQUIRE(fixture.output.body_submissions.size() == after_recovery);
}
} // namespace skyrim_adapter::tests::session
