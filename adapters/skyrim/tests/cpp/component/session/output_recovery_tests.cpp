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
#include <span>

namespace skyrim_adapter::tests::session {
namespace {

using namespace std::chrono_literals;

} // namespace

TEST_CASE(
    "skyrim.session.coordinator_target_busy_retains_one_action_until_same_observation_recovers",
    "[component][skyrim][session]") {
    const auto expected_fixture = std::make_unique<Fixture>();
    auto& expected = *expected_fixture;
    const auto actual_fixture = std::make_unique<Fixture>();
    auto& actual = *actual_fixture;
    advertise_hd(expected.output, 917);
    advertise_hd(actual.output, 918);
    expected.settle(idle());
    actual.settle(idle());
    expected.time += 10ms;
    actual.time += 10ms;

    auto action = spell_release(Hand::left, expected.time, 1, SpellKind::fire);
    expected.observe(idle(), std::span{&action, 1});
    actual.observe(idle(), std::span{&action, 1});
    const auto trigger_count = actual.output.submissions.size();
    const auto body_count = actual.output.body_submissions.size();
    const auto clear_count = actual.output.clears.size();
    const auto body_clear_count = actual.output.body_clears.size();

    actual.output.target_status = INPUT_NEXT_STATUS_BUSY;
    actual.coordinator.tick(actual.time);
    INFO("target BUSY is diagnosed");
    REQUIRE(actual.coordinator.diagnostics().last_status == INPUT_NEXT_STATUS_BUSY);
    INFO("a BUSY target query stops before output delivery or mutation");
    REQUIRE((actual.output.submissions.size() == trigger_count &&
             actual.output.body_submissions.size() == body_count &&
             actual.output.clears.size() == clear_count &&
             actual.output.body_clears.size() == body_clear_count));

    actual.output.target_status = INPUT_NEXT_STATUS_OK;
    expected.coordinator.tick(expected.time);
    actual.coordinator.tick(actual.time);
    INFO("the retained action is delivered after the same observation recovers");
    REQUIRE((actual.output.body_submissions.back().request.pcm_command ==
                 INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             channel_peak(actual.output.body_submissions.back().samples, 0) > 0.0F));
    expected.time += 1ms;
    actual.time += 1ms;
    expected.coordinator.tick(expected.time);
    actual.coordinator.tick(actual.time);
    INFO("recovery matches one uninterrupted action without consuming it twice");
    REQUIRE((actual.output.body_submissions.size() == expected.output.body_submissions.size() &&
             actual.output.body_submissions.back().samples ==
                 expected.output.body_submissions.back().samples &&
             actual.output.body_submissions.back().request.sequence ==
                 expected.output.body_submissions.back().request.sequence));
}

TEST_CASE("skyrim.session.coordinator_trigger_busy_retries_with_current_observation",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(idle());
    fixture.time += 10ms;
    Observation blocking = idle();
    blocking.blocking = true;
    blocking.hands[1] = {
        .equipped_id = 52,
        .caster_source_id = 0,
        .equipment = EquipmentKind::weapon,
        .weapon = WeaponClass::sword,
    };
    fixture.observe(blocking);
    const auto queries_before = fixture.output.target_queries;
    const auto submissions_before = fixture.output.submissions.size();

    fixture.output.submit_status = INPUT_NEXT_STATUS_BUSY;
    fixture.coordinator.tick(fixture.time);
    const auto submissions_after_busy = fixture.output.submissions.size();
    INFO("trigger BUSY is reached after one successful target query");
    REQUIRE(fixture.output.target_queries == queries_before + 1);
    INFO("trigger BUSY reaches a submit attempt");
    REQUIRE(submissions_after_busy > submissions_before);
    INFO("trigger BUSY remains the delivery diagnostic");
    REQUIRE(fixture.coordinator.diagnostics().last_status == INPUT_NEXT_STATUS_BUSY);

    fixture.output.submit_status = INPUT_NEXT_STATUS_OK;
    fixture.coordinator.tick(fixture.time + 1ms);
    INFO("the same fresh observation retries trigger delivery without a forced rebind");
    REQUIRE((fixture.output.target_queries == queries_before + 2 &&
             fixture.output.submissions.size() > submissions_after_busy &&
             same_device_target(fixture.output.submissions.back().target, fixture.output.target)));
}

TEST_CASE("skyrim.session.coordinator_target_change_after_busy_requires_new_baseline",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(idle());
    fixture.time += 10ms;
    auto action = spell_release(Hand::left, fixture.time, 1, SpellKind::fire);
    fixture.observe(idle(), std::span{&action, 1});

    fixture.output.target_status = INPUT_NEXT_STATUS_BUSY;
    fixture.coordinator.tick(fixture.time);
    fixture.output.target_status = INPUT_NEXT_STATUS_OK;
    ++fixture.output.target.connection_id;
    fixture.time += 1ms;
    fixture.coordinator.tick(fixture.time);

    fixture.time += 1ms;
    fixture.observe(idle());
    fixture.coordinator.tick(fixture.time);
    const auto& baseline = fixture.output.body_submissions.back();
    INFO("a replacement target receives a fresh silent baseline without the old queued action");
    REQUIRE((same_device_target(baseline.request.target, fixture.output.target) &&
             baseline.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             baseline.request.fallback_rumble.low_frequency == 0.0F &&
             baseline.request.fallback_rumble.high_frequency == 0.0F));
}

TEST_CASE("skyrim.session.coordinator_busy_body_clear_keeps_mandatory_withdrawal_pending",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    const auto lease = fixture.output.active_body_lease;
    INFO("withdrawal fixture did not acquire a body owner");
    REQUIRE(lease != 0);
    fixture.coordinator.control(Control::focus_lost, fixture.time);
    fixture.output.body_clear_status = INPUT_NEXT_STATUS_BUSY;

    fixture.coordinator.tick(fixture.time);
    INFO("BUSY keeps the mandatory clear obligation and exact owner");
    REQUIRE((fixture.coordinator.diagnostics().body_owner_present &&
             !fixture.output.body_clears.empty() &&
             fixture.output.body_clears.back().lease_id == lease));
    const auto first_attempt_count = fixture.output.body_clears.size();
    fixture.coordinator.tick(fixture.time + 1ms);
    INFO("withdrawal remains pending while the exact owner clear stays BUSY");
    REQUIRE((fixture.coordinator.diagnostics().body_owner_present &&
             fixture.output.body_clears.size() > first_attempt_count &&
             fixture.output.body_clears.back().lease_id == lease));

    fixture.output.body_clear_status = INPUT_NEXT_STATUS_OK;
    fixture.coordinator.tick(fixture.time + 2ms);
    INFO("successful retry settles the pending body owner");
    REQUIRE((!fixture.coordinator.diagnostics().body_owner_present &&
             fixture.output.body_clears.back().lease_id == lease));
}

} // namespace skyrim_adapter::tests::session
