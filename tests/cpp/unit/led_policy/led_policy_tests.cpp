#include <catch2/catch_test_macros.hpp>

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>

namespace {
using namespace std::chrono_literals;
using inputnext::adapters::LedHealthSample;
using inputnext::adapters::LedIntent;
using inputnext::adapters::LedMode;
using inputnext::adapters::LedObservation;
using inputnext::adapters::LedPolicy;
using inputnext::adapters::LedSettings;
using inputnext::adapters::LedStealthSample;
using inputnext::adapters::MonoTime;

template <typename T>
[[nodiscard]] const T& require_value(const std::optional<T>& value) {
    if (!value) {
        throw std::runtime_error("expected optional value in LED policy test");
    }
    return *value;
}

[[nodiscard]] MonoTime at(std::int64_t milliseconds) noexcept {
    return MonoTime{} + std::chrono::milliseconds{milliseconds};
}

[[nodiscard]] LedObservation sample(std::int64_t timestamp,
    std::optional<LedHealthSample> health = std::nullopt) noexcept {
    LedObservation result{};
    result.sampled_at = at(timestamp);
    result.subject_id = 1U;
    result.health = health;
    return result;
}

[[nodiscard]] std::optional<LedIntent>
update(LedPolicy& policy, const LedObservation& observation, std::int64_t now_ms) noexcept {
    return policy.update(observation, at(now_ms));
}

[[nodiscard]] std::uint32_t color(const std::optional<LedIntent>& intent) {
    REQUIRE(intent.has_value());
    return require_value(intent).payload.lightbar_rgb;
}

[[nodiscard]] std::optional<MonoTime> latest(std::span<const std::int64_t> timestamps) noexcept {
    if (timestamps.empty()) {
        return std::nullopt;
    }
    return at(*std::ranges::max_element(timestamps));
}

TEST_CASE("led_policy.validates authored bounds and disables invalid direct settings",
    "[unit][effects][led_policy]") {
    CHECK(inputnext::adapters::valid_led_settings(LedSettings{}));

    LedSettings invalid{};
    invalid.health_hold_ms = 0U;
    CHECK_FALSE(inputnext::adapters::valid_led_settings(invalid));
    invalid = {};
    invalid.health_hold_ms = 60001U;
    CHECK_FALSE(inputnext::adapters::valid_led_settings(invalid));
    invalid = {};
    invalid.healing_transition_ms = 1001U;
    CHECK_FALSE(inputnext::adapters::valid_led_settings(invalid));
    invalid = {};
    invalid.critical_enter = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(inputnext::adapters::valid_led_settings(invalid));
    invalid = {};
    invalid.critical_enter = 0.3F;
    invalid.critical_exit = 0.2F;
    CHECK_FALSE(inputnext::adapters::valid_led_settings(invalid));
    invalid = {};
    invalid.detection_hidden_score = 4;
    invalid.detection_alert_score = 4;
    CHECK_FALSE(inputnext::adapters::valid_led_settings(invalid));

    invalid = {};
    invalid.detection_hidden_score = std::numeric_limits<std::int32_t>::min();
    invalid.detection_alert_score = std::numeric_limits<std::int32_t>::max();
    CHECK(inputnext::adapters::valid_led_settings(invalid));

    invalid.health_hold_ms = 0U;
    LedPolicy policy{invalid};
    auto observation = sample(0, LedHealthSample{.current = 100.0F, .maximum = 100.0F});
    CHECK_FALSE(update(policy, observation, 0));
    observation = sample(100, LedHealthSample{.current = 99.0F, .maximum = 100.0F});
    CHECK_FALSE(update(policy, observation, 100));
    CHECK(policy.mode() == LedMode::none);

    LedPolicy unknown_subject;
    observation = sample(0, LedHealthSample{.current = 100.0F, .maximum = 100.0F});
    observation.subject_id = 0U;
    observation.radiation_sources = 1U;
    CHECK_FALSE(update(unknown_subject, observation, 0));
    observation = sample(100, LedHealthSample{.current = 99.0F, .maximum = 100.0F});
    observation.subject_id = 0U;
    CHECK_FALSE(update(unknown_subject, observation, 100));
}

TEST_CASE("led_policy.uses silent baselines exact changes and timed critical hysteresis",
    "[unit][effects][led_policy]") {
    LedPolicy policy;
    auto observation = sample(0, LedHealthSample{.current = 5.0F, .maximum = 100.0F});
    CHECK_FALSE(update(policy, observation, 0));
    CHECK(policy.mode() == LedMode::none);

    observation = sample(100, LedHealthSample{.current = 4.999F, .maximum = 100.0F});
    auto intent = update(policy, observation, 100);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::critical_health);
    CHECK((require_value(intent).payload.lightbar_rgb >> 16U) == 0xFFU);
    CHECK(require_value(intent).source_at == at(100));
    CHECK(require_value(intent).ttl_ms <= 200U);
    CHECK(require_value(intent).payload.player_index == 0U);
    CHECK(require_value(intent).payload.microphone_led == 0U);
    CHECK(require_value(intent).payload.reserved == 0U);

