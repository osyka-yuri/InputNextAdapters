#include "config.hpp"

#include "effect_settings.hpp"

#include <inputnext_adapters/led_policy.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace oblivion_adapter {
namespace {
constexpr std::size_t maximum_config_bytes = std::size_t{64} * 1024;
constexpr std::string_view maximum_config_message = "configuration exceeds 64 KiB";

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                                 value.front() == '\r' || value.front() == '\n')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' ||
                                 value.back() == '\n')) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] char ascii_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] bool equal_ci(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool parse_bool(std::string_view value, bool& result) noexcept {
    if (equal_ci(value, "true")) {
        result = true;
        return true;
    }
    if (equal_ci(value, "false")) {
        result = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool parse_unsigned(std::string_view value, std::uint32_t& result) noexcept {
    std::uint32_t parsed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size()) {
        return false;
    }
    result = parsed;
    return true;
}

[[nodiscard]] bool parse_finite(std::string_view value, float& result) noexcept {
    float parsed{};
    const auto [end, error] = std::from_chars(value.data(),
        value.data() + value.size(),
        parsed,
        std::chars_format::general);
    if (error != std::errc{} || end != value.data() + value.size() || !std::isfinite(parsed)) {
        return false;
    }
    result = parsed;
    return true;
}

[[nodiscard]] bool
parse_range(std::string_view value, float minimum, float maximum, float& result) noexcept {
    float parsed{};
    if (!parse_finite(value, parsed) || parsed < minimum || parsed > maximum) {
        return false;
    }
    result = parsed;
    return true;
}

enum class SectionKind : std::uint8_t { root, led, profile };

struct Parser {
    explicit Parser(EffectSettings& candidate) noexcept : settings(candidate) {}

    EffectSettings& settings;
    bool section_open{};
    SectionKind section_kind{SectionKind::root};
    std::size_t profile_index{};
    std::array<std::string_view, effect_sections.size() + 2> sections{};
    std::size_t section_count{};
    std::array<std::string_view, 12> keys{};
    std::size_t key_count{};
    std::size_t last_led_line{};
};

enum class RememberResult : std::uint8_t { stored, duplicate, full };

struct KeyValue {
    std::string_view key;
    std::string_view value;
};

template <std::size_t Capacity>
RememberResult remember(std::array<std::string_view, Capacity>& entries,
    std::size_t& count,
    std::string_view value) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (equal_ci(entries[index], value)) {
            return RememberResult::duplicate;
        }
    }
    if (count == entries.size()) {
        return RememberResult::full;
    }
    entries[count++] = value;
    return RememberResult::stored;
}

[[nodiscard]] bool
set_error(ConfigError* error, std::size_t line, std::string_view message) noexcept {
    if (error) {
        error->line = line;
        error->message = message;
    }
    return false;
}

