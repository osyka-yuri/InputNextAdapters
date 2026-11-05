#include "support/policy_test_support.hpp"
#include "test_support.hpp"

#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <input_next_effects.h>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>
#include <memory>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>

namespace skyrim_adapter::tests::policy {
namespace {
using inputnext::adapters::LedHealthSample;
using inputnext::adapters::LedStealthSample;

const inputnext::adapters::LedIntent& led_intent(const EffectFrame& frame) {
    return skyrim_adapter::test::require_value(frame.leds);
}

void set_health(Observation& value, float current, float maximum = 100.0F) {
    value.led.subject_id = value.player_id;
    value.led.health = LedHealthSample{.current = current, .maximum = maximum};
}

bool same_led(const input_next_effects_led_v1& left, const input_next_effects_led_v1& right) {
    return left.lightbar_rgb == right.lightbar_rgb && left.player_index == right.player_index &&
           left.microphone_led == right.microphone_led && left.reserved == right.reserved;
}
} // namespace

TEST_CASE("skyrim.policy.led_health_uses_absolute_values_and_survives_body_resets",
    "[component][skyrim][policy][led]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->trigger_scale = 0.0F;
    config->rumble_scale = 0.0F;
    const auto policy_owner = std::make_unique<Policy>(*config);
    Policy& policy = *policy_owner;
    Observation sample = observation(at(100));
    set_health(sample, 100.0F);
    INFO("the first absolute health sample establishes a silent baseline");
    CHECK_FALSE(policy.step(sample, {}, at(100)).leds.has_value());

    sample.sampled_at = at(120);
    set_health(sample, 99.0F);
    sample.body_vibration_enabled = false;
    const EffectFrame changed = policy.step(sample, {}, at(120));
    INFO("one HP change is reported even when the game's body vibration preference is off");
    REQUIRE(changed.leds.has_value());
    CHECK(led_intent(changed).source_at == at(120));
    CHECK(led_intent(changed).ttl_ms > 0U);
    CHECK(led_intent(changed).ttl_ms <= 200U);
    CHECK(led_intent(changed).payload.player_index == 0U);
    CHECK(led_intent(changed).payload.microphone_led == 0U);
    CHECK(led_intent(changed).payload.reserved == 0U);
    CHECK(finite_legal(changed));

    policy.reset_body_history(at(120));
    sample.sampled_at = at(140);
    const EffectFrame after_body_reset = policy.step(sample, {}, at(140));
    INFO("a body-only reset leaves the independent health lease active");
    REQUIRE(after_body_reset.leds.has_value());
    CHECK(led_intent(after_body_reset).source_at == at(140));

    policy.reset();
    sample.sampled_at = at(160);
    INFO("the full policy reset makes the next current value a silent baseline");
    CHECK_FALSE(policy.step(sample, {}, at(160)).leds.has_value());
}

TEST_CASE("skyrim.policy.led_maximum_change_does_not_create_health_activity",
    "[component][skyrim][policy][led]") {
    const auto policy_owner = std::make_unique<Policy>();
    Policy& policy = *policy_owner;
    Observation sample = observation(at(100));
    set_health(sample, 80.0F, 100.0F);
    CHECK_FALSE(policy.step(sample, {}, at(100)).leds.has_value());

    sample.sampled_at = at(120);
    set_health(sample, 80.0F, 200.0F);
    INFO("a gameplay maximum change rebaselines instead of looking like healing");
    CHECK_FALSE(policy.step(sample, {}, at(120)).leds.has_value());
}

TEST_CASE("skyrim.policy.led_invalid_observation_discards_old_health_activity",
    "[component][skyrim][policy][led]") {
    const auto policy_owner = std::make_unique<Policy>();
    Policy& policy = *policy_owner;
    Observation sample = observation(at(100));
    set_health(sample, 70.0F);
    CHECK_FALSE(policy.step(sample, {}, at(100)).leds.has_value());
    sample.sampled_at = at(120);
    set_health(sample, 69.0F);
    REQUIRE(policy.step(sample, {}, at(120)).leds.has_value());

    sample.sampled_at = at(130);
    sample.alive = false;
    INFO("an invalid observation removes the previous LED deadline");
    CHECK_FALSE(policy.step(sample, {}, at(130)).leds.has_value());

    sample.sampled_at = at(140);
    sample.alive = true;
    INFO("the first valid state after recovery is a silent health baseline");
    CHECK_FALSE(policy.step(sample, {}, at(140)).leds.has_value());
}

TEST_CASE("skyrim.policy.led_settings_flow_through_effect_configuration",
    "[component][skyrim][policy][led]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.leds.enabled = false;
    const auto policy_owner = std::make_unique<Policy>(*config);
    Policy& policy = *policy_owner;
    Observation sample = observation(at(100));
    set_health(sample, 80.0F);
    CHECK_FALSE(policy.step(sample, {}, at(100)).leds.has_value());
    sample.sampled_at = at(120);
    set_health(sample, 79.0F);
    INFO("the independent LED enable switch suppresses output through PolicyConfig");
    CHECK_FALSE(policy.step(sample, {}, at(120)).leds.has_value());
}

TEST_CASE("skyrim.policy.led_incoming_hit_batch_is_order_independent",
    "[component][skyrim][policy][led]") {
    const auto forward_owner = std::make_unique<Policy>();
    const auto reverse_owner = std::make_unique<Policy>();
    Policy& forward_policy = *forward_owner;
    Policy& reverse_policy = *reverse_owner;
    Observation baseline = observation(at(100));
    set_health(baseline, 60.0F);
    CHECK_FALSE(forward_policy.step(baseline, {}, at(100)).leds.has_value());
    CHECK_FALSE(reverse_policy.step(baseline, {}, at(100)).leds.has_value());

    Observation changed = baseline;
    changed.sampled_at = at(200);
    set_health(changed, 59.0F);
    changed.led.stealth = LedStealthSample{.sneaking = true, .detection_score = -40};
    Action earlier = action(EventKind::hit, Hand::unknown, at(150), 701);
    earlier.incoming = true;
    Action later = action(EventKind::hit, Hand::unknown, at(180), 702);
    later.incoming = true;
    const std::array forward_events{earlier, later};
    const std::array reverse_events{later, earlier};

    const EffectFrame forward = forward_policy.step(changed, forward_events, at(200));
    const EffectFrame reverse = reverse_policy.step(changed, reverse_events, at(200));
    INFO("Skyrim reports an attributed hit but not independently confirmed positive damage");
    REQUIRE(forward.leds.has_value());
    REQUIRE(reverse.leds.has_value());
    CHECK(same_led(led_intent(forward).payload, led_intent(reverse).payload));
    CHECK(led_intent(forward).source_at == led_intent(reverse).source_at);
    CHECK(led_intent(forward).ttl_ms == led_intent(reverse).ttl_ms);

    INFO("the incoming hit contributes only its original timestamp, never the render timestamp");
    CHECK(led_intent(forward).source_at == at(200));
}

TEST_CASE("skyrim.policy.led_unknown_hit_does_not_assert_positive_damage",
    "[component][skyrim][policy][led]") {
    const auto policy_owner = std::make_unique<Policy>();
    Policy& policy = *policy_owner;
    Observation sample = observation(at(100));
    set_health(sample, 60.0F);
    CHECK_FALSE(policy.step(sample, {}, at(100)).leds.has_value());

    sample.sampled_at = at(120);
    Action hit = action(EventKind::hit, Hand::unknown, at(115), 703);
    hit.incoming = true;
    const std::array events{hit};
    INFO("an incoming hit with no observed HP change does not create a health window");
    CHECK_FALSE(policy.step(sample, events, at(120)).leds.has_value());
}

TEST_CASE("skyrim.policy.led_small_environmental_change_reaches_critical_health",
    "[component][skyrim][policy][led]") {
    const auto policy_owner = std::make_unique<Policy>();
    Policy& policy = *policy_owner;
    Observation sample = observation(at(100));
    set_health(sample, 5.5F);
    sample.led.stealth = LedStealthSample{.sneaking = true, .detection_score = -40};
    const EffectFrame baseline_stealth = policy.step(sample, {}, at(100));
    REQUIRE(baseline_stealth.leds.has_value());

    sample.sampled_at = at(120);
    set_health(sample, 5.0F);
    const EffectFrame changed = policy.step(sample, {}, at(120));
    INFO("a small environment-driven decrease activates LED health without a hit classification");
    REQUIRE(changed.leds.has_value());
    CHECK(led_intent(changed).source_at == at(120));
    CHECK(led_intent(changed).payload.lightbar_rgb !=
          led_intent(baseline_stealth).payload.lightbar_rgb);
}

TEST_CASE("skyrim.policy.led_combat_health_and_sneak_follow_priority",
    "[component][skyrim][policy][led]") {
    const auto combat_owner = std::make_unique<Policy>();
    Policy& combat_policy = *combat_owner;
    Observation combat = observation(at(100));
    set_health(combat, 60.0F);
    combat.in_combat = true;
    combat.led.in_combat = true;
    combat.led.stealth = LedStealthSample{.sneaking = true, .detection_score = -40};
    const EffectFrame sneaking_in_combat = combat_policy.step(combat, {}, at(100));
    REQUIRE(sneaking_in_combat.leds.has_value());

    combat.sampled_at = at(120);
    set_health(combat, 59.0F);
    const EffectFrame combat_health = combat_policy.step(combat, {}, at(120));
    REQUIRE(combat_health.leds.has_value());
    INFO("a genuine health change in combat outranks simultaneous sneaking detection");
    CHECK(led_intent(combat_health).payload.lightbar_rgb !=
          led_intent(sneaking_in_combat).payload.lightbar_rgb);

    const auto recovery_owner = std::make_unique<Policy>();
    Policy& recovery_policy = *recovery_owner;
    Observation recovery = observation(at(100));
    set_health(recovery, 60.0F);
    recovery.led.stealth = LedStealthSample{.sneaking = true, .detection_score = -40};
    const EffectFrame stealth = recovery_policy.step(recovery, {}, at(100));
    REQUIRE(stealth.leds.has_value());
    recovery.sampled_at = at(120);
    set_health(recovery, 61.0F);
    const EffectFrame recovering_while_sneaking = recovery_policy.step(recovery, {}, at(120));
    REQUIRE(recovering_while_sneaking.leds.has_value());
    INFO("outside combat, stealth keeps priority while the hidden health value continues updating");
    CHECK(same_led(led_intent(recovering_while_sneaking).payload, led_intent(stealth).payload));

    recovery.sampled_at = at(140);
    recovery.led.stealth.reset();
    const EffectFrame recovered_health = recovery_policy.step(recovery, {}, at(140));
    REQUIRE(recovered_health.leds.has_value());
    INFO("leaving sneak reveals the still-active health change using the latest current HP");
    CHECK(led_intent(recovered_health).payload.lightbar_rgb !=
          led_intent(stealth).payload.lightbar_rgb);
}

TEST_CASE("skyrim.policy.invalid_led_settings_disabled_on_policy_construction",
    "[component][skyrim][policy][led]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.leds.enabled = true;
    config->effects.leds.critical_enter = 0.50F;
    config->effects.leds.critical_exit = 0.20F;

    const auto policy_owner = std::make_unique<Policy>(*config);
    Policy& policy = *policy_owner;

    Observation sample = observation(at(100));
    set_health(sample, 100.0F);
    CHECK_FALSE(policy.step(sample, {}, at(100)).leds.has_value());

    sample.sampled_at = at(120);
    set_health(sample, 10.0F);
    const EffectFrame dropped = policy.step(sample, {}, at(120));
    CHECK_FALSE(dropped.leds.has_value());
}
} // namespace skyrim_adapter::tests::policy