    observation = sample(200, LedHealthSample{.current = 22.0F, .maximum = 100.0F});
    intent = update(policy, observation, 200);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::critical_health);

    observation = sample(300, LedHealthSample{.current = 26.0F, .maximum = 100.0F});
    intent = update(policy, observation, 300);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::health);

    for (std::int64_t timestamp = 400; timestamp < 4300; timestamp += 100) {
        observation = sample(timestamp, LedHealthSample{.current = 26.0F, .maximum = 100.0F});
        intent = update(policy, observation, timestamp);
        REQUIRE(intent.has_value());
    }
    observation = sample(4300, LedHealthSample{.current = 26.0F, .maximum = 100.0F});
    CHECK_FALSE(update(policy, observation, 4300));
    CHECK(policy.mode() == LedMode::none);
}

TEST_CASE("led_policy.critical thresholds enter and exit inclusively",
    "[unit][effects][led_policy]") {
    LedSettings settings{};
    settings.healing_transition_ms = 0U;
    LedPolicy policy{settings};
    CHECK_FALSE(update(policy, sample(0, LedHealthSample{.current = 20.0F, .maximum = 100.0F}), 0));
    auto observation = sample(100, LedHealthSample{.current = 21.0F, .maximum = 100.0F});
    REQUIRE(update(policy, observation, 100));
    CHECK(policy.mode() == LedMode::critical_health);
    observation = sample(200, LedHealthSample{.current = 25.0F, .maximum = 100.0F});
    REQUIRE(update(policy, observation, 200));
    CHECK(policy.mode() == LedMode::health);
}

TEST_CASE("led_policy.max_changes clamp ratios and invalid recovery never invent activity",
    "[unit][effects][led_policy]") {
    LedSettings settings{};
    settings.healing_transition_ms = 0U;
    LedPolicy policy{settings};
    CHECK_FALSE(update(policy, sample(0, LedHealthSample{.current = 80.0F, .maximum = 100.0F}), 0));

    auto observation = sample(100, LedHealthSample{.current = 150.0F, .maximum = 200.0F});
    CHECK_FALSE(update(policy, observation, 100));
    CHECK(policy.mode() == LedMode::none);

    observation = sample(200, LedHealthSample{.current = 400.0F, .maximum = 200.0F});
    auto intent = update(policy, observation, 200);
    REQUIRE(intent.has_value());
    CHECK(require_value(intent).payload.lightbar_rgb == 0x00FF00U);

    observation = sample(300, LedHealthSample{.current = 200.0F, .maximum = 200.0F});
    intent = update(policy, observation, 300);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::health);
    CHECK(require_value(intent).payload.lightbar_rgb == 0x00FF00U);

    observation = sample(400);
    observation.radiation_sources = 2U;
    intent = update(policy, observation, 400);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::radiation);

    observation = sample(500, LedHealthSample{.current = 20.0F, .maximum = 100.0F});
    observation.radiation_sources = 0U;
    CHECK_FALSE(update(policy, observation, 500));
    CHECK(policy.mode() == LedMode::none);

    observation = sample(600,
        LedHealthSample{.current = std::numeric_limits<float>::infinity(), .maximum = 100.0F});
    CHECK_FALSE(update(policy, observation, 600));
    observation = sample(700, LedHealthSample{.current = 10.0F, .maximum = 100.0F});
    CHECK_FALSE(update(policy, observation, 700));
    CHECK(policy.mode() == LedMode::none);

    settings.health_hold_ms = 100U;
    LedPolicy active_max{settings};
    CHECK_FALSE(
        update(active_max, sample(0, LedHealthSample{.current = 80.0F, .maximum = 100.0F}), 0));
    observation = sample(100, LedHealthSample{.current = 79.0F, .maximum = 100.0F});
    intent = update(active_max, observation, 100);
    REQUIRE(intent.has_value());
    const std::uint32_t before_max_change = color(intent);
    observation = sample(150, LedHealthSample{.current = 79.0F, .maximum = 200.0F});
    intent = update(active_max, observation, 150);
    REQUIRE(intent.has_value());
    CHECK(active_max.mode() == LedMode::health);
    CHECK(color(intent) != before_max_change);
    observation = sample(200, LedHealthSample{.current = 79.0F, .maximum = 200.0F});
    CHECK_FALSE(update(active_max, observation, 200));
}

