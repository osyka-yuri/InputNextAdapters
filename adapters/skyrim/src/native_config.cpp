#include <skyrim_adapter/native/config.hpp>

#include <skyrim_adapter/effect_settings.hpp>

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

namespace skyrim_adapter::native {
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

[[nodiscard]] bool equals_ascii_case_insensitive(std::string_view value,
    std::string_view expected) noexcept {
    if (value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        char actual = value[index];
        if (actual >= 'A' && actual <= 'Z') {
            actual = static_cast<char>(actual - 'A' + 'a');
        }
        if (actual != expected[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool parse_boolean(std::string_view value, bool& result) noexcept {
    if (equals_ascii_case_insensitive(value, "true")) {
        result = true;
        return true;
    }
    if (equals_ascii_case_insensitive(value, "false")) {
        result = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool parse_unsigned(std::string_view value, unsigned& result) noexcept {
    unsigned parsed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size()) {
        return false;
    }
    result = parsed;
    return true;
}

#ifndef INPUTNEXT_SKYRIM_LE_CONFIG
[[nodiscard]] bool parse_signed(std::string_view value, std::int32_t& result) noexcept {
    std::int32_t parsed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size()) {
        return false;
    }
    result = parsed;
    return true;
}
#endif

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
parse_bounded_float(std::string_view value, float minimum, float maximum, float& result) noexcept {
    float parsed{};
    if (!parse_finite(value, parsed) || parsed < minimum || parsed > maximum) {
        return false;
    }
    result = parsed;
    return true;
}

[[nodiscard]] bool parse_section(std::string_view section) noexcept {
    if (section == "InputNextSkyrim" || section == "LED") {
        return true;
    }
    if (section.starts_with("Body.")) {
        for (const auto& descriptor : skyrim_adapter::effect_descriptors) {
            if (section == descriptor.section) {
                return true;
            }
        }
    }
    if (section.starts_with("Trigger.")) {
        for (const auto& descriptor : skyrim_adapter::trigger_descriptors) {
            if (section == descriptor.section) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] std::size_t index_of(skyrim_adapter::EffectId id) noexcept {
    return static_cast<std::size_t>(id);
}

[[nodiscard]] std::size_t index_of(skyrim_adapter::TriggerId id) noexcept {
    return static_cast<std::size_t>(id);
}

[[nodiscard]] const skyrim_adapter::EffectDescriptor* find_body_descriptor(
    std::string_view section) noexcept {
    for (const auto& descriptor : skyrim_adapter::effect_descriptors) {
        if (section == descriptor.section) {
            return &descriptor;
        }
    }
    return nullptr;
}

[[nodiscard]] const skyrim_adapter::TriggerDescriptor* find_trigger_descriptor(
    std::string_view section) noexcept {
    for (const auto& descriptor : skyrim_adapter::trigger_descriptors) {
        if (section == descriptor.section) {
            return &descriptor;
        }
    }
    return nullptr;
}

[[nodiscard]] bool parse_bounded_float_field(std::string_view value,
    float minimum,
    float maximum,
    float& target,
    std::string_view& message,
    std::string_view expected) noexcept {
    if (parse_bounded_float(value, minimum, maximum, target)) {
        return true;
    }
    message = expected;
    return false;
}

struct ConfigProperty {
    std::string_view key;
    std::string_view value;
};

[[nodiscard]] bool parse_body_key(const skyrim_adapter::EffectDescriptor& descriptor,
    ConfigProperty property,
    NativeConfig& config,
    std::string_view& message) noexcept {
    const auto [key, value] = property;
    auto& adjustment = config.effects.body[index_of(descriptor.id)];
    if (key == "Enabled") {
        if (parse_boolean(value, adjustment.enabled)) {
            return true;
        }
        message = "Expected true or false.";
        return false;
    }
    if (key == "HDScale") {
        return parse_bounded_float_field(value,
            0.0F,
            2.0F,
            adjustment.hd_scale,
            message,
            "Expected a finite number from 0 to 2.");
    }
    if (key == "ClassicScale") {
        if (!descriptor.classic) {
            message = "ClassicScale is unsupported for this body profile.";
            return false;
        }
        return parse_bounded_float_field(value,
            0.0F,
            2.0F,
            adjustment.classic_scale,
            message,
            "Expected a finite number from 0 to 2.");
    }
    if (key == "DurationScale") {
        if (!descriptor.transient) {
            message = "DurationScale is unsupported for this held body profile.";
            return false;
        }
        return parse_bounded_float_field(value,
            0.25F,
            2.0F,
            adjustment.duration_scale,
            message,
            "Expected a finite number from 0.25 to 2.");
    }
    if (key == "FrequencyHz") {
        if (equals_ascii_case_insensitive(value, "default")) {
            adjustment.frequency_hz = 0.0F;
            return true;
        }
        return parse_bounded_float_field(value,
            20.0F,
            1000.0F,
            adjustment.frequency_hz,
            message,
            "Expected default or a finite number from 20 to 1000 Hz.");
    }
    message = "Unknown key in this body profile.";
    return false;
}

[[nodiscard]] bool parse_trigger_key(const skyrim_adapter::TriggerDescriptor& descriptor,
    ConfigProperty property,
    NativeConfig& config,
    std::string_view& message) noexcept {
    const auto [key, value] = property;
    auto& adjustment = config.effects.triggers[index_of(descriptor.id)];
    if (key == "Enabled") {
        if (parse_boolean(value, adjustment.enabled)) {
            return true;
        }
        message = "Expected true or false.";
        return false;
    }
    if (key == "StrengthScale") {
        return parse_bounded_float_field(value,
            0.0F,
            2.0F,
            adjustment.strength_scale,
            message,
            "Expected a finite number from 0 to 2.");
    }
    if (key == "ResistanceStart") {
        if (equals_ascii_case_insensitive(value, "default")) {
            adjustment.start.reset();
            return true;
        }
        float parsed{};
        if (!parse_bounded_float(value, 0.0F, 1.0F, parsed)) {
            message = "Expected default or a finite number from 0 to 1.";
            return false;
        }
        adjustment.start = parsed;
        return true;
    }
    if (key == "VibrationFrequencyCode") {
        if (!descriptor.vibration) {
            message =
                "VibrationFrequencyCode is unsupported for this non-vibration trigger profile.";
            return false;
        }
        if (equals_ascii_case_insensitive(value, "default")) {
            adjustment.frequency_code.reset();
            return true;
        }
        unsigned parsed{};
        if (!parse_unsigned(value, parsed) || parsed > 255) {
            message = "Expected default or an integer from 0 to 255.";
            return false;
        }
        adjustment.frequency_code = parsed;
        return true;
    }
    message = "Unknown key in this trigger profile.";
    return false;
}

[[nodiscard]] bool
parse_led_key(ConfigProperty property, NativeConfig& config, std::string_view& message) noexcept {
    const auto [key, value] = property;
    auto& settings = config.effects.leds;
    if (key == "Enabled") {
        if (parse_boolean(value, settings.enabled)) {
            return true;
        }
        message = "Expected true or false.";
        return false;
    }
    if (key == "HealthEnabled") {
        if (parse_boolean(value, settings.health_enabled)) {
            return true;
        }
        message = "Expected true or false.";
        return false;
    }
    if (key == "HealthHoldMs") {
        unsigned parsed{};
        if (parse_unsigned(value, parsed) && parsed >= 1U && parsed <= 60000U) {
            settings.health_hold_ms = parsed;
            return true;
        }
        message = "Expected an integer from 1 to 60000 milliseconds.";
        return false;
    }
    if (key == "HealingTransitionMs") {
        unsigned parsed{};
        if (parse_unsigned(value, parsed) && parsed <= 1000U) {
            settings.healing_transition_ms = parsed;
            return true;
        }
        message = "Expected an integer from 0 to 1000 milliseconds.";
        return false;
    }
    if (key == "CriticalEnter") {
        return parse_bounded_float_field(value,
            0.0F,
            1.0F,
            settings.critical_enter,
            message,
            "Expected a finite number from 0 to 1.");
    }
    if (key == "CriticalExit") {
        return parse_bounded_float_field(value,
            0.0F,
            1.0F,
            settings.critical_exit,
            message,
            "Expected a finite number from 0 to 1.");
    }
#ifndef INPUTNEXT_SKYRIM_LE_CONFIG
    if (key == "StealthEnabled") {
        if (parse_boolean(value, settings.stealth_enabled)) {
            return true;
        }
        message = "Expected true or false.";
        return false;
    }
    if (key == "DetectionHiddenScore") {
        if (parse_signed(value, settings.detection_hidden_score)) {
            return true;
        }
        message = "Expected a signed 32-bit integer.";
        return false;
    }
    if (key == "DetectionAlertScore") {
        if (parse_signed(value, settings.detection_alert_score)) {
            return true;
        }
        message = "Expected a signed 32-bit integer.";
        return false;
    }
#endif
    message = "Unknown key in this LED profile or unsupported for this game edition.";
    return false;
}

void reset_error(ConfigError* error) noexcept {
    if (error) {
        error->line = 0;
        error->section.clear();
        error->key.clear();
        error->message = {};
    }
}

struct ConfigLocation {
    std::string_view section;
    std::string_view key;
};

void set_error(ConfigError* error,
    std::size_t line,
    ConfigLocation location,
    std::string_view message) {
    if (error) {
        error->line = line;
        error->section.assign(location.section);
        error->key.assign(location.key);
        error->message = message;
    }
}

enum class RememberResult : std::uint8_t { stored, duplicate, full };

template <std::size_t Capacity>
RememberResult remember(std::array<std::string_view, Capacity>& values,
    std::size_t& count,
    std::string_view value) noexcept {
    const auto end = values.begin() + static_cast<std::ptrdiff_t>(count);
    if (std::find(values.begin(), end, value) != end) {
        return RememberResult::duplicate;
    }
    if (count == values.size()) {
        return RememberResult::full;
    }
    values[count++] = value;
    return RememberResult::stored;
}

enum class RootField : std::uint8_t {
    slot,
    trigger_scale,
    rumble_scale,
    body_enabled,
    audio_haptics_enabled,
    triggers_enabled,
    respect_game_vibration,
};

struct RootKeyDescriptor {
    std::string_view key;
    RootField field;
    std::string_view expected;
};

constexpr std::array root_key_descriptors{
    RootKeyDescriptor{
        .key = "ControllerSlot",
        .field = RootField::slot,
        .expected = "Expected an integer from 0 to 3.",
    },
    RootKeyDescriptor{
        .key = "TriggerScale",
        .field = RootField::trigger_scale,
        .expected = "Expected a finite number from 0 to 1.",
    },
    RootKeyDescriptor{
        .key = "RumbleScale",
        .field = RootField::rumble_scale,
        .expected = "Expected a finite number from 0 to 1.",
    },
    RootKeyDescriptor{
        .key = "BodyEnabled",
        .field = RootField::body_enabled,
        .expected = "Expected true or false.",
    },
    RootKeyDescriptor{
        .key = "AudioHapticsEnabled",
        .field = RootField::audio_haptics_enabled,
        .expected = "Expected true or false.",
    },
    RootKeyDescriptor{
        .key = "TriggersEnabled",
        .field = RootField::triggers_enabled,
        .expected = "Expected true or false.",
    },
    RootKeyDescriptor{
        .key = "RespectGameVibration",
        .field = RootField::respect_game_vibration,
        .expected = "Expected true or false.",
    },
};

struct ParseState {
    explicit ParseState(NativeConfig& destination) noexcept : parsed(destination) {}

    NativeConfig& parsed;
    bool section_open{};
    std::string_view current_section;
    std::array<std::string_view,
        2 + skyrim_adapter::effect_descriptors.size() + skyrim_adapter::trigger_descriptors.size()>
        seen_sections{};
    std::size_t seen_section_count{};
    std::array<std::string_view, 16> seen_root_keys{};
    std::size_t seen_root_key_count{};
    std::array<std::string_view, 12> seen_profile_keys{};
    std::size_t seen_profile_key_count{};
    std::size_t last_led_line{};
    std::string_view last_led_key;
};

[[nodiscard]] const RootKeyDescriptor* find_root_key(std::string_view key) noexcept {
    for (const auto& descriptor : root_key_descriptors) {
        if (descriptor.key == key) {
            return &descriptor;
        }
    }
    return nullptr;
}

[[nodiscard]] bool apply_root_value(const RootKeyDescriptor& descriptor,
    std::string_view value,
    NativeConfig& config) noexcept {
    switch (descriptor.field) {
    case RootField::slot:
        return parse_unsigned(value, config.slot) && config.slot <= 3;
    case RootField::trigger_scale:
        return parse_bounded_float(value, 0.0F, 1.0F, config.trigger_scale);
    case RootField::rumble_scale:
        return parse_bounded_float(value, 0.0F, 1.0F, config.rumble_scale);
    case RootField::body_enabled:
        return parse_boolean(value, config.effects.body_enabled);
    case RootField::audio_haptics_enabled:
        return parse_boolean(value, config.effects.audio_haptics_enabled);
    case RootField::triggers_enabled:
        return parse_boolean(value, config.effects.triggers_enabled);
    case RootField::respect_game_vibration:
        return parse_boolean(value, config.effects.respect_game_vibration);
    }
    return false;
}

[[nodiscard]] bool parse_section_line(std::string_view line,
    std::size_t line_number,
    ParseState& state,
    ConfigError* error) {
    std::string_view section = line.size() > 1 ? line.substr(1) : std::string_view{};
    const bool closed = !section.empty() && section.back() == ']';
    if (closed) {
        section.remove_suffix(1);
    }
    section = trim(section);
    if (!closed) {
        set_error(error,
            line_number,
            {.section = section, .key = {}},
            "Expected a section header ending with ']'.");
        return false;
    }
    if (section.empty()) {
        set_error(error, line_number, ConfigLocation(), "Section name must not be empty.");
        return false;
    }
    if (!parse_section(section)) {
        set_error(error, line_number, {.section = section, .key = {}}, "Unknown section name.");
        return false;
    }
    const auto remembered = remember(state.seen_sections, state.seen_section_count, section);
    if (remembered == RememberResult::duplicate) {
        set_error(error,
            line_number,
            {.section = section, .key = {}},
            "Section appears more than once.");
        return false;
    }
    if (remembered == RememberResult::full) {
        set_error(error, line_number, {.section = section, .key = {}}, "Too many sections.");
        return false;
    }
    state.current_section = section;
    state.section_open = true;
    state.seen_profile_key_count = 0;
    return true;
}

[[nodiscard]] bool parse_root_entry(ConfigProperty property,
    std::size_t line_number,
    ParseState& state,
    ConfigError* error) {
    const auto [key, value] = property;
    const auto remembered = remember(state.seen_root_keys, state.seen_root_key_count, key);
    if (remembered == RememberResult::duplicate) {
        set_error(error,
            line_number,
            {.section = state.current_section, .key = key},
            "Key appears more than once in this section.");
        return false;
    }
    if (remembered == RememberResult::full) {
        set_error(error,
            line_number,
            {.section = state.current_section, .key = key},
            "Too many keys in this section.");
        return false;
    }

    const RootKeyDescriptor* descriptor = find_root_key(key);
    if (!descriptor) {
        set_error(error,
            line_number,
            {.section = state.current_section, .key = key},
            "Unknown key in [InputNextSkyrim].");
        return false;
    }
    if (!apply_root_value(*descriptor, value, state.parsed)) {
        set_error(error,
            line_number,
            {.section = state.current_section, .key = key},
            descriptor->expected);
        return false;
    }
    return true;
}

[[nodiscard]] bool parse_profile_entry(ConfigProperty property,
    std::size_t line_number,
    ParseState& state,
    ConfigError* error) {
    const auto remembered =
        remember(state.seen_profile_keys, state.seen_profile_key_count, property.key);
    if (remembered == RememberResult::duplicate) {
        set_error(error,
            line_number,
            {.section = state.current_section, .key = property.key},
            "Key appears more than once in this section.");
        return false;
    }
    if (remembered == RememberResult::full) {
        set_error(error,
            line_number,
            {.section = state.current_section, .key = property.key},
            "Too many keys in this section.");
        return false;
    }

    std::string_view message;
    bool valid{};
    if (state.current_section.starts_with("Body.")) {
        const auto* descriptor = find_body_descriptor(state.current_section);
        valid =
            descriptor != nullptr && parse_body_key(*descriptor, property, state.parsed, message);
        if (!descriptor) {
            message = "Unknown body profile.";
        }
    } else if (state.current_section.starts_with("Trigger.")) {
        const auto* descriptor = find_trigger_descriptor(state.current_section);
        valid = descriptor != nullptr &&
                parse_trigger_key(*descriptor, property, state.parsed, message);
        if (!descriptor) {
            message = "Unknown trigger profile.";
        }
    } else if (state.current_section == "LED") {
        valid = parse_led_key(property, state.parsed, message);
        state.last_led_line = line_number;
        state.last_led_key = property.key;
    }
    if (!valid) {
        set_error(error,
            line_number,
            {.section = state.current_section, .key = property.key},
            message);
        return false;
    }
    return true;
}

[[nodiscard]] bool parse_entry_line(std::string_view line,
    std::size_t line_number,
    ParseState& state,
    ConfigError* error) {
    const auto equals = line.find('=');
    const auto key = trim(equals == std::string_view::npos ? line : line.substr(0, equals));
    if (!state.section_open) {
        set_error(error,
            line_number,
            {.section = {}, .key = key},
            "Key/value entries must follow a section header.");
        return false;
    }
    if (equals == std::string_view::npos) {
        set_error(error,
            line_number,
            {.section = state.current_section, .key = key},
            "Expected Key=Value syntax.");
        return false;
    }

    auto value = trim(line.substr(equals + 1));
    if (const auto comment = value.find_first_of(";#"); comment != std::string_view::npos) {
        value = trim(value.substr(0, comment));
    }
    if (key.empty()) {
        set_error(error,
            line_number,
            {.section = state.current_section, .key = {}},
            "Key name must not be empty.");
        return false;
    }
    if (value.empty()) {
        set_error(error,
            line_number,
            {.section = state.current_section, .key = key},
            "Value must not be empty.");
        return false;
    }
    const ConfigProperty property{.key = key, .value = value};
    if (state.current_section == "InputNextSkyrim") {
        return parse_root_entry(property, line_number, state, error);
    }
    return parse_profile_entry(property, line_number, state, error);
}

[[nodiscard]] bool
parse_line(std::string_view line, std::size_t line_number, ParseState& state, ConfigError* error) {
    line = trim(line);
    if (line.empty() || line.front() == ';' || line.front() == '#') {
        return true;
    }
    if (line.front() == '[') {
        return parse_section_line(line, line_number, state, error);
    }
    return parse_entry_line(line, line_number, state, error);
}

[[nodiscard]] bool
parse_lines(std::string_view contents, NativeConfig& parsed, ConfigError* error) {
    ParseState state{parsed};
    std::size_t line_number{};
    while (!contents.empty()) {
        ++line_number;
        const auto newline = contents.find('\n');
        const auto line = contents.substr(0, newline);
        contents =
            newline == std::string_view::npos ? std::string_view{} : contents.substr(newline + 1);
        if (!parse_line(line, line_number, state, error)) {
            return false;
        }
    }
    if (!inputnext::adapters::valid_led_settings(state.parsed.effects.leds)) {
        set_error(error,
            state.last_led_line,
            {.section = "LED", .key = state.last_led_key},
            "LED timing, thresholds, or score endpoints are inconsistent.");
        return false;
    }
    return true;
}
} // namespace

bool parse_config(std::string_view contents, NativeConfig& config, ConfigError* error) {
    reset_error(error);
    if (contents.size() > maximum_config_bytes) {
        if (error) {
            error->line = 0;
            error->section.clear();
            error->key.clear();
            error->message = maximum_config_message;
        }
        return false;
    }
    if (contents.starts_with("\xEF\xBB\xBF")) {
        contents.remove_prefix(3);
    }
    NativeConfig parsed{};
    if (!parse_lines(contents, parsed, error)) {
        return false;
    }
    config = parsed;
    return true;
}

ConfigStatus
read_config_file(const std::filesystem::path& path, NativeConfig& config, ConfigError* error) {
    reset_error(error);
    try {
        std::error_code filesystem_error;
        const bool exists = std::filesystem::exists(path, filesystem_error);
        if (filesystem_error) {
            return ConfigStatus::io_error;
        }
        if (!exists) {
            config = NativeConfig{};
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

        NativeConfig parsed{};
        if (!parse_config(contents, parsed, error)) {
            return ConfigStatus::invalid;
        }
        config = parsed;
        return ConfigStatus::loaded;
    } catch (...) {
        reset_error(error);
        return ConfigStatus::io_error;
    }
}
} // namespace skyrim_adapter::native
