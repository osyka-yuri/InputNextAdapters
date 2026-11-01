#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/effect_settings.hpp>
#include <fallout4_adapter/model.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {
void require(bool condition, std::string_view message) {
    INFO(message);
    REQUIRE(condition);
}

bool same_config(const fallout4_adapter::Config& left,
    const fallout4_adapter::Config& right) noexcept {
    if (left.slot != right.slot || left.diagnostics != right.diagnostics ||
        left.policy.enabled != right.policy.enabled ||
        left.policy.trigger_scale != right.policy.trigger_scale ||
        left.policy.body_scale != right.policy.body_scale ||
        left.policy.resource_cues != right.policy.resource_cues ||
        left.policy.ui_cues != right.policy.ui_cues ||
        left.policy.effects.body_enabled != right.policy.effects.body_enabled ||
        left.policy.effects.triggers_enabled != right.policy.effects.triggers_enabled ||
        left.policy.effects.leds.enabled != right.policy.effects.leds.enabled ||
        left.policy.effects.leds.health_enabled != right.policy.effects.leds.health_enabled ||
        left.policy.effects.leds.stealth_enabled != right.policy.effects.leds.stealth_enabled ||
        left.policy.effects.leds.radiation_enabled != right.policy.effects.leds.radiation_enabled ||
        left.policy.effects.leds.health_hold_ms != right.policy.effects.leds.health_hold_ms ||
        left.policy.effects.leds.healing_transition_ms !=
            right.policy.effects.leds.healing_transition_ms ||
        left.policy.effects.leds.critical_enter != right.policy.effects.leds.critical_enter ||
        left.policy.effects.leds.critical_exit != right.policy.effects.leds.critical_exit ||
        left.policy.effects.leds.detection_hidden_score !=
            right.policy.effects.leds.detection_hidden_score ||
        left.policy.effects.leds.detection_alert_score !=
            right.policy.effects.leds.detection_alert_score) {
        return false;
    }
    for (std::size_t index{}; index < left.policy.effects.body.size(); ++index) {
        const auto& body_left = left.policy.effects.body[index];
        const auto& body_right = right.policy.effects.body[index];
        if (body_left.enabled != body_right.enabled || body_left.hd_scale != body_right.hd_scale ||
            body_left.classic_scale != body_right.classic_scale ||
            body_left.duration_scale != body_right.duration_scale ||
            body_left.frequency_hz != body_right.frequency_hz) {
            return false;
        }
    }
    for (std::size_t index{}; index < left.policy.effects.triggers.size(); ++index) {
        const auto& trigger_left = left.policy.effects.triggers[index];
        const auto& trigger_right = right.policy.effects.triggers[index];
        if (trigger_left.enabled != trigger_right.enabled ||
            trigger_left.strength_scale != trigger_right.strength_scale ||
            trigger_left.resistance_start != trigger_right.resistance_start ||
            trigger_left.vibration_frequency_code != trigger_right.vibration_frequency_code) {
            return false;
        }
    }
    return true;
}

std::optional<bool> profile_enabled(const fallout4_adapter::Config& config,
    std::string_view section) noexcept {
    using namespace fallout4_adapter;
    for (const auto& descriptor : body_effect_descriptors) {
        if (descriptor.section == section) {
            const auto index = static_cast<std::size_t>(descriptor.effect);
            return config.policy.effects.body[index].enabled;
        }
    }
    return std::nullopt;
}
} // namespace

TEST_CASE("fallout4.config.partial_settings_and_default_step_profiles",
    "[unit][fallout4][config]") {
    using namespace fallout4_adapter;
    Config config{};
    ConfigError error{};
    require(parse_config("[General]\nSlot=2\nEnabled=false\n[Effects]\nBodyScale=0.4\n",
                config,
                &error) == ConfigStatus::loaded,
        "omitted settings use built-in defaults");
    require(config.slot == 2 && !config.policy.enabled && config.policy.body_scale == 0.4F &&
                config.policy.effects.body_enabled && config.policy.effects.triggers_enabled,
        "explicit settings parse and omitted global controls retain defaults");
    require(config.policy.effects.leds.detection_hidden_score == 0 &&
                config.policy.effects.leds.detection_alert_score == 100,
        "omitted LED overrides retain the default detection color range");
    require(profile_enabled(config, "Body.OrdinaryStep") == false &&
                profile_enabled(config, "Body.SprintStep") == true &&
                profile_enabled(config, "Body.PowerArmorStep") == true,
        "ordinary footfalls default off while sprint and power-armor steps default on");
}

