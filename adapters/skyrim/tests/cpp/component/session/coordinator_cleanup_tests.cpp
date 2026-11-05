#include "interleaving_support.hpp"
#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <algorithm>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <input_next_core.h>
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

TEST_CASE("skyrim.session.coordinator_blocked_clear_cannot_ack_a_coalesced_withdrawal",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    const auto previous_request = fixture.output.body_submissions.back().request;
    const auto previous_lease = fixture.output.active_body_lease;
    INFO("coalesced-clear fixture did not acquire a body lease");
    REQUIRE(previous_lease != 0);

    fixture.coordinator.control(Control::pre_load, fixture.time);
    BlockingCall blocked_clear;
    fixture.output.on_body_clear = [&] {
        blocked_clear.pause_first();
    };
    const auto tick_time = fixture.time;
    std::thread delivery{[&] { fixture.coordinator.tick(tick_time); }};
    const bool clear_entered = blocked_clear.wait_until_entered();

    struct Result {
        bool fresh_paused_publication{};
    } result;

    Completion ingress_complete;
    std::thread ingress{[&] {
        fixture.coordinator.control(Control::load_succeeded, tick_time);
        fixture.coordinator.control(Control::menu_suppressed, tick_time);
        const auto ticket = fixture.coordinator.native_ticket();
        if (ticket) {
            const auto capture = fixture.coordinator.begin_read(*ticket, tick_time);
            if (capture) {
                auto paused = casting();
                paused.menus_clear = false;
                fixture.coordinator.publish(*capture, paused, {}, tick_time);
                result.fresh_paused_publication = true;
            }
        }
        ingress_complete.signal();
    }};
    InterleavingGuard const guard{&blocked_clear, &ingress, &delivery};
    const bool ingress_progressed_while_blocked = ingress_complete.wait_for(250ms);
    blocked_clear.release();
    ingress.join();
    delivery.join();

    const auto exact_clear =
        std::ranges::find_if(fixture.output.body_clears, [&](const BodyClear& clear) {
            return clear.lease_id == previous_lease &&
                   same_effects_context(clear.context, previous_request.context) &&
                   same_device_target(clear.target, previous_request.target) &&
                   clear.audio_instance_id == previous_request.audio_instance_id &&
                   clear.sequence == previous_request.sequence + 1;
        });
    INFO("mandatory body clear did not enter the blocking output hook");
    REQUIRE(clear_entered);
    INFO("coalesced load/menu controls waited for body clear");
    REQUIRE(ingress_progressed_while_blocked);
    INFO("fresh paused observation did not publish during body clear");
    REQUIRE(result.fresh_paused_publication);
    INFO("a newer sticky withdrawal was acknowledged by the prior clear and sent output in the "
         "same tick");
    REQUIRE((exact_clear != fixture.output.body_clears.end() &&
             fixture.output.body_submissions.size() == 1 &&
             !fixture.coordinator.diagnostics().body_owner_present));
}

TEST_CASE("skyrim.session.coordinator_stale_cleanup_preserves_fresh_publication",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 923);
    fixture.settle(idle());
    const auto previous_request = fixture.output.body_submissions.back().request;
    const auto previous_lease = fixture.output.active_body_lease;
    const auto old_ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("stale-cleanup fixture did not acquire a body lease");
    REQUIRE(previous_lease != 0);

    const auto stale_time = fixture.time + 151ms;
    BlockingCall blocked_clear;
    fixture.output.on_body_clear = [&] {
        blocked_clear.pause_first();
    };
    const auto submission_count_before_stale_tick = fixture.output.body_submissions.size();
    std::thread delivery{[&] { fixture.coordinator.tick(stale_time); }};
    const bool clear_entered = blocked_clear.wait_until_entered();

    struct Result {
        Epoch published_epoch{};
        bool published{};
    } result;

    Completion ingress_complete;
    std::thread ingress{[&] {
        const auto current_ticket = fixture.coordinator.native_ticket();
        if (current_ticket) {
            result.published_epoch = current_ticket->epoch;
            const auto capture = fixture.coordinator.begin_read(*current_ticket, stale_time);
            if (capture) {
                auto fresh_voice = voice_action(stale_time);
                fresh_voice.event.shout_level = ShoutLevel::three_words;
                fixture.coordinator.publish(*capture,
                    idle(),
                    std::span{&fresh_voice, 1},
                    stale_time);
                result.published = true;
            }
        }
        ingress_complete.signal();
    }};
    InterleavingGuard const guard{&blocked_clear, &ingress, &delivery};
    const bool ingress_progressed_while_blocked = ingress_complete.wait_for(250ms);
    blocked_clear.release();
    ingress.join();
    delivery.join();
    fixture.output.on_body_clear = {};

    const auto exact_clear =
        std::ranges::find_if(fixture.output.body_clears, [&](const BodyClear& clear) {
            return clear.lease_id == previous_lease &&
                   same_effects_context(clear.context, previous_request.context) &&
                   same_device_target(clear.target, previous_request.target) &&
                   clear.audio_instance_id == previous_request.audio_instance_id &&
                   clear.sequence == previous_request.sequence + 1;
        });
    bool fresh_voice_delivered{};
    for (std::size_t index = submission_count_before_stale_tick;
        index < fixture.output.body_submissions.size();
        ++index) {
        const auto& submission = fixture.output.body_submissions[index];
        fresh_voice_delivered =
            fresh_voice_delivered ||
            (submission.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
                !submission.samples.empty() &&
                std::ranges::any_of(submission.samples,
                    [](float sample) { return sample != 0.0F; }));
    }
    INFO("ordinary stale admission did not clear its old body owner");
    REQUIRE(clear_entered);
    INFO("fresh publication waited for the stale-owner clear");
    REQUIRE(ingress_progressed_while_blocked);
    INFO("cleanup from the expired admission erased or lost a fresh publication and its action");
    REQUIRE((result.published && result.published_epoch != old_ticket.epoch &&
             exact_clear != fixture.output.body_clears.end() && fresh_voice_delivered));
}

