#include <fallout4_adapter/config.hpp>

#include <fallout4_adapter/effect_settings.hpp>
#include <fallout4_adapter/model.hpp>

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
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace fallout4_adapter {
namespace {
constexpr std::size_t maximum_config_bytes = std::size_t{16} * 1024;

enum class GlobalField : std::uint8_t {
    slot,
    enabled,
    trigger_scale,
    body_scale,
    resource_cues,
    ui_cues,
    diagnostics,
    body_enabled,
    audio_haptics_enabled,
    triggers_enabled,
};

enum class LedField : std::uint8_t {
    enabled,
    health_enabled,
    stealth_enabled,
    radiation_enabled,
    health_hold_ms,
    healing_transition_ms,
    critical_enter,
    critical_exit,
    detection_hidden_score,
    detection_alert_score,
};

struct GlobalDescriptor {
    std::string_view section;
    std::string_view key;
    GlobalField field;
};

constexpr std::array global_descriptors{
    GlobalDescriptor{.section = "General", .key = "Slot", .field = GlobalField::slot},
    GlobalDescriptor{.section = "General", .key = "Enabled", .field = GlobalField::enabled},
    GlobalDescriptor{.section = "Effects",
        .key = "TriggerScale",
        .field = GlobalField::trigger_scale},
    GlobalDescriptor{.section = "Effects", .key = "BodyScale", .field = GlobalField::body_scale},
    GlobalDescriptor{.section = "Effects",
        .key = "ResourceCues",
        .field = GlobalField::resource_cues},
    GlobalDescriptor{.section = "Effects", .key = "UICues", .field = GlobalField::ui_cues},
    GlobalDescriptor{.section = "Logging", .key = "Diagnostics", .field = GlobalField::diagnostics},
    GlobalDescriptor{.section = "Effects",
        .key = "BodyEnabled",
        .field = GlobalField::body_enabled},
    GlobalDescriptor{.section = "Effects",
        .key = "AudioHapticsEnabled",
        .field = GlobalField::audio_haptics_enabled},
    GlobalDescriptor{.section = "Effects",
        .key = "TriggersEnabled",
        .field = GlobalField::triggers_enabled},
};

constexpr std::array<std::string_view, 5> body_fields{"Enabled",
    "HDScale",
    "ClassicScale",
    "DurationScale",
    "FrequencyHz"};
constexpr std::array<std::string_view, 4> trigger_fields{"Enabled",
    "StrengthScale",
    "ResistanceStart",
    "VibrationFrequencyCode"};
constexpr std::array<std::string_view, 10> led_fields{"Enabled",
    "HealthEnabled",
    "StealthEnabled",
    "RadiationEnabled",
    "HealthHoldMs",
    "HealingTransitionMs",
    "CriticalEnter",
    "CriticalExit",
    "DetectionHiddenScore",
    "DetectionAlertScore"};

constexpr std::size_t body_seen_offset = global_descriptors.size();
constexpr std::size_t trigger_seen_offset =
    body_seen_offset + (body_effect_count * body_fields.size());
constexpr std::size_t setting_count =
    trigger_seen_offset + (trigger_profile_count * trigger_fields.size()) + led_fields.size();
constexpr std::size_t led_seen_offset =
    trigger_seen_offset + (trigger_profile_count * trigger_fields.size());

enum class SettingGroup : std::uint8_t { global, body, trigger, led };

struct SettingReference {
    SettingGroup group{};
    std::size_t entry_index{};
    std::size_t field_index{};
    std::size_t seen_index{};
};

struct SectionKey {
    std::string_view section;
    std::string_view key;
};

std::string_view trim(std::string_view text) noexcept {
    constexpr std::string_view whitespace = " \t\r";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

template <class Descriptors, std::size_t FieldCount>
std::optional<SettingReference> find_profile_setting(SectionKey name,
    const Descriptors& descriptors,
    const std::array<std::string_view, FieldCount>& fields,
    SettingGroup group,
    std::size_t seen_offset) noexcept {
    for (std::size_t profile{}; profile < descriptors.size(); ++profile) {
        if (name.section != descriptors[profile].section) {
            continue;
        }
        for (std::size_t field{}; field < fields.size(); ++field) {
            if (name.key == fields[field]) {
                return SettingReference{.group = group,
                    .entry_index = profile,
                    .field_index = field,
                    .seen_index = seen_offset + (profile * fields.size()) + field};
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

bool boolean(std::string_view text, bool& result) noexcept {
    if (text == "true" || text == "1") {
        result = true;
        return true;
    }
    if (text == "false" || text == "0") {
        result = false;
        return true;
    }
    return false;
}

bool bounded_float(std::string_view text, float minimum, float maximum, float& result) noexcept {
    float value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || !std::isfinite(value) ||
        value < minimum || value > maximum) {
        return false;
    }
    result = value;
    return true;
}

bool bounded_uint(std::string_view text,
    std::uint32_t minimum,
    std::uint32_t maximum,
    std::uint32_t& result) noexcept {
    std::uint32_t value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || value < minimum ||
        value > maximum) {
        return false;
    }
    result = value;
    return true;
}

bool parse_int32(std::string_view text, std::int32_t& result) noexcept {
    std::int32_t value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return false;
    }
    result = value;
    return true;
}

std::optional<SettingReference> find_setting(std::string_view key,
    std::string_view section) noexcept {
    for (std::size_t index{}; index < global_descriptors.size(); ++index) {
        const auto& descriptor = global_descriptors[index];
        if (section == descriptor.section && key == descriptor.key) {
            return SettingReference{
                .group = SettingGroup::global,
                .entry_index = index,
                .field_index = 0,
                .seen_index = index,
            };
        }
    }

    if (section == "LED") {
        for (std::size_t field{}; field < led_fields.size(); ++field) {
            if (key == led_fields[field]) {
                return SettingReference{
                    .group = SettingGroup::led,
                    .entry_index = 0,
                    .field_index = field,
                    .seen_index = led_seen_offset + field,
                };
            }
        }
        return std::nullopt;
    }

    const SectionKey name{.section = section, .key = key};
    const auto body = find_profile_setting(name,
        body_effect_descriptors,
        body_fields,
        SettingGroup::body,
        body_seen_offset);
    if (body) {
        return body;
    }
    return find_profile_setting(name,
        trigger_profile_descriptors,
        trigger_fields,
        SettingGroup::trigger,
        trigger_seen_offset);
}

bool assign_global(GlobalField field, std::string_view value, Config& config) noexcept {
    switch (field) {
    case GlobalField::slot: {
        std::uint32_t slot{};
        const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), slot);
        if (ec != std::errc{} || end != value.data() + value.size() || slot > 3U) {
            return false;
        }
        config.slot = slot;
        return true;
    }
    case GlobalField::enabled:
        return boolean(value, config.policy.enabled);
    case GlobalField::trigger_scale:
        return bounded_float(value, 0.0F, 1.0F, config.policy.trigger_scale);
    case GlobalField::body_scale:
        return bounded_float(value, 0.0F, 1.0F, config.policy.body_scale);
    case GlobalField::resource_cues:
        return boolean(value, config.policy.resource_cues);
    case GlobalField::ui_cues:
        return boolean(value, config.policy.ui_cues);
    case GlobalField::diagnostics:
        return boolean(value, config.diagnostics);
    case GlobalField::body_enabled:
        return boolean(value, config.policy.effects.body_enabled);
    case GlobalField::audio_haptics_enabled:
        return boolean(value, config.policy.effects.audio_haptics_enabled);
    case GlobalField::triggers_enabled:
        return boolean(value, config.policy.effects.triggers_enabled);
    }
    return false;
}

bool assign_body(const SettingReference& setting,
    std::string_view value,
    EffectSettings& settings) noexcept {
    if (setting.entry_index >= settings.body.size()) {
        return false;
    }
    auto& adjustment = settings.body[setting.entry_index];
    switch (setting.field_index) {
    case 0:
        return boolean(value, adjustment.enabled);
    case 1:
        return bounded_float(value, 0.0F, 2.0F, adjustment.hd_scale);
    case 2:
        return bounded_float(value, 0.0F, 2.0F, adjustment.classic_scale);
    case 3:
        return bounded_float(value, 0.25F, 2.0F, adjustment.duration_scale);
    case 4: {
        if (value == "default") {
            adjustment.frequency_hz.reset();
            return true;
        }
        float frequency{};
        if (!bounded_float(value, 20.0F, 1000.0F, frequency)) {
            return false;
        }
        adjustment.frequency_hz = frequency;
        return true;
    }
    default:
        return false;
    }
}

bool assign_trigger(const SettingReference& setting,
    std::string_view value,
    EffectSettings& settings) noexcept {
    if (setting.entry_index >= settings.triggers.size()) {
        return false;
    }
    auto& adjustment = settings.triggers[setting.entry_index];
    switch (setting.field_index) {
    case 0:
        return boolean(value, adjustment.enabled);
    case 1:
        return bounded_float(value, 0.0F, 2.0F, adjustment.strength_scale);
    case 2: {
        if (value == "default") {
            adjustment.resistance_start.reset();
            return true;
        }
        float start{};
        if (!bounded_float(value, 0.0F, 1.0F, start)) {
            return false;
        }
        adjustment.resistance_start = start;
        return true;
    }
    case 3: {
        if (value == "default") {
            adjustment.vibration_frequency_code.reset();
            return true;
        }
        std::uint16_t code{};
        const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), code);
        if (ec != std::errc{} || end != value.data() + value.size() || code < 1U || code > 255U) {
            return false;
        }
        adjustment.vibration_frequency_code = code;
        return true;
    }
    default:
        return false;
    }
}