TEST_CASE("fallout4.config.led_group_defaults_bounds_and_cross_field_validation",
    "[unit][fallout4][config]") {
    using namespace fallout4_adapter;
    const auto first_owner = std::make_unique<Config>();
    const auto second_owner = std::make_unique<Config>();
    const auto snapshot_owner = std::make_unique<Config>();
    Config& first = *first_owner;
    Config& second = *second_owner;
    Config& snapshot = *snapshot_owner;
    ConfigError error{};
    require(parse_config("[LED]\nCriticalEnter=0.3\nCriticalExit=0.35\n"
                         "DetectionHiddenScore=-120\nDetectionAlertScore=10\n"
                         "Enabled=true\nHealthEnabled=false\nStealthEnabled=true\n"
                         "RadiationEnabled=false\nHealthHoldMs=60000\nHealingTransitionMs=0\n",
                first,
                &error) == ConfigStatus::loaded,
        "LED settings accept documented boundaries and order-independent thresholds");
    require(parse_config("[LED]\nHealingTransitionMs=0\nRadiationEnabled=false\n"
                         "HealthHoldMs=60000\nStealthEnabled=true\nHealthEnabled=false\n"
                         "Enabled=true\nDetectionAlertScore=10\nCriticalExit=0.35\n"
                         "DetectionHiddenScore=-120\nCriticalEnter=0.3\n",
                second,
                &error) == ConfigStatus::loaded,
        "LED thresholds validate after the full group regardless of key order");
    require(same_config(first, second), "equivalent LED groups produce the same configuration");
    require(first.policy.effects.leds.enabled && !first.policy.effects.leds.health_enabled &&
                first.policy.effects.leds.stealth_enabled &&
                !first.policy.effects.leds.radiation_enabled &&
                first.policy.effects.leds.health_hold_ms == 60000U &&
                first.policy.effects.leds.healing_transition_ms == 0U &&
                first.policy.effects.leds.critical_enter == 0.3F &&
                first.policy.effects.leds.critical_exit == 0.35F &&
                first.policy.effects.leds.detection_hidden_score == -120 &&
                first.policy.effects.leds.detection_alert_score == 10,
        "all LED values map to their shared policy fields");

    snapshot = first;
    require(parse_config("[LED]\nCriticalEnter=0.4\nCriticalExit=0.3\n", first, &error) ==
                    ConfigStatus::invalid &&
                same_config(first, snapshot),
        "cross-field critical ordering errors roll back the entire configuration");
    require(error.line == 3, "cross-field error points to the last related setting");
    require(parse_config("[LED]\nDetectionAlertScore=-100\nDetectionHiddenScore=-100\n",
                first,
                &error) == ConfigStatus::invalid,
        "detection endpoints must be strictly ordered");

    constexpr std::array invalid_led_values{
        "[LED]\nHealthHoldMs=0\n",
        "[LED]\nHealthHoldMs=60001\n",
        "[LED]\nHealingTransitionMs=1001\n",
        "[LED]\nCriticalEnter=nan\n",
        "[LED]\nCriticalExit=1.01\n",
        "[LED]\nDetectionHiddenScore=2147483648\n",
        "[LED]\nRadiationEnabled=yes\n",
        "[LED]\nUnknown=true\n",
    };
    for (const auto* input : invalid_led_values) {
        snapshot = first;
        require(parse_config(input, first, &error) == ConfigStatus::invalid &&
                    same_config(first, snapshot),
            "invalid LED values preserve transactional parsing");
    }
}