TEST_CASE("skyrim.session.coordinator_cleanup_ack_progresses_after_focus_loss",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(idle());
    fixture.output.query_context_status = INPUT_NEXT_STATUS_REVOKED;
    fixture.output.ack_close_status = INPUT_NEXT_STATUS_BUSY;

    BlockingCall blocked_query;
    fixture.output.on_query = [&] {
        blocked_query.pause_first();
    };
    const auto tick_time = fixture.time + 1ms;
    fixture.time = tick_time;
    fixture.observe(idle());
    std::thread delivery{[&] { fixture.coordinator.tick(tick_time); }};
    const bool query_entered = blocked_query.wait_until_entered();

    struct IngressResult {
        bool capture_started{};
        bool published{};
        bool diagnostics_read{};
    } result;

    Completion ingress_complete;
    std::thread ingress{[&] {
        fixture.coordinator.control(Control::focus_lost, tick_time);
        const auto ticket = fixture.coordinator.native_ticket();
        if (ticket) {
            const auto capture = fixture.coordinator.begin_read(*ticket, tick_time);
            result.capture_started = capture.has_value();
            if (capture) {
                fixture.coordinator.publish(*capture, idle(), {}, tick_time);
                result.published = true;
            }
        }
        static_cast<void>(fixture.coordinator.diagnostics());
        result.diagnostics_read = true;
        ingress_complete.signal();
    }};
    InterleavingGuard const guard{&blocked_query, &ingress, &delivery};
    const bool ingress_progressed_while_blocked = ingress_complete.wait_for(250ms);
    blocked_query.release();
    ingress.join();
    delivery.join();
    fixture.output.on_query = {};

    INFO("runtime query did not block lifecycle callbacks or diagnostics");
    REQUIRE((query_entered && ingress_progressed_while_blocked && result.capture_started &&
             result.published && result.diagnostics_read));
    INFO("a context retired after reconciliation must wait for the next cleanup pass");
    REQUIRE((fixture.output.close_calls == 0 && fixture.output.ack_close_calls == 0 &&
             fixture.output.contexts.size() == 1));
    fixture.coordinator.tick(tick_time + 1ms);
    INFO("binding loss did not retain the exact close receipt after BUSY acknowledgement");
    REQUIRE((fixture.output.close_receipts.size() == 1 &&
             fixture.output.acknowledged_close_receipts.size() == 1 &&
             fixture.output.contexts.size() == 1 &&
             same_effects_context(fixture.output.close_receipts.front().context,
                 fixture.output.acknowledged_close_receipts.front().context) &&
             same_device_target(fixture.output.close_receipts.front().target,
                 fixture.output.acknowledged_close_receipts.front().target) &&
             fixture.output.close_receipts.front().close_request_id ==
                 fixture.output.acknowledged_close_receipts.front().close_request_id));

    const auto target_queries = fixture.output.target_queries;
    const auto open_calls = fixture.output.open_calls;
    const auto query_calls = fixture.output.query_context_calls;
    const auto close_calls = fixture.output.close_calls;
    const auto trigger_submissions = fixture.output.submissions.size();
    const auto trigger_clears = fixture.output.clears.size();
    const auto body_submissions = fixture.output.body_submissions.size();
    const auto body_clears = fixture.output.body_clears.size();
    const auto receipt = fixture.output.close_receipts.front();
    fixture.output.ack_close_status = INPUT_NEXT_STATUS_OK;
    fixture.coordinator.tick(tick_time + 2ms);

    INFO("paused cleanup retried binding or issued ordinary output calls");
    REQUIRE((fixture.output.target_queries == target_queries &&
             fixture.output.open_calls == open_calls &&
             fixture.output.query_context_calls == query_calls &&
             fixture.output.close_calls == close_calls &&
             fixture.output.submissions.size() == trigger_submissions &&
             fixture.output.clears.size() == trigger_clears &&
             fixture.output.body_submissions.size() == body_submissions &&
             fixture.output.body_clears.size() == body_clears));
    INFO("focus loss resumed gameplay or failed to retire the acknowledged context");
    REQUIRE((fixture.output.acknowledged_close_receipts.size() == 2 &&
             same_effects_context(fixture.output.acknowledged_close_receipts.back().context,
                 receipt.context) &&
             same_device_target(fixture.output.acknowledged_close_receipts.back().target,
                 receipt.target) &&
             fixture.output.acknowledged_close_receipts.back().close_request_id ==
                 receipt.close_request_id &&
             fixture.output.acknowledged_close_receipts.back().publication_revision ==
                 receipt.publication_revision &&
             fixture.output.contexts.empty() &&
             !fixture.coordinator.diagnostics().body_owner_present));
}

