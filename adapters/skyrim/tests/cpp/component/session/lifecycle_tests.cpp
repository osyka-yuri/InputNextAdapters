#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <input_next_core.h>
#include <input_next_effects.h>
#include <inputnext_adapters/led_policy.hpp>
#include <memory>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>
#include <utility>

namespace skyrim_adapter::tests::session {

TEST_CASE("skyrim.session.idle_and_withdrawal", "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 916);
    fixture.settle(idle());
    INFO("idle body ownership submitted a trigger channel");
    REQUIRE(fixture.output.submissions.empty());
    INFO("playable silence did not acquire a finite body lease");
    REQUIRE(fixture.output.body_submissions.size() == 1);
    INFO("neutral baseline was not represented as a silent KEEP lease");
    REQUIRE(fixture.output.body_submissions.front().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_KEEP);
    INFO("neutral body ownership was not finite");
    REQUIRE(fixture.output.body_submissions.front().request.ttl_ms == hold_ttl_ms);
    fixture.advance(casting());
    INFO("held magic produced no trigger output");
    REQUIRE(!fixture.output.submissions.empty());
    INFO("held magic did not replace the body PCM");
    REQUIRE(fixture.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
    const auto count = fixture.output.submissions.size();
    fixture.coordinator.tick(fixture.time + 20ms);
    INFO("unchanged hold refreshed every tick");
    REQUIRE(fixture.output.submissions.size() == count);
    fixture.advance(idle());
    INFO("ended intent was not withdrawn");
    REQUIRE(!fixture.output.clears.empty());
    INFO("left intent remained active");
    REQUIRE((fixture.output.clears.back().mask & INPUT_NEXT_EFFECTS_LEFT_TRIGGER) != 0);
    INFO("withdrawal retargeted a session");
    REQUIRE(same_device_target(fixture.output.clears.back().target, fixture.output.target));
    INFO("ending the last cue did not stop PCM once");
    REQUIRE(fixture.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_STOP);
}

TEST_CASE("skyrim.session.led_observation_projects_and_withdraws_the_exact_group",
    "[component][skyrim][session][led]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_LIGHTBAR;

    auto health = idle();
    health.led.subject_id = health.player_id;
    health.led.health = inputnext::adapters::LedHealthSample{.current = 100.0F, .maximum = 100.0F};
    fixture.settle(health);

    health.led.health->current = 99.0F;
    fixture.advance(health);
    INFO("a policy LED result was not projected through the coordinator to the whole group");
    REQUIRE(!fixture.output.submissions.empty());
    const auto& accepted = fixture.output.submissions.back();
    REQUIRE((accepted.channel_mask & INPUT_NEXT_EFFECTS_LEDS) != 0U);
    CHECK(accepted.leds.player_index == 0U);
    CHECK(accepted.leds.microphone_led == 0U);
    CHECK(accepted.leds.reserved == 0U);
    CHECK(accepted.ttl_ms > 0U);
    CHECK(accepted.ttl_ms <= 200U);

    fixture.coordinator.control(Control::menu_suppressed, fixture.time);
    fixture.coordinator.tick(fixture.time);
    INFO("menu suppression did not conditionally withdraw the accepted LED group");
    REQUIRE(!fixture.output.clears.empty());
    CHECK(fixture.output.clears.back().mask == INPUT_NEXT_EFFECTS_LEDS);
    CHECK(session_test::same_device_target(fixture.output.clears.back().target,
        fixture.output.target));

    const auto request_count = fixture.output.submissions.size();
    fixture.coordinator.control(Control::menu_resumed, fixture.time);
    fixture.time += 1ms;
    fixture.observe(health);
    fixture.coordinator.tick(fixture.time);
    INFO("a fresh same-subject menu resume failed to restore the still-live original LED window");
    REQUIRE(fixture.output.submissions.size() == request_count + 1U);
    CHECK((fixture.output.submissions.back().channel_mask & INPUT_NEXT_EFFECTS_LEDS) != 0U);
    CHECK(fixture.output.submissions.back().ttl_ms <= 200U);
}

TEST_CASE("skyrim.session.menu_followed_by_overflow_keeps_the_hard_led_reset",
    "[component][skyrim][session][led]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_LIGHTBAR;

    auto health = idle();
    health.led.subject_id = health.player_id;
    health.led.health = inputnext::adapters::LedHealthSample{.current = 100.0F, .maximum = 100.0F};
    fixture.settle(health);
    health.led.health->current = 99.0F;
    fixture.advance(health);
    REQUIRE(!fixture.output.submissions.empty());
    const auto submissions_before_menu = fixture.output.submissions.size();

    fixture.coordinator.control(Control::menu_suppressed, fixture.time);
    fixture.coordinator.tick(fixture.time);
    fixture.coordinator.control(Control::menu_resumed, fixture.time);
    const auto ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto event = raw_event(EventKind::voice_fire, health.player_id, fixture.time);
    for (std::size_t index = 0; index <= inbox_capacity; ++index) {
        REQUIRE(fixture.coordinator.enqueue(ticket, event));
    }
    REQUIRE(fixture.coordinator.diagnostics().inbox_overflows == 1U);

    fixture.time += 1ms;
    fixture.observe(health);
    fixture.coordinator.tick(fixture.time);
    INFO("an event overflow after a menu remains a hard reset and cannot restore the old LED");
    CHECK(fixture.output.submissions.size() == submissions_before_menu);
}

TEST_CASE("skyrim.session.menus_hold_neutral_authority_and_resume_fresh",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    fixture.output.capabilities.audio_instance_id = 71;
    fixture.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    fixture.settle(casting());
    const auto lease = fixture.output.active_body_lease;
    const auto old_ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto old_capture = fixture.capture();

    fixture.coordinator.control(Control::menu_suppressed, fixture.time);
    fixture.coordinator.tick(fixture.time);
    const auto& stopped = fixture.output.body_submissions.back().request;
    INFO("menu did not silence PCM and fallback while retaining its exact body owner");
    REQUIRE((stopped.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP && stopped.lease_id == lease &&
             stopped.fallback_rumble.low_frequency == 0.0F &&
             stopped.fallback_rumble.high_frequency == 0.0F));
    INFO("menu failed to withdraw triggers independently of body authority");
    REQUIRE((!fixture.output.clears.empty() && fixture.output.body_clears.empty()));
    INFO("pre-menu callback crossed the suspension epoch");
    REQUIRE(!fixture.coordinator.enqueue(old_ticket,
        raw_event(EventKind::voice_fire, 0x14, fixture.time)));
    fixture.coordinator.publish(old_capture, casting(), {}, fixture.time);
    const auto trigger_count = fixture.output.submissions.size();
    auto paused = casting();
    paused.menus_clear = false;
    std::uint64_t sequence = stopped.sequence;
    for (int renewal = 0; renewal < 4; ++renewal) {
        fixture.advance(paused, 100ms);
        const auto& neutral = fixture.output.body_submissions.back().request;
        INFO("menu silence did not renew one finite neutral owner");
        REQUIRE((neutral.lease_id == lease && neutral.sequence == ++sequence &&
                 neutral.ttl_ms == 200 && neutral.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
                 neutral.fallback_rumble.low_frequency == 0.0F &&
                 neutral.fallback_rumble.high_frequency == 0.0F));
        INFO("menu produced gameplay output or exposed the game's rumble channel");
        REQUIRE((fixture.output.submissions.size() == trigger_count &&
                 fixture.output.body_clears.empty()));
    }

    fixture.coordinator.control(Control::menu_resumed, fixture.time);
    fixture.coordinator.tick(fixture.time);
    INFO("menu resume reused an observation from the suppressed session");
    REQUIRE(fixture.output.submissions.size() == trigger_count);
    fixture.advance(casting(), 1ms);
    INFO("fresh gameplay did not resume effects under the retained owner");
    REQUIRE((fixture.output.submissions.size() > trigger_count &&
             fixture.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             fixture.output.body_submissions.back().request.lease_id == lease));

    fixture.coordinator.control(Control::menu_suppressed, fixture.time);
    fixture.coordinator.tick(fixture.time);
    fixture.coordinator.tick(fixture.time + 151ms);
    INFO("stale menu observation kept body authority beyond admission");
    REQUIRE((!fixture.output.body_clears.empty() &&
             fixture.output.body_clears.back().lease_id == lease &&
             !fixture.coordinator.diagnostics().body_owner_present));
}

TEST_CASE("skyrim.session.neutral_hd_recovery_is_bounded_and_explicit",
    "[component][skyrim][session]") {
    const auto successful_fixture = std::make_unique<Fixture>();
    auto& successful = *successful_fixture;
    successful.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    successful.output.capabilities.audio_instance_id = 71;
    successful.settle(idle());
    const auto original_lease = successful.output.active_body_lease;
    successful.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    successful.advance(idle(), 100ms);
    const auto& transition = successful.output.body_submissions.back().request;
    INFO("ready neutral recovery did not retain its exact live owner through STOP");
    REQUIRE((transition.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP &&
             transition.lease_id == original_lease && transition.sequence == 2 &&
             transition.route_preference == INPUT_NEXT_EFFECTS_BODY_ROUTE_PREFER_HD &&
             successful.coordinator.diagnostics().accepted_body_route ==
                 INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD));

    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    fixture.output.capabilities.audio_instance_id = 71;
    fixture.settle(idle());
    const auto lease = fixture.output.active_body_lease;
    auto diagnostics = fixture.coordinator.diagnostics();
    INFO("in-memory diagnostics did not preserve the accepted fallback route");
    REQUIRE((diagnostics.body_owner_present &&
             diagnostics.accepted_body_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC &&
             diagnostics.body_fallback_reason == INPUT_NEXT_EFFECTS_BODY_FALLBACK_NOT_READY));
    const auto count = fixture.output.body_submissions.size();
    fixture.advance(idle(), 99ms);
    INFO("neutral recovery retried before its bounded cadence");
    REQUIRE(fixture.output.body_submissions.size() == count);

    fixture.output.body_submit_status = INPUT_NEXT_STATUS_UNAVAILABLE;
    fixture.advance(idle(), 1ms);
    INFO("neutral recovery reused KEEP instead of an explicit silent HD transition");
    REQUIRE(fixture.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_STOP);
    const auto failed_count = fixture.output.body_submissions.size();
    fixture.output.body_submit_status = INPUT_NEXT_STATUS_OK;
    fixture.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    fixture.advance(idle(), 99ms);
    INFO("failed neutral transition retried every tick");
    REQUIRE(fixture.output.body_submissions.size() == failed_count);
    fixture.advance(idle(), 1ms);
    const auto& recovered = fixture.output.body_submissions.back().request;
    INFO("neutral recovery reused an expired owner instead of acquiring finite silence");
    REQUIRE((recovered.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP && recovered.lease_id == 0 &&
             recovered.sequence == 1 && recovered.audio_instance_id == 71 &&
             recovered.route_preference == INPUT_NEXT_EFFECTS_BODY_ROUTE_PREFER_HD));
    const auto recovered_lease = fixture.output.active_body_lease;
    INFO("expired neutral owner retained its lease identity");
    REQUIRE(recovered_lease != lease);
    diagnostics = fixture.coordinator.diagnostics();
    INFO("accepted HD recovery did not update bounded in-memory diagnostics");
    REQUIRE((diagnostics.accepted_body_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD &&
             diagnostics.body_fallback_reason == INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE));
    fixture.advance(idle(), 100ms);
    INFO("stable HD silence repeated recovery STOP commands");
    REQUIRE(fixture.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_KEEP);

    fixture.coordinator.control(Control::focus_lost, fixture.time);
    fixture.coordinator.tick(fixture.time);
    INFO("focus loss retained the recovered body owner");
    REQUIRE((!fixture.output.body_clears.empty() &&
             fixture.output.body_clears.back().lease_id == recovered_lease &&
             !fixture.coordinator.diagnostics().body_owner_present));
}

TEST_CASE("skyrim.session.load_and_callback_fences", "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    const auto old_ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto old_capture = fixture.capture();
    fixture.coordinator.control(Control::pre_load, fixture.time);
    INFO("callback crossed the load epoch");
    REQUIRE(!fixture.coordinator.enqueue(old_ticket,
        raw_event(EventKind::voice_fire, 0x14, fixture.time)));
    fixture.coordinator.publish(old_capture, casting(), {}, fixture.time);
    fixture.coordinator.tick(fixture.time);
    INFO("load did not withdraw accepted intents");
    REQUIRE(!fixture.output.clears.empty());
    INFO("player read admitted during load");
    REQUIRE(!fixture.coordinator.begin_read(require_ticket(fixture.coordinator.native_ticket()),
        fixture.time));
    fixture.coordinator.control(Control::load_failed, fixture.time);
    const auto previous = fixture.output.submissions.size();
    fixture.time += 50ms;
    fixture.coordinator.tick(fixture.time);
    INFO("failed load resumed effects");
    REQUIRE(fixture.output.submissions.size() == previous);
    fixture.coordinator.control(Control::load_succeeded, fixture.time);
    fixture.observe(casting());
    fixture.coordinator.tick(fixture.time);
    INFO("successful load could not recover held state");
    REQUIRE(fixture.output.submissions.size() > previous);
    INFO("old task crossed load epoch");
    REQUIRE(!fixture.coordinator.begin_read(old_ticket, fixture.time));
}

TEST_CASE("skyrim.session.coalesced_lifecycle_recovery_withdraws_the_old_owner",
    "[component][skyrim][session]") {
    for (const auto transition : {
             std::pair{Control::pre_load, Control::load_succeeded},
             std::pair{Control::focus_lost, Control::focus_gained},
         }) {
        const auto fixture_owner = std::make_unique<Fixture>();
        auto& fixture = *fixture_owner;
        fixture.settle(casting());
        const auto old_lease = fixture.output.active_body_lease;
        const auto old_ticket = require_ticket(fixture.coordinator.native_ticket());
        const auto old_capture = fixture.capture();
        const auto previous_submissions = fixture.output.body_submissions.size();
        fixture.time += 20ms;
        fixture.coordinator.control(transition.first, fixture.time);
        fixture.coordinator.control(transition.second, fixture.time);
        fixture.coordinator.control(Control::menu_suppressed, fixture.time);
        fixture.coordinator.control(Control::menu_resumed, fixture.time);
        fixture.time += 1ms;
        fixture.coordinator.publish(old_capture, casting(), {}, fixture.time);
        INFO("coalesced recovery admitted a callback from the previous epoch");
        REQUIRE(!fixture.coordinator.begin_read(old_ticket, fixture.time));
        fixture.observe(casting());
        fixture.coordinator.tick(fixture.time);
        INFO("coalesced recovery retained the old exact owner through a later menu transition");
        REQUIRE((
            !fixture.output.body_clears.empty() &&
            fixture.output.body_clears.back().lease_id == old_lease &&
            same_device_target(fixture.output.body_clears.back().target, fixture.output.target)));
        INFO("coalesced recovery renewed the previous lease instead of acquiring a new owner");
        REQUIRE((fixture.output.body_submissions.size() == previous_submissions + 1 &&
                 fixture.output.body_submissions.back().request.lease_id == 0 &&
                 fixture.output.body_submissions.back().request.sequence == 1 &&
                 fixture.output.active_body_lease != old_lease));
    }
}

TEST_CASE("skyrim.session.mandatory_withdrawal_waits_for_clear_or_expiry",
    "[component][skyrim][session]") {
    for (const bool expire : {false, true}) {
        const auto fixture_owner = std::make_unique<Fixture>();
        auto& fixture = *fixture_owner;
        fixture.settle(casting());
        const auto old_lease = fixture.output.active_body_lease;
        const auto previous_submissions = fixture.output.body_submissions.size();
        const auto previous_trigger_submissions = fixture.output.submissions.size();
        const auto previous_trigger_clears = fixture.output.clears.size();
        INFO("mandatory-clear fixture did not establish a trigger owner");
        REQUIRE(previous_trigger_submissions != 0);
        fixture.output.body_clear_status = INPUT_NEXT_STATUS_BUSY;
        fixture.time += 20ms;
        fixture.coordinator.control(Control::pre_load, fixture.time);
        fixture.coordinator.control(Control::load_succeeded, fixture.time);
        fixture.observe(casting());
        fixture.coordinator.tick(fixture.time);
        INFO("failed mandatory clear renewed or replaced the previous owner");
        REQUIRE((fixture.output.body_submissions.size() == previous_submissions &&
                 fixture.output.body_clears.back().lease_id == old_lease));
        INFO("busy body cleanup failed to withdraw the independent accepted trigger owner");
        REQUIRE((fixture.output.clears.size() == previous_trigger_clears + 1 &&
                 same_device_target(fixture.output.clears.back().target, fixture.output.target) &&
                 fixture.output.clears.back().mask != 0 &&
                 fixture.output.submissions.size() == previous_trigger_submissions));
        INFO("busy body cleanup discarded the owner before clear or expiry");
        REQUIRE(fixture.coordinator.diagnostics().body_owner_present);
        fixture.coordinator.control(Control::menu_suppressed, fixture.time);
        fixture.coordinator.tick(fixture.time);
        INFO("menu erased an outstanding mandatory clear");
        REQUIRE((fixture.output.body_submissions.size() == previous_submissions &&
                 fixture.output.body_clears.back().lease_id == old_lease));
        if (!expire) {
            fixture.output.body_clear_status = INPUT_NEXT_STATUS_OK;
        }
        fixture.time += expire ? 200ms : 20ms;
        auto paused = casting();
        paused.menus_clear = false;
        fixture.observe(paused);
        fixture.coordinator.tick(fixture.time);
        INFO("mandatory clear or accepted expiry did not admit a fresh neutral owner");
        REQUIRE((fixture.output.body_submissions.size() == previous_submissions + 1 &&
                 fixture.output.body_submissions.back().request.lease_id == 0 &&
                 fixture.output.body_submissions.back().request.sequence == 1 &&
                 fixture.output.active_body_lease != old_lease));
    }
}

TEST_CASE("skyrim.session.stale_menu_observation_cannot_cancel_a_failed_withdrawal",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(idle());
    fixture.coordinator.control(Control::menu_suppressed, fixture.time);
    fixture.coordinator.tick(fixture.time);
    const auto old_lease = fixture.output.active_body_lease;
    const auto previous_submissions = fixture.output.body_submissions.size();
    fixture.output.body_clear_status = INPUT_NEXT_STATUS_BUSY;
    fixture.time += 151ms;
    fixture.coordinator.tick(fixture.time);
    fixture.time += 10ms;
    auto paused = idle();
    paused.menus_clear = false;
    fixture.observe(paused);
    fixture.coordinator.tick(fixture.time);
    INFO("fresh menu observation cancelled withdrawal required by an earlier stale sample");
    REQUIRE((fixture.output.body_submissions.size() == previous_submissions &&
             fixture.output.body_clears.back().lease_id == old_lease));
    fixture.output.body_clear_status = INPUT_NEXT_STATUS_OK;
    fixture.time += 1ms;
    fixture.coordinator.tick(fixture.time);
    INFO("fresh menu session failed to reacquire after exact withdrawal succeeded");
    REQUIRE((fixture.output.active_body_lease != old_lease &&
             fixture.output.body_submissions.back().request.lease_id == 0));
}

TEST_CASE("skyrim.session.coordinator_fault_isolated_from_other_instance",
    "[component][skyrim][session]") {
    const auto first = std::make_unique<Fixture>();
    const auto second = std::make_unique<Fixture>();
    auto& first_fixture = *first;
    auto& second_fixture = *second;
    first_fixture.settle(casting());
    second_fixture.settle(idle());
    first_fixture.coordinator.fault();
    first_fixture.coordinator.tick(first_fixture.time);
    second_fixture.advance(casting());
    INFO("one coordinator changed an independent instance");
    REQUIRE(!second_fixture.output.submissions.empty());
    INFO("faulted coordinator admitted native task");
    REQUIRE(!first_fixture.coordinator.native_ticket());
}

TEST_CASE("skyrim.session.coordinator_invalid_slot_rejects_native_ticket",
    "[component][skyrim][session]") {
    FakeOutput output;
    const auto invalid = std::make_unique<Coordinator>(output, CoordinatorConfig{4});
    invalid->control(Control::data_loaded, MonoTime{1050ms});
    INFO("invalid slot was admitted");
    REQUIRE(!invalid->native_ticket());
}

} // namespace skyrim_adapter::tests::session