TEST_CASE("fallout4.config.transactional_parse_on_error_and_structure_validation",
    "[unit][fallout4][config]") {
    using namespace fallout4_adapter;
    Config config{};
    ConfigError error{};
    require(parse_config("[General]\nSlot=2\nEnabled=false\n[Effects]\nBodyScale=0.4\n",
                config,
                &error) == ConfigStatus::loaded,
        "baseline valid configuration parses");

    const auto unchanged_on_error = [&](std::string_view input, const char* message) {
        const auto before = config;
        require(parse_config(input, config, &error) == ConfigStatus::invalid, message);
        require(same_config(config, before),
            "invalid configuration leaves every caller setting unchanged");
    };

    unchanged_on_error("[General]\nSlot=1\n[Effects]\nBodyScale=nan\n",
        "nonfinite global value rejected");
    require(error.line == 4, "invalid values report their source line");
    unchanged_on_error("[General]\nSlot=4\n", "invalid slot rejected");
    unchanged_on_error("[Effects]\nBodyScale=1.01\n", "body scale range enforced");
    unchanged_on_error("[Effects]\nBodyScale=0.2\n[Effects]\nBodyScale=0.3\n",
        "repeated global key rejected");
    unchanged_on_error("[Body.RifleShot]\nHDScale=1\n[Body.RifleShot]\nHDScale=2\n",
        "repeated profile key rejected");
    unchanged_on_error("[Body.RifleShot]\nStrengthScale=1\n", "wrong body-profile key rejected");
    unchanged_on_error("[Trigger.Weapon.Rifle]\nHDScale=1\n", "wrong trigger-profile key rejected");
    unchanged_on_error("[Body.Unknown]\nEnabled=true\n", "unknown profile rejected");
    unchanged_on_error("[Effects]\nBodyScael=0.2\n", "unknown key rejected");
    unchanged_on_error("[Other]\n", "unknown section rejected");

    require(
        parse_config(
            "[General]\nSlot=2\n[General]\nEnabled=false\n[Effects]\nTriggerScale=0.25\n[Effects]\n"
            "BodyScale=0.4\nBodyEnabled=false\nTriggersEnabled=false\n"
            "[Logging]\nDiagnostics=true\n[Body.RifleShot]\nHDScale=0.5\n[Body.RifleShot]\n"
            "ClassicScale=1.5\n[Trigger.Weapon.Rifle]\nStrengthScale=1.25\n",
            config,
            &error) == ConfigStatus::loaded &&
            config.slot == 2 && !config.policy.enabled && config.policy.trigger_scale == 0.25F &&
            config.policy.body_scale == 0.4F && config.diagnostics &&
            !config.policy.effects.body_enabled && !config.policy.effects.triggers_enabled &&
            config.policy.effects.body[static_cast<std::size_t>(BodyEffect::rifle_shot)].hd_scale ==
                0.5F &&
            config.policy.effects.body[static_cast<std::size_t>(BodyEffect::rifle_shot)]
                    .classic_scale == 1.5F &&
            config.policy.effects.triggers[static_cast<std::size_t>(TriggerProfile::rifle)]
                    .strength_scale == 1.25F,
        "repeated sections accept distinct global and profile keys");
    unchanged_on_error("[General]\nSlot=2\n[General]\nSlot=3\n",
        "repeated global sections reject duplicate keys");
}