TEST_CASE("led_policy.subject and freshness boundaries require a new silent baseline",
    "[unit][effects][led_policy]") {
    LedPolicy policy;
    CHECK_FALSE(update(policy, sample(0, LedHealthSample{.current = 80.0F, .maximum = 100.0F}), 0));
    auto observation = sample(100, LedHealthSample{.current = 79.0F, .maximum = 100.0F});
    REQUIRE(update(policy, observation, 100));

    observation = sample(150, LedHealthSample{.current = 5.0F, .maximum = 100.0F});
    observation.subject_id = 2U;
    CHECK_FALSE(update(policy, observation, 150));
    observation = sample(200, LedHealthSample{.current = 5.0F, .maximum = 100.0F});
    observation.subject_id = 2U;
    CHECK_FALSE(update(policy, observation, 200));
    observation = sample(250, LedHealthSample{.current = 4.0F, .maximum = 100.0F});
    observation.subject_id = 2U;
    REQUIRE(update(policy, observation, 250));
    CHECK(policy.mode() == LedMode::critical_health);

    observation = sample(400, LedHealthSample{.current = 3.0F, .maximum = 100.0F});
    observation.subject_id = 2U;
    CHECK_FALSE(update(policy, observation, 400));
    CHECK(policy.mode() == LedMode::none);

    policy.reset();
    observation = sample(500, LedHealthSample{.current = 3.0F, .maximum = 100.0F});
    observation.subject_id = 2U;
    CHECK_FALSE(update(policy, observation, 500));
}

TEST_CASE("led_policy.freshness duplicates future and old timestamps cannot extend a cue",
    "[unit][effects][led_policy]") {
    LedPolicy policy;
    CHECK_FALSE(update(policy, sample(0, LedHealthSample{.current = 80.0F, .maximum = 100.0F}), 0));
    auto changed = sample(100, LedHealthSample{.current = 79.0F, .maximum = 100.0F});
    auto intent = update(policy, changed, 100);
    REQUIRE(intent.has_value());
    CHECK(require_value(intent).source_at == at(100));
    const std::uint32_t changed_color = color(intent);

    auto duplicate = sample(100, LedHealthSample{.current = 1.0F, .maximum = 100.0F});
    intent = update(policy, duplicate, 149);
    REQUIRE(intent.has_value());
    CHECK(require_value(intent).source_at == at(100));
    CHECK(require_value(intent).payload.lightbar_rgb == changed_color);

    auto future = sample(151, LedHealthSample{.current = 1.0F, .maximum = 100.0F});
    intent = update(policy, future, 150);
    REQUIRE(intent.has_value());
    CHECK(require_value(intent).source_at == at(100));

    auto old = sample(90, LedHealthSample{.current = 1.0F, .maximum = 100.0F});
    intent = update(policy, old, 150);
    REQUIRE(intent.has_value());
    CHECK(require_value(intent).source_at == at(100));

    CHECK_FALSE(update(policy, old, 250));
    CHECK(policy.mode() == LedMode::none);
}

TEST_CASE("led_policy.menu pause resumes only the remaining original health window",
    "[unit][effects][led_policy]") {
    LedPolicy policy;
    CHECK_FALSE(update(policy, sample(0, LedHealthSample{.current = 80.0F, .maximum = 100.0F}), 0));
    REQUIRE(update(policy, sample(100, LedHealthSample{.current = 79.0F, .maximum = 100.0F}), 100));

    policy.pause(at(200));
    auto resumed = sample(4000, LedHealthSample{.current = 79.0F, .maximum = 100.0F});
    const auto intent = update(policy, resumed, 4000);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::health);
    CHECK(require_value(intent).source_at == at(4000));
    CHECK(require_value(intent).ttl_ms == 100U);
    CHECK_FALSE(
        update(policy, sample(4100, LedHealthSample{.current = 79.0F, .maximum = 100.0F}), 4100));
    CHECK(policy.mode() == LedMode::none);
}

