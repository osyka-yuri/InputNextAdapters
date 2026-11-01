#include "support.hpp"

#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>

#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include <catch2/catch_test_macros.hpp>

namespace fallout4_adapter::policy_tests {
namespace {
using inputnext::adapters::LedHealthSample;

Observation led_sample(std::int64_t milliseconds,
    inputnext::adapters::LedHealthSample health) noexcept {
    auto result = gameplay_at(milliseconds);
    result.led.subject_id = 0x14U;
    result.led.health = health;
    result.led.sampled_at = at(milliseconds - 1);
    return result;
}

bool same_led(const std::optional<inputnext::adapters::LedIntent>& left,
    const std::optional<inputnext::adapters::LedIntent>& right) noexcept {
    return left.has_value() == right.has_value() &&
           (!left || (left->payload.lightbar_rgb == right->payload.lightbar_rgb &&
                         left->payload.player_index == right->payload.player_index &&
                         left->payload.microphone_led == right->payload.microphone_led &&
                         left->payload.reserved == right->payload.reserved &&
                         left->source_at == right->source_at && left->ttl_ms == right->ttl_ms));
}
} // namespace

TEST_CASE("fallout4.policy.led_baselines_max_hp_and_authoritative_sample_time",
    "[component][fallout4][policy][led]") {
    Policy policy;
    const auto first = led_sample(1000, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    REQUIRE(!policy.update(first, {}, at(1000)).leds);

    const auto maximum_only = led_sample(1040, LedHealthSample{.current = 40.0F, .maximum = 80.0F});
    REQUIRE(!policy.update(maximum_only, {}, at(1040)).leds);

    const auto changed = led_sample(1080, LedHealthSample{.current = 39.0F, .maximum = 80.0F});
    const auto frame = policy.update(changed, {}, at(1080));
    const auto led = frame.leds.value_or(inputnext::adapters::LedIntent{});
    REQUIRE(frame.leds);
    CHECK(led.source_at == changed.sampled_at);
    CHECK(led.source_at != changed.led.sampled_at);
    CHECK(led.ttl_ms > 0U);
    CHECK(led.ttl_ms <= 200U);
    CHECK(led.payload.player_index == 0U);
    CHECK(led.payload.microphone_led == 0U);
    CHECK(led.payload.reserved == 0U);
}

TEST_CASE("fallout4.policy.led_event_timestamp_collapse_is_order_independent",
    "[component][fallout4][policy][led]") {
    const auto forward = std::make_unique<Policy>();
    const auto reverse = std::make_unique<Policy>();
    const auto baseline = led_sample(2000, LedHealthSample{.current = 70.0F, .maximum = 100.0F});
    REQUIRE(!forward->update(baseline, {}, at(2000)).leds);
    REQUIRE(!reverse->update(baseline, {}, at(2000)).leds);

    auto first = event(EventKind::hit_received, at(2010), 1);
    first.health_damage_known = true;
    first.confirmed_positive_health_damage = true;
    auto second = event(EventKind::hit_received, at(2020), 2);
    second.health_damage_known = true;
    second.confirmed_positive_health_damage = true;
    const std::array forward_events{first, second};
    const std::array reverse_events{second, first};
    const auto unchanged = led_sample(2040, LedHealthSample{.current = 70.0F, .maximum = 100.0F});
    const auto forward_frame = forward->update(unchanged, forward_events, at(2040));
    const auto reverse_frame = reverse->update(unchanged, reverse_events, at(2040));
    const auto forward_led = forward_frame.leds.value_or(inputnext::adapters::LedIntent{});
    REQUIRE(forward_frame.leds);
    CHECK(same_led(forward_frame.leds, reverse_frame.leds));
    CHECK(forward_led.source_at == unchanged.sampled_at);
}

TEST_CASE("fallout4.policy.led_confirmed_positive_damage_is_separate_from_net_health_delta",
    "[component][fallout4][policy][led]") {
    const auto unknown_damage = std::make_unique<Policy>();
    const auto confirmed_damage = std::make_unique<Policy>();
    const auto baseline = led_sample(3000, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    REQUIRE(!unknown_damage->update(baseline, {}, at(3000)).leds);
    REQUIRE(!confirmed_damage->update(baseline, {}, at(3000)).leds);

    const auto unknown = event(EventKind::hit_received, at(3020), 1);
    auto positive = unknown;
    positive.health_damage_known = true;
    positive.confirmed_positive_health_damage = true;
    const auto unchanged = led_sample(3040, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    const std::array unknown_events{unknown};
    const std::array confirmed_events{positive};
    CHECK(!unknown_damage->update(unchanged, unknown_events, at(3040)).leds);
    CHECK(confirmed_damage->update(unchanged, confirmed_events, at(3040)).leds);
}

TEST_CASE("fallout4.policy.led_radiation_wins_over_ordinary_health_recovery",
    "[component][fallout4][policy][led]") {
    Policy policy;
    const auto baseline = led_sample(4000, LedHealthSample{.current = 80.0F, .maximum = 100.0F});
    REQUIRE(!policy.update(baseline, {}, at(4000)).leds);
    auto exposed = led_sample(4040, LedHealthSample{.current = 81.0F, .maximum = 100.0F});
    exposed.led.radiation_sources = 1U;
    const auto frame = policy.update(exposed, {}, at(4040));
    const auto led = frame.leds.value_or(inputnext::adapters::LedIntent{});
    REQUIRE(frame.leds);
    CHECK((led.payload.lightbar_rgb & 0x00FFFFU) == 0x00FF00U);
}

TEST_CASE("fallout4.policy.led_health_change_in_combat_takes_priority_over_sneaking",
    "[component][fallout4][policy][led]") {
    const auto policy = std::make_unique<Policy>();
    auto baseline = led_sample(5000, LedHealthSample{.current = 80.0F, .maximum = 100.0F});
    baseline.led.stealth = inputnext::adapters::LedStealthSample{
        .sneaking = false,
        .detection_score = -100,
    };
    REQUIRE(!policy->update(baseline, {}, at(5000)).leds);

    auto sneaking = led_sample(5040, LedHealthSample{.current = 80.0F, .maximum = 100.0F});
    sneaking.led.stealth = inputnext::adapters::LedStealthSample{
        .sneaking = true,
        .detection_score = -100,
    };
    const auto stealth_frame = policy->update(sneaking, {}, at(5040));
    const auto stealth_led = stealth_frame.leds.value_or(inputnext::adapters::LedIntent{});
    REQUIRE(stealth_frame.leds);
    CHECK(stealth_led.payload.lightbar_rgb == 0x0000FFU);

    auto combat_heal = led_sample(5080, LedHealthSample{.current = 81.0F, .maximum = 100.0F});
    combat_heal.led.in_combat = true;
    combat_heal.led.stealth = inputnext::adapters::LedStealthSample{
        .sneaking = true,
        .detection_score = -100,
    };
    const auto health_frame = policy->update(combat_heal, {}, at(5080));
    const auto health_led = health_frame.leds.value_or(inputnext::adapters::LedIntent{});
    REQUIRE(health_frame.leds);
    CHECK(health_led.payload.lightbar_rgb != 0x0000FFU);
    CHECK((health_led.payload.lightbar_rgb & 0x0000FFU) == 0U);
}

TEST_CASE("fallout4.policy.invalid_led_settings_disabled_on_policy_construction",
    "[component][fallout4][policy][led]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.leds.enabled = true;
    config->effects.leds.critical_enter = 0.50F;
    config->effects.leds.critical_exit = 0.20F;

    const auto policy = std::make_unique<Policy>(*config);

    const auto baseline = led_sample(1000, LedHealthSample{.current = 100.0F, .maximum = 100.0F});
    CHECK(!policy->update(baseline, {}, at(1000)).leds);

    const auto dropped = led_sample(1040, LedHealthSample{.current = 10.0F, .maximum = 100.0F});
    const auto frame = policy->update(dropped, {}, at(1040));
    CHECK(!frame.leds.has_value());
}
} // namespace fallout4_adapter::policy_tests