TEST_CASE("skyrim.session.coordinator_cleanup_ack_progresses_after_stale_observation",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(idle());
    fixture.output.query_context_status = INPUT_NEXT_STATUS_REVOKED;
    fixture.output.ack_close_status = INPUT_NEXT_STATUS_BUSY;
    fixture.time += 1ms;
    fixture.observe(idle());
    fixture.coordinator.tick(fixture.time);
    REQUIRE((fixture.output.close_calls == 0 && fixture.output.ack_close_calls == 0 &&
             fixture.output.contexts.size() == 1));
    const auto stale_time = fixture.time + observation_max_age + 1ms;
    fixture.coordinator.tick(stale_time);
    REQUIRE((fixture.output.close_receipts.size() == 1 &&
             fixture.output.acknowledged_close_receipts.size() == 1 &&
             fixture.output.contexts.size() == 1));

    const auto receipt = fixture.output.close_receipts.front();
    fixture.output.ack_close_status = INPUT_NEXT_STATUS_OK;
    const auto target_queries = fixture.output.target_queries;
    const auto open_calls = fixture.output.open_calls;
    const auto query_calls = fixture.output.query_context_calls;
    const auto close_calls = fixture.output.close_calls;
    const auto trigger_submissions = fixture.output.submissions.size();
    const auto trigger_clears = fixture.output.clears.size();
    const auto body_submissions = fixture.output.body_submissions.size();
    const auto body_clears = fixture.output.body_clears.size();
    fixture.coordinator.tick(stale_time + 1ms);

    INFO("stale cleanup resolved or reopened a foreground binding");
    REQUIRE((fixture.output.target_queries == target_queries &&
             fixture.output.open_calls == open_calls &&
             fixture.output.query_context_calls == query_calls &&
             fixture.output.close_calls == close_calls));
    INFO("stale cleanup emitted an ordinary effect mutation or resumed gameplay");
    REQUIRE((fixture.output.submissions.size() == trigger_submissions &&
             fixture.output.clears.size() == trigger_clears &&
             fixture.output.body_submissions.size() == body_submissions &&
             fixture.output.body_clears.size() == body_clears));
    INFO("stale cleanup did not ACK its exact retained receipt and retire its context");
    REQUIRE((fixture.output.acknowledged_close_receipts.size() == 2 &&
             same_effects_context(fixture.output.acknowledged_close_receipts.back().context,
                 receipt.context) &&
             same_device_target(fixture.output.acknowledged_close_receipts.back().target,
                 receipt.target) &&
             fixture.output.acknowledged_close_receipts.back().close_request_id ==
                 receipt.close_request_id &&
             fixture.output.acknowledged_close_receipts.back().publication_revision ==
                 receipt.publication_revision &&
             fixture.output.contexts.empty() &&
             !fixture.coordinator.diagnostics().body_owner_present));
}
} // namespace skyrim_adapter::tests::session