TEST_CASE("led_policy.menu expiry and changed health do not extend the old window",
    "[unit][effects][led_policy]") {
    LedPolicy expired;
    CHECK_FALSE(
        update(expired, sample(0, LedHealthSample{.current = 80.0F, .maximum = 100.0F}), 0));
    REQUIRE(
        update(expired, sample(100, LedHealthSample{.current = 79.0F, .maximum = 100.0F}), 100));
    expired.pause(at(200));
    CHECK_FALSE(
        update(expired, sample(4100, LedHealthSample{.current = 79.0F, .maximum = 100.0F}), 4100));
    CHECK(expired.mode() == LedMode::none);

    LedPolicy changed_health;
    CHECK_FALSE(
        update(changed_health, sample(0, LedHealthSample{.current = 80.0F, .maximum = 100.0F}), 0));
    REQUIRE(update(changed_health,
        sample(100, LedHealthSample{.current = 79.0F, .maximum = 100.0F}),
        100));
    changed_health.pause(at(200));
    const auto resumed = sample(4000, LedHealthSample{.current = 60.0F, .maximum = 100.0F});
    const auto intent = update(changed_health, resumed, 4000);
    REQUIRE(intent.has_value());
    CHECK(changed_health.mode() == LedMode::health);
    CHECK(require_value(intent).ttl_ms == 100U);
    CHECK_FALSE(update(changed_health,
        sample(4100, LedHealthSample{.current = 60.0F, .maximum = 100.0F}),
        4100));
}

TEST_CASE("led_policy.menu resume rejects stale future duplicate and different subjects",
    "[unit][effects][led_policy]") {
    const auto prepare_paused = [] {
        LedPolicy policy;
        static_cast<void>(
            update(policy, sample(0, LedHealthSample{.current = 80.0F, .maximum = 100.0F}), 0));
        static_cast<void>(
            update(policy, sample(100, LedHealthSample{.current = 79.0F, .maximum = 100.0F}), 100));
        policy.pause(at(200));
        return policy;
    };

    auto stale = prepare_paused();
    CHECK_FALSE(
        update(stale, sample(250, LedHealthSample{.current = 79.0F, .maximum = 100.0F}), 500));
    CHECK(stale.mode() == LedMode::none);

    auto future = prepare_paused();
    CHECK_FALSE(
        update(future, sample(301, LedHealthSample{.current = 79.0F, .maximum = 100.0F}), 300));
    CHECK(future.mode() == LedMode::none);

    auto duplicate = prepare_paused();
    CHECK_FALSE(
        update(duplicate, sample(200, LedHealthSample{.current = 79.0F, .maximum = 100.0F}), 250));
    CHECK(duplicate.mode() == LedMode::none);

    auto other_subject = prepare_paused();
    auto other = sample(300, LedHealthSample{.current = 79.0F, .maximum = 100.0F});
    other.subject_id = 2U;
    CHECK_FALSE(update(other_subject, other, 300));
    CHECK(other_subject.mode() == LedMode::none);
}