bool assign_led(const SettingReference& setting,
    std::string_view value,
    inputnext::adapters::LedSettings& settings) noexcept {
    switch (static_cast<LedField>(setting.field_index)) {
    case LedField::enabled:
        return boolean(value, settings.enabled);
    case LedField::health_enabled:
        return boolean(value, settings.health_enabled);
    case LedField::stealth_enabled:
        return boolean(value, settings.stealth_enabled);
    case LedField::radiation_enabled:
        return boolean(value, settings.radiation_enabled);
    case LedField::health_hold_ms:
        return bounded_uint(value, 1U, 60000U, settings.health_hold_ms);
    case LedField::healing_transition_ms:
        return bounded_uint(value, 0U, 1000U, settings.healing_transition_ms);
    case LedField::critical_enter:
        return bounded_float(value, 0.0F, 1.0F, settings.critical_enter);
    case LedField::critical_exit:
        return bounded_float(value, 0.0F, 1.0F, settings.critical_exit);
    case LedField::detection_hidden_score:
        return parse_int32(value, settings.detection_hidden_score);
    case LedField::detection_alert_score:
        return parse_int32(value, settings.detection_alert_score);
    }
    return false;
}

bool assign(const SettingReference& setting, std::string_view value, Config& config) noexcept {
    switch (setting.group) {
    case SettingGroup::global:
        if (setting.entry_index >= global_descriptors.size()) {
            return false;
        }
        return assign_global(global_descriptors[setting.entry_index].field, value, config);
    case SettingGroup::body:
        return assign_body(setting, value, config.policy.effects);
    case SettingGroup::trigger:
        return assign_trigger(setting, value, config.policy.effects);
    case SettingGroup::led:
        return assign_led(setting, value, config.policy.effects.leds);
    }
    return false;
}

