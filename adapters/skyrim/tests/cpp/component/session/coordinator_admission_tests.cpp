#include "interleaving_support.hpp"
#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <algorithm>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
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

TEST_CASE("skyrim.session.coordinator_hard_invalidation_clears_exact_body_lease",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.time = std::chrono::steady_clock::now();
    fixture.settle(casting());
    const auto accepted_lease = fixture.output.active_body_lease;
    INFO("hard-fence fixture did not acquire a body lease");
    REQUIRE(accepted_lease != 0);

    fixture.time = std::chrono::steady_clock::now();
    const auto action = voice_action(fixture.time);
    fixture.observe(casting(), std::span{&action, 1});
    const auto trigger_count = fixture.output.submissions.size();
    BlockingCall blocked_submit;
    fixture.output.on_body_submit = [&] {
        blocked_submit.pause_first();
    };
    std::thread delivery{[&] { fixture.coordinator.tick(); }};
    const bool submit_entered = blocked_submit.wait_until_entered();

    Completion ingress_complete;
    Diagnostics during_block{};
    std::thread ingress{[&] {
        fixture.coordinator.control(Control::focus_lost, fixture.time);
        during_block = fixture.coordinator.diagnostics();
        ingress_complete.signal();
    }};
    InterleavingGuard const guard{&blocked_submit, &ingress, &delivery};
    const bool ingress_progressed_while_blocked = ingress_complete.wait_for(250ms);
    blocked_submit.release();
    ingress.join();
    delivery.join();

    const auto accepted_request = fixture.output.body_submissions.back().request;
    const auto exact_clear =
        std::ranges::find_if(fixture.output.body_clears, [&](const BodyClear& clear) {
            return clear.lease_id == accepted_request.lease_id &&
                   same_effects_context(clear.context, accepted_request.context) &&
                   same_device_target(clear.target, accepted_request.target) &&
                   clear.audio_instance_id == accepted_request.audio_instance_id &&
                   clear.sequence == accepted_request.sequence + 1;
        });
    const bool exact_context_closed =
        has_acked_close(fixture.output, accepted_request.context, accepted_request.target);
    INFO("body submit did not enter the blocking output hook");
    REQUIRE(submit_entered);
    INFO("hard control or diagnostics waited for body output");
    REQUIRE(ingress_progressed_while_blocked);
    INFO("diagnostics did not report the cached accepted body owner");
    REQUIRE(during_block.body_owner_present);
    INFO("focus loss failed to clear the exact lease accepted by the returning output call");
    REQUIRE((fixture.output.body_submissions.size() >= 2 &&
             accepted_request.lease_id == accepted_lease &&
             (exact_clear != fixture.output.body_clears.end() || exact_context_closed) &&
             !fixture.coordinator.diagnostics().body_owner_present));
    INFO("the body call returning after hard invalidation emitted a trigger frame");
    REQUIRE(fixture.output.submissions.size() == trigger_count);
}

TEST_CASE("skyrim.session.coordinator_menu_during_blocked_submit_keeps_neutral_lease",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 919);
    fixture.settle(casting());
    const auto lease = fixture.output.active_body_lease;
    INFO("menu interleaving fixture did not acquire a body lease");
    REQUIRE(lease != 0);

    fixture.time += 10ms;
    const auto action = voice_action(fixture.time);
    fixture.observe(casting(), std::span{&action, 1});
    const auto trigger_count = fixture.output.submissions.size();
    BlockingCall blocked_submit;
    fixture.output.on_body_submit = [&] {
        blocked_submit.pause_first();
    };
    std::thread delivery{[&] { fixture.coordinator.tick(fixture.time); }};
    const bool submit_entered = blocked_submit.wait_until_entered();

    struct Result {
        bool menu_ticket_available{};
        bool paused_capture_published{};
    } result;

    Completion ingress_complete;
    std::thread ingress{[&] {
        fixture.coordinator.control(Control::menu_suppressed, fixture.time);
        const auto ticket = fixture.coordinator.native_ticket();
        if (ticket) {
            const auto capture = fixture.coordinator.begin_read(*ticket, fixture.time);
            if (capture) {
                auto paused = casting();
                paused.menus_clear = false;
                fixture.coordinator.publish(*capture, paused, {}, fixture.time);
                result.paused_capture_published = true;
            }
            result.menu_ticket_available = true;
        }
        ingress_complete.signal();
    }};
    InterleavingGuard const guard{&blocked_submit, &ingress, &delivery};
    const bool ingress_progressed_while_blocked = ingress_complete.wait_for(250ms);
    blocked_submit.release();
    ingress.join();
    delivery.join();

    const auto& stopped = fixture.output.body_submissions.back().request;
    INFO("body submit did not enter the menu interleaving hook");
    REQUIRE(submit_entered);
    INFO("menu control or paused publication waited for body output");
    REQUIRE(ingress_progressed_while_blocked);
    INFO("fresh paused sample was not published");
    REQUIRE((result.menu_ticket_available && result.paused_capture_published));
    INFO("the returning body call cleared or refreshed gameplay on the exact neutral menu lease");
    REQUIRE((fixture.output.body_clears.empty() &&
             stopped.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP && stopped.lease_id == lease &&
             stopped.fallback_rumble.low_frequency == 0.0F &&
             stopped.fallback_rumble.high_frequency == 0.0F &&
             fixture.coordinator.diagnostics().body_owner_present));
    INFO("the body call returning after menu suppression emitted a gameplay trigger");
    REQUIRE(fixture.output.submissions.size() == trigger_count);
}