TEST_CASE("led_policy.menu pause clears pending hit correlation", "[unit][effects][led_policy]") {
    LedPolicy policy;
    CHECK_FALSE(update(policy, sample(0, LedHealthSample{.current = 50.0F, .maximum = 100.0F}), 0));
    auto incoming = sample(20, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    incoming.incoming_hit_at = at(20);
    CHECK_FALSE(update(policy, incoming, 20));

    policy.pause(at(30));
    CHECK_FALSE(
        update(policy, sample(50, LedHealthSample{.current = 50.0F, .maximum = 100.0F}), 50));
    const auto dropped_after_resume =
        sample(70, LedHealthSample{.current = 49.0F, .maximum = 100.0F});
    REQUIRE(update(policy, dropped_after_resume, 70));
    CHECK(policy.mode() == LedMode::health);
}

TEST_CASE("led_policy.confirmed_damage handles net_zero healing while hit evidence needs a drop",
    "[unit][effects][led_policy]") {
    LedPolicy confirmed;
    CHECK_FALSE(
        update(confirmed, sample(0, LedHealthSample{.current = 50.0F, .maximum = 100.0F}), 0));
    auto observation = sample(50, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    observation.confirmed_damage_at = at(40);
    auto intent = update(confirmed, observation, 50);
    REQUIRE(intent.has_value());
    CHECK(confirmed.mode() == LedMode::recent_damage);

    LedPolicy unknown_hit;
    CHECK_FALSE(
        update(unknown_hit, sample(0, LedHealthSample{.current = 50.0F, .maximum = 100.0F}), 0));
    observation = sample(50, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    observation.incoming_hit_at = at(40);
    CHECK_FALSE(update(unknown_hit, observation, 50));
    observation = sample(80, LedHealthSample{.current = 49.0F, .maximum = 100.0F});
    intent = update(unknown_hit, observation, 80);
    REQUIRE(intent.has_value());
    CHECK(unknown_hit.mode() == LedMode::recent_damage);

    LedPolicy delayed_hit;
    CHECK_FALSE(
        update(delayed_hit, sample(0, LedHealthSample{.current = 50.0F, .maximum = 100.0F}), 0));
    observation = sample(100, LedHealthSample{.current = 49.0F, .maximum = 100.0F});
    intent = update(delayed_hit, observation, 100);
    REQUIRE(intent.has_value());
    CHECK(delayed_hit.mode() == LedMode::health);
    observation = sample(120, LedHealthSample{.current = 49.0F, .maximum = 100.0F});
    observation.incoming_hit_at = at(90);
    intent = update(delayed_hit, observation, 120);
    REQUIRE(intent.has_value());
    CHECK(delayed_hit.mode() == LedMode::recent_damage);

    LedPolicy invalid_evidence;
    CHECK_FALSE(update(invalid_evidence,
        sample(0, LedHealthSample{.current = 50.0F, .maximum = 100.0F}),
        0));
    observation = sample(50, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    observation.confirmed_damage_at = at(51);
    CHECK_FALSE(update(invalid_evidence, observation, 50));
    observation = sample(100, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    observation.confirmed_damage_at = at(-50);
    CHECK_FALSE(update(invalid_evidence, observation, 100));

    LedPolicy single_match;
    CHECK_FALSE(
        update(single_match, sample(0, LedHealthSample{.current = 50.0F, .maximum = 100.0F}), 0));
    observation = sample(20, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    observation.incoming_hit_at = at(10);
    observation.radiation_sources = 1U;
    CHECK(update(single_match, observation, 20));
    observation = sample(30, LedHealthSample{.current = 49.0F, .maximum = 100.0F});
    observation.incoming_hit_at = at(10);
    observation.radiation_sources = 1U;
    REQUIRE(update(single_match, observation, 30));
    CHECK(single_match.mode() == LedMode::recent_damage);
    for (std::int64_t timestamp = 40; timestamp <= 3940; timestamp += 100) {
        observation = sample(timestamp, LedHealthSample{.current = 49.0F, .maximum = 100.0F});
        observation.incoming_hit_at = at(10);
        observation.radiation_sources = 1U;
        REQUIRE(update(single_match, observation, timestamp));
    }
    observation = sample(4035, LedHealthSample{.current = 49.0F, .maximum = 100.0F});
    observation.incoming_hit_at = at(10);
    observation.radiation_sources = 1U;
    REQUIRE(update(single_match, observation, 4035));
    CHECK(single_match.mode() == LedMode::radiation);

    LedPolicy old_evidence;
    CHECK_FALSE(
        update(old_evidence, sample(0, LedHealthSample{.current = 50.0F, .maximum = 100.0F}), 0));
    observation = sample(10, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    observation.confirmed_damage_at = at(1);
    REQUIRE(update(old_evidence, observation, 10));
    for (std::int64_t timestamp = 110; timestamp <= 4000; timestamp += 100) {
        observation = sample(timestamp, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
        observation.confirmed_damage_at = at(1);
        intent = update(old_evidence, observation, timestamp);
        REQUIRE(intent.has_value());
    }
    observation = sample(4010, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    observation.confirmed_damage_at = at(1);
    CHECK_FALSE(update(old_evidence, observation, 4010));
}

TEST_CASE("led_policy.arbitrates all live sources without replaying suppressed health",
    "[unit][effects][led_policy]") {
    LedPolicy policy;
    auto observation = sample(0, LedHealthSample{.current = 80.0F, .maximum = 100.0F});
    observation.stealth = LedStealthSample{.sneaking = true, .detection_score = -100};
    auto intent = update(policy, observation, 0);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::stealth);
    CHECK(color(intent) == 0x0000FFU);

    observation = sample(100, LedHealthSample{.current = 81.0F, .maximum = 100.0F});
    observation.in_combat = true;
    observation.stealth = LedStealthSample{.sneaking = true, .detection_score = -50};
    intent = update(policy, observation, 100);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::combat_health);

    observation = sample(150, LedHealthSample{.current = 81.0F, .maximum = 100.0F});
    observation.in_combat = true;
    observation.stealth = LedStealthSample{.sneaking = true, .detection_score = 0};
    observation.radiation_sources = 1U;
    intent = update(policy, observation, 150);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::radiation);

    observation = sample(200, LedHealthSample{.current = 82.0F, .maximum = 100.0F});
    observation.confirmed_damage_at = at(190);
    observation.radiation_sources = 5U;
    observation.in_combat = true;
    observation.stealth = LedStealthSample{.sneaking = true, .detection_score = 0};
    intent = update(policy, observation, 200);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::recent_damage);

    observation = sample(300, LedHealthSample{.current = 10.0F, .maximum = 100.0F});
    observation.radiation_sources = 1U;
    observation.in_combat = true;
    observation.stealth = LedStealthSample{.sneaking = true, .detection_score = 0};
    intent = update(policy, observation, 300);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::critical_health);

    for (std::int64_t timestamp = 400; timestamp <= 4300; timestamp += 100) {
        observation = sample(timestamp, LedHealthSample{.current = 10.0F, .maximum = 100.0F});
        observation.radiation_sources = 1U;
        observation.stealth = LedStealthSample{.sneaking = true, .detection_score = 0};
        intent = update(policy, observation, timestamp);
        REQUIRE(intent.has_value());
        if (timestamp < 4300) {
            CHECK(policy.mode() == LedMode::critical_health);
        }
    }
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::radiation);

    observation = sample(4301, LedHealthSample{.current = 10.0F, .maximum = 100.0F});
    observation.radiation_sources = 0U;
    observation.stealth = LedStealthSample{.sneaking = true, .detection_score = 0};
    intent = update(policy, observation, 4301);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::stealth);
}

TEST_CASE("led_policy.default stealth colors distinguish zero and positive detection",
    "[unit][effects][led_policy]") {
    LedPolicy policy;
    constexpr std::array<std::int32_t, 9> scores{-1000, 0, 25, 50, 75, 100, 1000, 50, 0};
    constexpr std::array<std::uint32_t, 9> colors{0x0000FFU,
        0x0000FFU,
        0x4000FFU,
        0x8000FFU,
        0xBF00FFU,
        0xFF00FFU,
        0xFF00FFU,
        0x8000FFU,
        0x0000FFU};
    for (std::size_t index = 0; index < scores.size(); ++index) {
        const auto timestamp = static_cast<std::int64_t>(index) * 50;
        auto observation = sample(timestamp);
        observation.stealth = LedStealthSample{.sneaking = true, .detection_score = scores[index]};
        const auto intent = update(policy, observation, timestamp);
        REQUIRE(intent.has_value());
        CHECK(policy.mode() == LedMode::stealth);
        CHECK(color(intent) == colors[index]);
    }
}

TEST_CASE("led_policy.renders radiation pulse and authored raw stealth endpoints",
    "[unit][effects][led_policy]") {
    LedPolicy radiation;
    auto observation = sample(0);
    observation.radiation_sources = 1U;
    auto intent = update(radiation, observation, 0);
    REQUIRE(intent.has_value());
    CHECK(radiation.mode() == LedMode::radiation);
    const auto dim_pulse = color(intent);

    observation = sample(800);
    observation.radiation_sources = 7U;
    intent = update(radiation, observation, 800);
    REQUIRE(intent.has_value());
    CHECK(radiation.mode() == LedMode::radiation);
    CHECK(color(intent) != dim_pulse);

    LedSettings settings{};
    settings.detection_hidden_score = -50;
    settings.detection_alert_score = 50;
    LedPolicy stealth{settings};
    observation = sample(0);
    observation.stealth = LedStealthSample{.sneaking = true, .detection_score = -500};
    intent = update(stealth, observation, 0);
    REQUIRE(intent.has_value());
    CHECK(color(intent) == 0x0000FFU);

    observation = sample(50);
    observation.stealth = LedStealthSample{.sneaking = true, .detection_score = 500};
    intent = update(stealth, observation, 50);
    REQUIRE(intent.has_value());
    CHECK(color(intent) == 0xFF00FFU);

    observation = sample(100);
    observation.stealth = LedStealthSample{.sneaking = false, .detection_score = 50};
    CHECK_FALSE(update(stealth, observation, 100));
    CHECK(stealth.mode() == LedMode::none);
}

TEST_CASE(
    "led_policy.healing ramps advance through duplicate and changing samples, drops interrupt",
    "[unit][effects][led_policy]") {
    LedPolicy policy;
    CHECK_FALSE(update(policy, sample(0, LedHealthSample{.current = 50.0F, .maximum = 100.0F}), 0));

    auto observation = sample(100, LedHealthSample{.current = 60.0F, .maximum = 100.0F});
    auto intent = update(policy, observation, 100);
    REQUIRE(intent.has_value());
    CHECK(color(intent) == 0xFFFF00U);

    observation = sample(100, LedHealthSample{.current = 90.0F, .maximum = 100.0F});
    intent = update(policy, observation, 150);
    REQUIRE(intent.has_value());
    CHECK(require_value(intent).source_at == at(100));
    CHECK(color(intent) == 0xF2FF00U);

    observation = sample(200, LedHealthSample{.current = 70.0F, .maximum = 100.0F});
    intent = update(policy, observation, 200);
    REQUIRE(intent.has_value());
    CHECK(color(intent) == 0xE6FF00U);

    observation = sample(250, LedHealthSample{.current = 80.0F, .maximum = 100.0F});
    intent = update(policy, observation, 250);
    REQUIRE(intent.has_value());
    CHECK(color(intent) != 0x00FF00U);

    observation = sample(300, LedHealthSample{.current = 40.0F, .maximum = 100.0F});
    intent = update(policy, observation, 300);
    REQUIRE(intent.has_value());
    CHECK(color(intent) == 0xFFCC00U);

    observation = sample(400, LedHealthSample{.current = 40.0F, .maximum = 100.0F});
    intent = update(policy, observation, 400);
    REQUIRE(intent.has_value());
    CHECK(color(intent) == 0xFFCC00U);

    LedPolicy hidden_ramp;
    observation = sample(0, LedHealthSample{.current = 50.0F, .maximum = 100.0F});
    observation.radiation_sources = 1U;
    REQUIRE(update(hidden_ramp, observation, 0));
    observation = sample(100, LedHealthSample{.current = 60.0F, .maximum = 100.0F});
    observation.radiation_sources = 1U;
    REQUIRE(update(hidden_ramp, observation, 100));
    CHECK(hidden_ramp.mode() == LedMode::radiation);
    observation = sample(200, LedHealthSample{.current = 60.0F, .maximum = 100.0F});
    observation.radiation_sources = 0U;
    intent = update(hidden_ramp, observation, 200);
    REQUIRE(intent.has_value());
    CHECK(hidden_ramp.mode() == LedMode::health);
    CHECK(color(intent) == 0xE6FF00U);
}

TEST_CASE("led_policy.bounds source-aged ttl saturates deadlines and resets on backwards time",
    "[unit][effects][led_policy]") {
    LedPolicy policy;
    CHECK_FALSE(
        update(policy, sample(1000, LedHealthSample{.current = 80.0F, .maximum = 100.0F}), 1000));
    auto observation = sample(1100, LedHealthSample{.current = 79.0F, .maximum = 100.0F});
    auto intent = update(policy, observation, 1200);
    REQUIRE(intent.has_value());
    CHECK(require_value(intent).source_at == at(1100));
    CHECK(require_value(intent).ttl_ms == 150U);
    CHECK_FALSE(update(policy, observation, 1250));

    LedPolicy saturated;
    const MonoTime near_end = MonoTime::max() - 10ms;
    LedObservation baseline{};
    baseline.sampled_at = near_end;
    baseline.subject_id = 3U;
    baseline.health = LedHealthSample{.current = 50.0F, .maximum = 100.0F};
    CHECK_FALSE(saturated.update(baseline, near_end));
    LedObservation damaged = baseline;
    damaged.sampled_at = MonoTime::max() - 5ms;
    damaged.health = LedHealthSample{.current = 49.0F, .maximum = 100.0F};
    intent = saturated.update(damaged, damaged.sampled_at);
    REQUIRE(intent.has_value());
    CHECK(require_value(intent).ttl_ms <= 150U);
    CHECK(saturated.update(damaged, MonoTime::max() - 1ms));
    CHECK_FALSE(saturated.update(damaged, MonoTime::max()));

    LedPolicy backwards;
    CHECK_FALSE(update(backwards,
        sample(1000, LedHealthSample{.current = 50.0F, .maximum = 100.0F}),
        1000));
    observation = sample(1100, LedHealthSample{.current = 49.0F, .maximum = 100.0F});
    REQUIRE(update(backwards, observation, 1100));
    CHECK_FALSE(update(backwards, observation, 1099));
    CHECK(backwards.mode() == LedMode::none);
    CHECK_FALSE(update(backwards,
        sample(1099, LedHealthSample{.current = 49.0F, .maximum = 100.0F}),
        1099));
}

TEST_CASE("led_policy.continuous regeneration has no total display cap and expires exactly",
    "[unit][effects][led_policy]") {
    LedPolicy policy;
    CHECK_FALSE(update(policy, sample(0, LedHealthSample{.current = 49.0F, .maximum = 100.0F}), 0));
    std::optional<LedIntent> intent;
    for (std::int64_t timestamp = 100; timestamp <= 9000; timestamp += 100) {
        const std::int64_t regeneration_step = timestamp / 1000;
        const float current = 50.0F + static_cast<float>(regeneration_step);
        intent = update(policy,
            sample(timestamp, LedHealthSample{.current = current, .maximum = 100.0F}),
            timestamp);
        REQUIRE(intent.has_value());
        CHECK(policy.mode() == LedMode::health);
    }

    // The last genuine change was at 9000 ms; the cue ends at its exact deadline.
    intent =
        update(policy, sample(9100, LedHealthSample{.current = 59.0F, .maximum = 100.0F}), 9100);
    REQUIRE(intent.has_value());
    CHECK(policy.mode() == LedMode::health);
    for (std::int64_t timestamp = 9200; timestamp <= 12900; timestamp += 100) {
        intent = update(policy,
            sample(timestamp, LedHealthSample{.current = 59.0F, .maximum = 100.0F}),
            timestamp);
        REQUIRE(intent.has_value());
    }
    intent =
        update(policy, sample(13000, LedHealthSample{.current = 59.0F, .maximum = 100.0F}), 13000);
    CHECK_FALSE(intent);
    CHECK(policy.mode() == LedMode::none);
}

TEST_CASE("led_policy.uses the collapsed maximum independent event timestamp",
    "[unit][effects][led_policy]") {
    constexpr std::array<std::int64_t, 3> first_order{80, 20, 60};
    constexpr std::array<std::int64_t, 3> second_order{60, 80, 20};
    const auto first_max = latest(first_order);
    const auto second_max = latest(second_order);
    REQUIRE(first_max.has_value());
    REQUIRE(second_max.has_value());
    CHECK(require_value(first_max) == require_value(second_max));

    LedPolicy left;
    LedPolicy right;
    const auto baseline = sample(0, LedHealthSample{.current = 100.0F, .maximum = 100.0F});
    CHECK_FALSE(update(left, baseline, 0));
    CHECK_FALSE(update(right, baseline, 0));

    auto observation = sample(100, LedHealthSample{.current = 100.0F, .maximum = 100.0F});
    observation.confirmed_damage_at = first_max;
    const auto first = update(left, observation, 100);
    observation.confirmed_damage_at = second_max;
    const auto second = update(right, observation, 100);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(left.mode() == LedMode::recent_damage);
    CHECK(right.mode() == LedMode::recent_damage);
    CHECK(require_value(first).payload.lightbar_rgb == require_value(second).payload.lightbar_rgb);
    CHECK(require_value(first).source_at == require_value(second).source_at);
    CHECK(require_value(first).ttl_ms == require_value(second).ttl_ms);
}

} // namespace