struct ParseState {
    Config parsed{};
    std::string_view section;
    std::array<bool, setting_count> seen{};
    std::size_t line_number{};
    ConfigError* error{};
    std::size_t critical_enter_line{};
    std::size_t critical_exit_line{};
    std::size_t detection_hidden_line{};
    std::size_t detection_alert_line{};
};

bool fail(const ParseState& state, std::string_view message) noexcept {
    if (state.error) {
        *state.error = {.line = state.line_number, .message = message};
    }
    return false;
}

bool parse_section_line(std::string_view line, ParseState& state) noexcept {
    state.section = trim(line.substr(1, line.size() - 2));
    if (state.section != "General" && state.section != "Effects" && state.section != "Logging" &&
        state.section != "LED") {
        for (const auto& descriptor : body_effect_descriptors) {
            if (state.section == descriptor.section) {
                return true;
            }
        }
        for (const auto& descriptor : trigger_profile_descriptors) {
            if (state.section == descriptor.section) {
                return true;
            }
        }
        return fail(state, "unknown section");
    }
    return true;
}

bool parse_entry_line(std::string_view line, ParseState& state) noexcept {
    const auto equals = line.find('=');
    if (equals == std::string_view::npos) {
        return fail(state, "expected key=value");
    }
    const auto key = trim(line.substr(0, equals));
    const auto value = trim(line.substr(equals + 1));
    const auto setting = find_setting(key, state.section);
    if (!setting) {
        return fail(state, "unknown key or incorrect section");
    }
    if (state.seen[setting->seen_index]) {
        return fail(state, "duplicate key");
    }
    if (value.empty() || !assign(*setting, value, state.parsed)) {
        return fail(state, "invalid value; see the reference INI for supported types and ranges");
    }
    state.seen[setting->seen_index] = true;
    if (setting->group == SettingGroup::led) {
        switch (static_cast<LedField>(setting->field_index)) {
        case LedField::critical_enter:
            state.critical_enter_line = state.line_number;
            break;
        case LedField::critical_exit:
            state.critical_exit_line = state.line_number;
            break;
        case LedField::detection_hidden_score:
            state.detection_hidden_line = state.line_number;
            break;
        case LedField::detection_alert_score:
            state.detection_alert_line = state.line_number;
            break;
        default:
            break;
        }
    }
    return true;
}