TEST_CASE("fallout4.config.profile_boundary_values_and_rejections", "[unit][fallout4][config]") {
    using namespace fallout4_adapter;
    Config config{};
    ConfigError error{};
    require(
        parse_config(
            "[Body.PistolShot]\nHDScale=0\nClassicScale=2\nDurationScale=0.25\nFrequencyHz=1000\n"
            "[Trigger.Weapon.Launcher]\nStrengthScale=2\nResistanceStart=1\nVibrationFrequencyCode="
            "255\n",
            config,
            &error) == ConfigStatus::loaded,
        "documented adjustment range endpoints are accepted");
    const auto& body_boundary =
        config.policy.effects.body[static_cast<std::size_t>(BodyEffect::pistol_shot)];
    const auto& trigger_boundary =
        config.policy.effects.triggers[static_cast<std::size_t>(TriggerProfile::launcher)];
    require(body_boundary.hd_scale == 0.0F && body_boundary.classic_scale == 2.0F &&
                body_boundary.duration_scale == 0.25F && body_boundary.frequency_hz == 1000.0F &&
                trigger_boundary.strength_scale == 2.0F &&
                trigger_boundary.resistance_start == 1.0F &&
                trigger_boundary.vibration_frequency_code == 255,
        "profile adjustment endpoints are retained");

    const auto unchanged_on_error = [&](std::string_view input, const char* message) {
        const auto before = config;
        require(parse_config(input, config, &error) == ConfigStatus::invalid, message);
        require(same_config(config, before),
            "invalid configuration leaves every caller setting unchanged");
    };

    constexpr std::array invalid_profile_values{
        "[Body.PistolShot]\nEnabled=maybe\n",
        "[Body.PistolShot]\nHDScale=-0.01\n",
        "[Body.PistolShot]\nHDScale=2.01\n",
        "[Body.PistolShot]\nClassicScale=inf\n",
        "[Body.PistolShot]\nDurationScale=0.24\n",
        "[Body.PistolShot]\nDurationScale=2.01\n",
        "[Body.PistolShot]\nFrequencyHz=19.99\n",
        "[Body.PistolShot]\nFrequencyHz=1001\n",
        "[Body.PistolShot]\nFrequencyHz=nan\n",
        "[Trigger.Weapon.Rifle]\nEnabled=yes\n",
        "[Trigger.Weapon.Rifle]\nStrengthScale=-0.01\n",
        "[Trigger.Weapon.Rifle]\nStrengthScale=2.01\n",
        "[Trigger.Weapon.Rifle]\nResistanceStart=-0.01\n",
        "[Trigger.Weapon.Rifle]\nResistanceStart=1.01\n",
        "[Trigger.Weapon.Rifle]\nResistanceStart=nan\n",
        "[Trigger.Weapon.Rifle]\nVibrationFrequencyCode=0\n",
        "[Trigger.Weapon.Rifle]\nVibrationFrequencyCode=256\n",
        "[Trigger.Weapon.Rifle]\nVibrationFrequencyCode=1.5\n",
    };
    for (const auto* input : invalid_profile_values) {
        unchanged_on_error(input, "malformed or out-of-range profile setting rejected");
    }
}

TEST_CASE("fallout4.config.default_keyword_restores_profile_defaults", "[unit][fallout4][config]") {
    using namespace fallout4_adapter;
    Config config{};
    ConfigError error{};
    require(
        parse_config("[Body.OrdinaryStep]\nFrequencyHz=default\n[Trigger.Weapon.GenericFirearm]\n"
                     "ResistanceStart=default\nVibrationFrequencyCode=default\n",
            config,
            &error) == ConfigStatus::loaded &&
            !config.policy.effects.body[static_cast<std::size_t>(BodyEffect::ordinary_step)]
                .frequency_hz &&
            !config.policy.effects
                .triggers[static_cast<std::size_t>(TriggerProfile::generic_firearm)]
                .resistance_start &&
            !config.policy.effects
                .triggers[static_cast<std::size_t>(TriggerProfile::generic_firearm)]
                .vibration_frequency_code,
        "default restores the profile's built-in frequency and start values");
}

TEST_CASE("fallout4.config.text_syntax_and_size_limits", "[unit][fallout4][config]") {
    using namespace fallout4_adapter;
    Config config{};
    ConfigError error{};
    require(
        parse_config(
            "\xEF\xBB\xBF[General]\r\nSlot=3 ; comment\r\n[Body.RifleShot]\r\nEnabled=false\r\n",
            config,
            &error) == ConfigStatus::loaded &&
            config.slot == 3 &&
            !config.policy.effects.body[static_cast<std::size_t>(BodyEffect::rifle_shot)].enabled,
        "UTF8-compatible text syntax, CRLF, comments, and profile sections");

    const auto unchanged_on_error = [&](std::string_view input, const char* message) {
        const auto before = config;
        require(parse_config(input, config, &error) == ConfigStatus::invalid, message);
        require(same_config(config, before),
            "invalid configuration leaves every caller setting unchanged");
    };

    unchanged_on_error(std::string_view{"[General]\0", 10}, "embedded NUL rejected");

    std::string maximum_config(std::size_t{16} * 1024, ';');
    require(parse_config(maximum_config, config) == ConfigStatus::loaded,
        "16 KiB configuration boundary accepted");
    maximum_config.push_back(';');
    unchanged_on_error(maximum_config, "configuration above 16 KiB rejected");
    require(error.line == 0, "oversize input is rejected before line parsing");
}
