#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/effect_settings.hpp>
#include <fallout4_adapter/model.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>

namespace {
void require(bool condition, std::string_view message) {
    INFO(message);
    REQUIRE(condition);
}

std::string_view trim(std::string_view text) noexcept {
    constexpr std::string_view whitespace = " \t\r";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
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
        left.policy.effects.audio_haptics_enabled != right.policy.effects.audio_haptics_enabled ||
        left.policy.effects.triggers_enabled != right.policy.effects.triggers_enabled) {
        return false;
    }
    for (std::size_t index{}; index < left.policy.effects.body.size(); ++index) {
        const auto& body_left = left.policy.effects.body[index];
        const auto& body_right = right.policy.effects.body[index];
        if (body_left.enabled != body_right.enabled || body_left.hd_scale != body_right.hd_scale ||
            body_left.classic_scale != body_right.classic_scale ||
            body_left.duration_scale != body_right.duration_scale ||
            body_left.frequency_hz != body_right.frequency_hz ||
            body_left.audio_haptic != body_right.audio_haptic) {
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

std::string read_fixture(std::string_view filename) {
    const std::filesystem::path path{std::string{filename}};
    std::ifstream file{path, std::ios::binary};
    require(static_cast<bool>(file), "shipped INI fixture exists in the configured test directory");
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

struct IniSetting {
    std::string_view section;
    std::string_view key;
};

bool has_setting(std::string_view text, const IniSetting& expected) noexcept {
    std::string_view active_section;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        auto line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        line = trim(line.substr(0, line.find_first_of(";#")));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            active_section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        if (active_section != expected.section) {
            continue;
        }
        const auto equals = line.find('=');
        if (equals != std::string_view::npos && trim(line.substr(0, equals)) == expected.key) {
            return true;
        }
    }
    return false;
}

bool has_default_values(const fallout4_adapter::Config& config) noexcept {
    using namespace fallout4_adapter;
    const auto defaults = std::make_unique<Config>();
    return same_config(config, *defaults);
}
} // namespace

TEST_CASE("fallout4.config.shipped_ini_fixtures_and_defaults", "[integration][fallout4][config]") {
    using namespace fallout4_adapter;
    const auto working_ini = read_fixture("InputNextFallout4.ini");
    const auto reference_ini = read_fixture("InputNextFallout4.reference.ini");
    require(working_ini.size() <= std::size_t{16} * 1024,
        "working INI remains within parser limit");
    require(reference_ini.size() <= std::size_t{16} * 1024,
        "reference INI remains within parser limit");
    const auto shipped_working = std::make_unique<Config>();
    const auto shipped_reference = std::make_unique<Config>();
    ConfigError error{};
    require(read_config("InputNextFallout4.ini", *shipped_working, &error) == ConfigStatus::loaded,
        "shipped working INI loads through the filesystem API");
    require(read_config("InputNextFallout4.reference.ini", *shipped_reference, &error) ==
                ConfigStatus::loaded,
        "shipped reference INI loads through the filesystem API");
    require(has_default_values(*shipped_working), "sparse working INI matches built-in defaults");
    require(has_default_values(*shipped_reference),
        "complete reference INI matches built-in defaults");
    require(has_setting(reference_ini, {.section = "Effects", .key = "AudioHapticsEnabled"}),
        "reference INI documents the optional audio-haptics opt-out");

    constexpr std::array<IniSetting, 9> required_globals{{
        {.section = "General", .key = "Slot"},
        {.section = "General", .key = "Enabled"},
        {.section = "Effects", .key = "TriggerScale"},
        {.section = "Effects", .key = "BodyScale"},
        {.section = "Effects", .key = "BodyEnabled"},
        {.section = "Effects", .key = "TriggersEnabled"},
        {.section = "Effects", .key = "ResourceCues"},
        {.section = "Effects", .key = "UICues"},
        {.section = "Logging", .key = "Diagnostics"},
    }};
    for (const auto& setting : required_globals) {
        require(has_setting(working_ini, setting), "working INI documents every global setting");
    }
    constexpr std::array required_body_fields{"Enabled",
        "HDScale",
        "ClassicScale",
        "DurationScale",
        "FrequencyHz"};
    for (const auto& descriptor : body_effect_descriptors) {
        for (const auto* const key : required_body_fields) {
            require(has_setting(reference_ini, {.section = descriptor.section, .key = key}),
                "reference INI covers every body profile setting");
        }
    }
    constexpr std::array required_trigger_fields{"Enabled",
        "StrengthScale",
        "ResistanceStart",
        "VibrationFrequencyCode"};
    for (const auto& descriptor : trigger_profile_descriptors) {
        for (const auto* const key : required_trigger_fields) {
            require(has_setting(reference_ini, {.section = descriptor.section, .key = key}),
                "reference INI covers every trigger profile setting");
        }
    }
}