[[nodiscard]] bool
resolve_section(std::string_view name, SectionKind& kind, std::size_t& index) noexcept {
    if (equal_ci(name, "InputNextOblivion")) {
        kind = SectionKind::root;
        return true;
    }
    if (equal_ci(name, "LED")) {
        kind = SectionKind::led;
        return true;
    }
    for (std::size_t candidate = 0; candidate < effect_sections.size(); ++candidate) {
        if (equal_ci(name, effect_sections[candidate])) {
            kind = SectionKind::profile;
            index = candidate;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool
parse_section(std::string_view line, std::size_t line_number, Parser& parser, ConfigError* error) {
    auto name = trim(line.substr(1));
    if (name.empty() || name.back() != ']') {
        return set_error(error, line_number, "Expected a section header ending with ']'.");
    }
    name.remove_suffix(1);
    name = trim(name);
    if (name.empty()) {
        return set_error(error, line_number, "Section name must not be empty.");
    }
    SectionKind kind{};
    std::size_t index{};
    if (!resolve_section(name, kind, index)) {
        return set_error(error, line_number, "Unknown section name.");
    }
    const auto remembered = remember(parser.sections, parser.section_count, name);
    if (remembered == RememberResult::duplicate) {
        return set_error(error, line_number, "Section appears more than once.");
    }
    if (remembered == RememberResult::full) {
        return set_error(error, line_number, "Too many sections.");
    }
    parser.section_kind = kind;
    parser.profile_index = index;
    parser.section_open = true;
    parser.key_count = 0;
    return true;
}

[[nodiscard]] bool
parse_root_value(KeyValue entry, EffectSettings& settings, std::string_view& message) noexcept {
    struct Switch {
        std::string_view key;
        bool EffectSettings::* member{};
    };

    constexpr std::array switches{
        Switch{.key = "BodyEnabled", .member = &EffectSettings::body_enabled},
        Switch{.key = "TriggersEnabled", .member = &EffectSettings::triggers_enabled},
        Switch{.key = "MenuEffectsEnabled", .member = &EffectSettings::menu_effects_enabled},
        Switch{.key = "AudioHapticsEnabled", .member = &EffectSettings::audio_haptics_enabled},
    };
    const auto [key, value] = entry;
    const auto setting = std::ranges::find_if(switches,
        [key](const Switch& candidate) { return equal_ci(key, candidate.key); });
    if (setting != switches.end()) {
        if (parse_bool(value, settings.*setting->member)) {
            return true;
        }
        message = "Expected true or false.";
        return false;
    }
    if (equal_ci(key, "ControllerSlot")) {
        if (parse_unsigned(value, settings.controller_slot) && settings.controller_slot <= 3U) {
            return true;
        }
        message = "Expected an integer from 0 to 3.";
    } else if (equal_ci(key, "RumbleScale")) {
        if (parse_range(value, 0.0F, 1.0F, settings.rumble_scale)) {
            return true;
        }
        message = "Expected a finite number from 0 to 1.";
    } else if (equal_ci(key, "TriggerScale")) {
        if (parse_range(value, 0.0F, 1.0F, settings.trigger_scale)) {
            return true;
        }
        message = "Expected a finite number from 0 to 1.";
    } else {
        message = "Unknown key in [InputNextOblivion].";
    }
    return false;
}

[[nodiscard]] bool
parse_led_value(KeyValue entry, EffectSettings& settings, std::string_view& message) noexcept {
    const auto [key, value] = entry;
    auto& led = settings.led;
    if (equal_ci(key, "Enabled")) {
        if (parse_bool(value, led.enabled)) {
            return true;
        }
        message = "Expected true or false.";
    } else if (equal_ci(key, "HealthEnabled")) {
        if (parse_bool(value, led.health_enabled)) {
            return true;
        }
        message = "Expected true or false.";
    } else if (equal_ci(key, "StealthEnabled")) {
        if (parse_bool(value, led.stealth_enabled)) {
            return true;
        }
        message = "Expected true or false.";
    } else if (equal_ci(key, "HealthHoldMs")) {
        std::uint32_t parsed{};
        if (parse_unsigned(value, parsed) && parsed >= 1U && parsed <= 60000U) {
            led.health_hold_ms = parsed;
            return true;
        }
        message = "Expected an integer from 1 to 60000 milliseconds.";
    } else if (equal_ci(key, "HealingTransitionMs")) {
        std::uint32_t parsed{};
        if (parse_unsigned(value, parsed) && parsed <= 1000U) {
            led.healing_transition_ms = parsed;
            return true;
        }
        message = "Expected an integer from 0 to 1000 milliseconds.";
    } else if (equal_ci(key, "CriticalEnter")) {
        if (parse_range(value, 0.0F, 1.0F, led.critical_enter)) {
            return true;
        }
        message = "Expected a finite number from 0 to 1.";
    } else if (equal_ci(key, "CriticalExit")) {
        if (parse_range(value, 0.0F, 1.0F, led.critical_exit)) {
            return true;
        }
        message = "Expected a finite number from 0 to 1.";
    } else {
        message = "Unknown key in [LED].";
    }
    return false;
}

[[nodiscard]] bool parse_profile_value(KeyValue entry,
    ProfileAdjustment& profile,
    std::string_view& message) noexcept {
    const auto [key, value] = entry;
    if (equal_ci(key, "Enabled")) {
        if (parse_bool(value, profile.enabled)) {
            return true;
        }
        message = "Expected true or false.";
    } else if (equal_ci(key, "HDScale")) {
        if (parse_range(value, 0.0F, 2.0F, profile.hd_scale)) {
            return true;
        }
        message = "Expected a finite number from 0 to 2.";
    } else if (equal_ci(key, "ClassicScale")) {
        if (parse_range(value, 0.0F, 2.0F, profile.classic_scale)) {
            return true;
        }
        message = "Expected a finite number from 0 to 2.";
    } else if (equal_ci(key, "DurationScale")) {
        if (parse_range(value, 0.25F, 2.0F, profile.duration_scale)) {
            return true;
        }
        message = "Expected a finite number from 0.25 to 2.";
    } else if (equal_ci(key, "FrequencyHz")) {
        if (equal_ci(value, "default")) {
            profile.frequency_hz.reset();
            return true;
        }
        float parsed{};
        if (parse_range(value, 20.0F, 1000.0F, parsed)) {
            profile.frequency_hz = parsed;
            return true;
        }
        message = "Expected default or a finite number from 20 to 1000 Hz.";
    } else {
        message = "Unknown key in this body profile.";
    }
    return false;
}

[[nodiscard]] bool
parse_entry(std::string_view line, std::size_t line_number, Parser& parser, ConfigError* error) {
    const auto equals = line.find('=');
    const auto key = trim(equals == std::string_view::npos ? line : line.substr(0, equals));
    if (!parser.section_open) {
        return set_error(error, line_number, "Key/value entries must follow a section header.");
    }
    if (equals == std::string_view::npos) {
        return set_error(error, line_number, "Expected Key=Value syntax.");
    }
    auto value = trim(line.substr(equals + 1));
    if (const auto comment = value.find_first_of(";#"); comment != std::string_view::npos) {
        value = trim(value.substr(0, comment));
    }
    if (key.empty()) {
        return set_error(error, line_number, "Key name must not be empty.");
    }
    if (value.empty()) {
        return set_error(error, line_number, "Value must not be empty.");
    }
    const auto remembered = remember(parser.keys, parser.key_count, key);
    if (remembered == RememberResult::duplicate) {
        return set_error(error, line_number, "Key appears more than once in this section.");
    }
    if (remembered == RememberResult::full) {
        return set_error(error, line_number, "Too many keys in this section.");
    }

    std::string_view message;
    bool valid{};
    const KeyValue entry{.key = key, .value = value};
    if (parser.section_kind == SectionKind::root) {
        valid = parse_root_value(entry, parser.settings, message);
    } else if (parser.section_kind == SectionKind::led) {
        valid = parse_led_value(entry, parser.settings, message);
        if (valid) {
            parser.last_led_line = line_number;
        }
    } else {
        valid = parse_profile_value(entry, parser.settings.profiles[parser.profile_index], message);
    }
    return valid || set_error(error, line_number, message);
}

[[nodiscard]] bool
parse_line(std::string_view line, std::size_t line_number, Parser& parser, ConfigError* error) {
    line = trim(line);
    if (line.empty() || line.front() == ';' || line.front() == '#') {
        return true;
    }
    if (line.front() == '[') {
        return parse_section(line, line_number, parser, error);
    }
    return parse_entry(line, line_number, parser, error);
}

[[nodiscard]] bool
parse_lines(std::string_view contents, EffectSettings& candidate, ConfigError* error) {
    Parser parser{candidate};
    std::size_t line_number{};
    while (!contents.empty()) {
        ++line_number;
        const auto newline = contents.find('\n');
        const auto line = contents.substr(0, newline);
        contents =
            newline == std::string_view::npos ? std::string_view{} : contents.substr(newline + 1);
        if (!parse_line(line, line_number, parser, error)) {
            return false;
        }
    }
    if (!inputnext::adapters::valid_led_settings(candidate.led)) {
        return set_error(error,
            parser.last_led_line,
            "LED timing or critical thresholds are inconsistent.");
    }
    return true;
}

void reset_error(ConfigError* error) noexcept {
    if (error) {
        error->line = 0;
        error->message = {};
    }
}
} // namespace

bool parse_config(std::string_view contents, EffectSettings& settings, ConfigError* error) {
    reset_error(error);
    if (contents.size() > maximum_config_bytes) {
        return set_error(error, 0, maximum_config_message);
    }
    if (contents.starts_with("\xEF\xBB\xBF")) {
        contents.remove_prefix(3);
    }
    EffectSettings candidate{};
    if (!parse_lines(contents, candidate, error)) {
        return false;
    }
    settings = std::move(candidate);
    return true;
}

ConfigStatus
read_config_file(const std::filesystem::path& path, EffectSettings& settings, ConfigError* error) {
    reset_error(error);
    try {
        std::error_code filesystem_error;
        const bool exists = std::filesystem::exists(path, filesystem_error);
        if (filesystem_error) {
            return ConfigStatus::io_error;
        }
        if (!exists) {
            settings = EffectSettings{};
            return ConfigStatus::missing;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return ConfigStatus::io_error;
        }
        std::string contents(maximum_config_bytes + 1, '\0');
        file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (file.bad() || (file.fail() && !file.eof())) {
            return ConfigStatus::io_error;
        }
        contents.resize(static_cast<std::size_t>(file.gcount()));
        if (!parse_config(contents, settings, error)) {
            return ConfigStatus::invalid;
        }
        return ConfigStatus::loaded;
    } catch (...) {
        reset_error(error);
        return ConfigStatus::io_error;
    }
}

} // namespace oblivion_adapter