TEST_CASE("skyrim.session.coordinator_ordinary_stale_admission_resets_transient_history",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 922);
    fixture.settle(idle());
    fixture.time += 1ms;
    const auto event_time = fixture.time;
    const auto old_ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto old_capture =
        require_capture(fixture.coordinator.begin_read(old_ticket, event_time));
    auto three_word_voice = voice_action(event_time);
    three_word_voice.event.shout_level = ShoutLevel::three_words;
    fixture.coordinator.publish(old_capture, idle(), std::span{&three_word_voice, 1}, event_time);
    fixture.coordinator.tick(event_time);
    const auto& initial_voice = fixture.output.body_submissions.back();
    INFO("three-word voice fixture did not create a live transient");
    REQUIRE((
        initial_voice.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
        !initial_voice.samples.empty() &&
        std::ranges::any_of(initial_voice.samples, [](float sample) { return sample != 0.0F; })));

    const auto stale_time = event_time + 151ms;
    fixture.coordinator.tick(stale_time);
    const auto stale_ticket_capture = fixture.coordinator.begin_read(old_ticket, stale_time);
    const auto fresh_time = event_time + 152ms;
    const auto current_ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto fresh_capture =
        require_capture(fixture.coordinator.begin_read(current_ticket, fresh_time));
    fixture.coordinator.publish(fresh_capture, idle(), {}, fresh_time);
    const auto before_fresh_delivery = fixture.output.body_submissions.size();
    fixture.coordinator.tick(fresh_time);

    bool stale_voice_resurfaced{};
    for (std::size_t index = before_fresh_delivery; index < fixture.output.body_submissions.size();
        ++index) {
        const auto& request = fixture.output.body_submissions[index];
        stale_voice_resurfaced =
            stale_voice_resurfaced ||
            (request.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
                !request.samples.empty() &&
                std::ranges::any_of(request.samples, [](float sample) { return sample != 0.0F; }));
    }
    INFO("stale three-word voice resurfaced after ordinary admission loss");
    REQUIRE(!stale_voice_resurfaced);
    INFO("ordinary stale admission loss did not fence the prior ticket and capture path");
    REQUIRE((current_ticket.epoch != old_ticket.epoch && !stale_ticket_capture &&
             !fixture.coordinator.enqueue(old_ticket,
                 raw_event(EventKind::voice_fire, 0x14, fresh_time))));
}

TEST_CASE("skyrim.session.coordinator_cold_binding_and_fresh_paused_stop",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 924);
    fixture.observe(casting());
    fixture.coordinator.tick(fixture.time);
    INFO("a cold coordinator activated a context before its first output binding");
    REQUIRE((fixture.output.target_queries == 1 && fixture.output.body_submissions.empty()));

    fixture.time += 1ms;
    fixture.observe(casting());
    fixture.coordinator.tick(fixture.time);
    INFO("the first fresh post-bind observation did not establish body ownership");
    REQUIRE((fixture.output.target_queries == 2 && fixture.output.active_body_lease != 0));
    const auto lease = fixture.output.active_body_lease;

    fixture.time += 151ms;
    fixture.coordinator.control(Control::menu_suppressed, fixture.time);
    auto paused = casting();
    paused.menus_clear = false;
    fixture.observe(paused);
    fixture.coordinator.tick(fixture.time);
    const auto& stopped = fixture.output.body_submissions.back().request;
    INFO(
        "a fresh paused sample at T+151 cleared its live lease instead of issuing same-lease STOP");
    REQUIRE((fixture.output.body_clears.empty() &&
             stopped.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP && stopped.lease_id == lease &&
             stopped.fallback_rumble.low_frequency == 0.0F &&
             stopped.fallback_rumble.high_frequency == 0.0F));
}

TEST_CASE("skyrim.session.coordinator_observation_menu_invalidates_before_neutral_context",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 925);
    fixture.settle(casting());
    const auto lease = fixture.output.active_body_lease;
    const auto old_ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto trigger_count = fixture.output.submissions.size();
    const auto body_count = fixture.output.body_submissions.size();
    fixture.time += 10ms;
    auto paused = casting();
    paused.menus_clear = false;
    fixture.observe(paused);
    fixture.coordinator.tick(fixture.time);

    const auto fenced_ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("native menu observation activated delivery before its preserve-observation fence");
    REQUIRE((fenced_ticket.epoch != old_ticket.epoch &&
             fixture.output.submissions.size() == trigger_count &&
             fixture.output.body_submissions.size() == body_count));

    fixture.coordinator.tick(fixture.time);
    const auto& neutral = fixture.output.body_submissions.back().request;
    INFO(
        "the preserved paused observation did not retain its exact owner through neutral delivery");
    REQUIRE((fixture.output.body_submissions.size() == body_count + 1 &&
             neutral.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP && neutral.lease_id == lease &&
             fixture.output.body_clears.empty()));
}
} // namespace skyrim_adapter::tests::session