bool parse_line(std::string_view raw, ParseState& state) noexcept {
    const auto line = trim(raw.substr(0, raw.find_first_of(";#")));
    if (line.empty()) {
        return true;
    }
    if (line.front() == '[' && line.back() == ']') {
        return parse_section_line(line, state);
    }
    return parse_entry_line(line, state);
}

bool parse_lines(std::string_view text, ParseState& state) noexcept {
    while (!text.empty()) {
        ++state.line_number;
        const auto newline = text.find('\n');
        const auto raw = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        if (!parse_line(raw, state)) {
            return false;
        }
    }
    return true;
}
} // namespace

ConfigStatus parse_config(std::string_view text, Config& config, ConfigError* error) noexcept {
    if (error) {
        *error = {};
    }
    if (text.size() > maximum_config_bytes) {
        if (error) {
            *error = {.line = 0, .message = "configuration exceeds 16 KiB"};
        }
        return ConfigStatus::invalid;
    }
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.remove_prefix(3);
    }
    if (text.contains('\0')) {
        if (error) {
            *error = {.line = 0, .message = "embedded NUL"};
        }
        return ConfigStatus::invalid;
    }

    ParseState state{};
    state.error = error;
    if (!parse_lines(text, state)) {
        return ConfigStatus::invalid;
    }
    if (!inputnext::adapters::valid_led_settings(state.parsed.policy.effects.leds)) {
        const auto& leds = state.parsed.policy.effects.leds;
        std::size_t line = state.line_number;
        if (leds.critical_exit < leds.critical_enter) {
            line = std::max(state.critical_enter_line, state.critical_exit_line);
        } else if (leds.detection_alert_score <= leds.detection_hidden_score) {
            line = std::max(state.detection_hidden_line, state.detection_alert_line);
        }
        if (error) {
            *error = {.line = line,
                .message = "LED thresholds must be finite, bounded, and ordered"};
        }
        return ConfigStatus::invalid;
    }
    config = state.parsed;
    return ConfigStatus::loaded;
}

ConfigStatus
read_config(const std::filesystem::path& path, Config& config, ConfigError* error) noexcept {
    if (error) {
        *error = {};
    }
    try {
        std::error_code ec;
        const auto status = std::filesystem::status(path, ec);
        if (ec == std::errc::no_such_file_or_directory ||
            (!ec && !std::filesystem::exists(status))) {
            return ConfigStatus::missing;
        }
        if (ec || !std::filesystem::is_regular_file(status)) {
            return ConfigStatus::io_error;
        }
        std::ifstream file{path, std::ios::binary};
        if (!file) {
            return ConfigStatus::io_error;
        }
        std::string contents(maximum_config_bytes + 1, '\0');
        file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (file.bad() || (file.fail() && !file.eof())) {
            return ConfigStatus::io_error;
        }
        contents.resize(static_cast<std::size_t>(file.gcount()));
        return parse_config(contents, config, error);
    } catch (...) {
        return ConfigStatus::io_error;
    }
}
} // namespace fallout4_adapter
