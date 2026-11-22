#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/audio_haptics.hpp>
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
#include <utility>

namespace fallout_gamebryo {
namespace {
constexpr std::size_t maximum_config_bytes = 16U * 1024U;
enum class SettingGroup : std::uint8_t { global, body, trigger, led, controls };

struct SettingReference {
    SettingGroup group{};
    std::size_t entry{};
    std::size_t field{};
};

constexpr std::array global_keys{
    std::string_view{"Enabled"},
    std::string_view{"BodyEnabled"},
    std::string_view{"TriggersEnabled"},
    std::string_view{"MenuEffects"},
    std::string_view{"AudioHapticsEnabled"},
    std::string_view{"ResourceCues"},
    std::string_view{"UICues"},
    std::string_view{"GaitCues"},
    std::string_view{"Diagnostics"},
    std::string_view{"ControllerSlot"},
    std::string_view{"BodyScale"},
    std::string_view{"TriggerScale"},
    std::string_view{"CriticalEnter"},
    std::string_view{"CriticalExit"},
    std::string_view{"HealthWarning"},
    std::string_view{"ActionPointWarning"},
    std::string_view{"RadiationWarning"},
};
constexpr std::array profile_keys{
    std::string_view{"Enabled"},
    std::string_view{"StrengthScale"},
    std::string_view{"DurationScale"},
};
constexpr std::array trigger_keys{
    std::string_view{"Enabled"},
    std::string_view{"StrengthScale"},
    std::string_view{"ResistanceStart"},
};
constexpr std::array led_keys{
    std::string_view{"Enabled"},
    std::string_view{"HealthEnabled"},
    std::string_view{"RadiationEnabled"},
    std::string_view{"HealthHoldMs"},
    std::string_view{"CriticalEnter"},
    std::string_view{"CriticalExit"},
    std::string_view{"StealthEnabled"},
};
constexpr std::array control_keys{std::string_view{"AttackTrigger"},
    std::string_view{"AimTrigger"}};

constexpr auto settings_count = global_keys.size() + (body_profiles.size() * profile_keys.size()) +
                                (trigger_profiles.size() * trigger_keys.size()) + led_keys.size() +
                                control_keys.size();
using SeenSettings = std::array<bool, settings_count>;

struct SettingName {
    std::string_view section;
    std::string_view key;
};

struct AssignmentLine {
    std::string_view text;
    std::string_view section;
    std::size_t number{};
};

std::string_view trim(std::string_view text) noexcept {
    constexpr std::string_view whitespace{" \t\r"};
    const auto begin = text.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) {
        return {};
    }
    return text.substr(begin, text.find_last_not_of(whitespace) - begin + 1U);
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

bool parse_float(std::string_view text, float minimum, float maximum, float& output) noexcept {
    float value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value) ||
        value < minimum || value > maximum) {
        return false;
    }
    output = value;
    return true;
}

bool parse_uint(std::string_view text,
    std::uint32_t minimum,
    std::uint32_t maximum,
    std::uint32_t& output) noexcept {
    std::uint32_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < minimum ||
        value > maximum) {
        return false;
    }
    output = value;
    return true;
}

template <std::size_t Size>
std::optional<std::size_t> key_index(const std::array<std::string_view, Size>& keys,
    std::string_view key) noexcept {
    for (std::size_t index{}; index < keys.size(); ++index) {
        if (keys[index] == key) {
            return index;
        }
    }
    return std::nullopt;
}

bool assign_global(std::size_t index, std::string_view value, Settings& settings) noexcept {
    switch (index) {
    case 0:
        return boolean(value, settings.enabled);
    case 1:
        return boolean(value, settings.body_enabled);
    case 2:
        return boolean(value, settings.triggers_enabled);
    case 3:
        return boolean(value, settings.menu_effects);
    case 4:
        return boolean(value, settings.audio_haptics_enabled);
    case 5:
        return boolean(value, settings.resource_cues);
    case 6:
        return boolean(value, settings.ui_cues);
    case 7:
        return boolean(value, settings.gait_cues);
    case 8:
        return boolean(value, settings.diagnostics);
    case 9:
        return parse_uint(value, 0U, 3U, settings.controller_slot);
    case 10:
        return parse_float(value, 0.0F, 1.0F, settings.body_scale);
    case 11:
        return parse_float(value, 0.0F, 1.0F, settings.trigger_scale);
    case 12:
        return parse_float(value, 0.0F, 1.0F, settings.critical_enter);
    case 13:
        return parse_float(value, 0.0F, 1.0F, settings.critical_exit);
    case 14:
        return parse_float(value, 0.0F, 1.0F, settings.health_warning);
    case 15:
        return parse_float(value, 0.0F, 1.0F, settings.action_point_warning);
    case 16:
        return parse_float(value, 0.0F, 1.0F, settings.radiation_warning);
    default:
        return false;
    }
}

bool assign_body(const SettingReference& setting,
    std::string_view value,
    Settings& settings) noexcept {
    auto& profile = settings.body[setting.entry];
    switch (setting.field) {
    case 0:
        return boolean(value, profile.enabled);
    case 1:
        return parse_float(value, 0.0F, 2.0F, profile.strength_scale);
    case 2:
        return parse_float(value, 0.25F, 2.0F, profile.duration_scale);
    default:
        return false;
    }
}

bool assign_trigger(const SettingReference& setting,
    std::string_view value,
    Settings& settings) noexcept {
    auto& profile = settings.trigger[setting.entry];
    switch (setting.field) {
    case 0:
        return boolean(value, profile.enabled);
    case 1:
        return parse_float(value, 0.0F, 2.0F, profile.strength_scale);
    case 2:
        return parse_float(value, 0.0F, 1.0F, profile.resistance_start);
    default:
        return false;
    }
}

bool assign_led(std::size_t key, std::string_view value, Settings& settings) noexcept {
    std::uint32_t duration{};
    switch (key) {
    case 0:
        return boolean(value, settings.led.enabled);
    case 1:
        return boolean(value, settings.led.health_enabled);
    case 2:
        return boolean(value, settings.led.radiation_enabled);
    case 3:
        if (!parse_uint(value, 1U, 60000U, duration)) {
            return false;
        }
        settings.led.health_hold_ms = duration;
        return true;
    case 4:
        return parse_float(value, 0.0F, 1.0F, settings.led.critical_enter);
    case 5:
        return parse_float(value, 0.0F, 1.0F, settings.led.critical_exit);
    case 6:
        return boolean(value, settings.led.stealth_enabled);
    default:
        return false;
    }
}

bool assign_control(std::size_t key, std::string_view value, Settings& settings) noexcept {
    TriggerSide side{};
    if (value == "left") {
        side = TriggerSide::left;
    } else if (value == "right") {
        side = TriggerSide::right;
    } else if (value != "none") {
        return false;
    }
    if (key == 0U) {
        settings.attack_trigger = side;
    } else if (key == 1U) {
        settings.aim_trigger = side;
    } else {
        return false;
    }
    return true;
}

std::size_t seen_index(const SettingReference& setting) noexcept {
    if (setting.group == SettingGroup::global) {
        return setting.field;
    }
    if (setting.group == SettingGroup::body) {
        return global_keys.size() + (setting.entry * profile_keys.size()) + setting.field;
    }
    if (setting.group == SettingGroup::trigger) {
        return global_keys.size() + (body_profiles.size() * profile_keys.size()) +
               (setting.entry * trigger_keys.size()) + setting.field;
    }
    if (setting.group == SettingGroup::led) {
        return global_keys.size() + (body_profiles.size() * profile_keys.size()) +
               (trigger_profiles.size() * trigger_keys.size()) + setting.field;
    }
    return global_keys.size() + (body_profiles.size() * profile_keys.size()) +
           (trigger_profiles.size() * trigger_keys.size()) + led_keys.size() + setting.field;
}

bool global_field_allowed(std::string_view section, std::size_t field) noexcept {
    if (section == "Logging") {
        return field == 8U;
    }
    if (section == "General") {
        return field == 0U || field == 9U;
    }
    return section == "Effects" && ((field >= 1U && field <= 7U) || (field >= 10U && field <= 16U));
}

std::optional<SettingReference> find_setting(SettingName name) noexcept {
    const auto [section, key] = name;
    if (section == "General" || section == "Effects" || section == "Logging") {
        const auto field = key_index(global_keys, key);
        if (field && global_field_allowed(section, *field)) {
            return SettingReference{.group = SettingGroup::global, .field = *field};
        }
        return std::nullopt;
    }
    if (section == "LED") {
        const auto field = key_index(led_keys, key);
        if (field) {
            return SettingReference{.group = SettingGroup::led, .field = *field};
        }
        return std::nullopt;
    }
    if (section == "Controls") {
        const auto field = key_index(control_keys, key);
        if (field) {
            return SettingReference{.group = SettingGroup::controls, .field = *field};
        }
        return std::nullopt;
    }
    for (std::size_t entry{}; entry < body_profiles.size(); ++entry) {
        if (section == body_profiles[entry].section) {
            const auto field = key_index(profile_keys, key);
            return field ? std::optional{SettingReference{.group = SettingGroup::body,
                               .entry = entry,
                               .field = *field}}
                         : std::nullopt;
        }
    }
    for (std::size_t entry{}; entry < trigger_profiles.size(); ++entry) {
        if (section == trigger_profiles[entry].section) {
            const auto field = key_index(trigger_keys, key);
            return field ? std::optional{SettingReference{.group = SettingGroup::trigger,
                               .entry = entry,
                               .field = *field}}
                         : std::nullopt;
        }
    }
    return std::nullopt;
}

void set_error(ConfigError* error, std::size_t line, std::string_view message) noexcept;

bool assign_setting(const SettingReference& setting,
    std::string_view value,
    Settings& settings) noexcept {
    switch (setting.group) {
    case SettingGroup::global:
        return assign_global(setting.field, value, settings);
    case SettingGroup::body:
        return assign_body(setting, value, settings);
    case SettingGroup::trigger:
        return assign_trigger(setting, value, settings);
    case SettingGroup::led:
        return assign_led(setting.field, value, settings);
    case SettingGroup::controls:
        return assign_control(setting.field, value, settings);
    }
    return false;
}

bool parse_assignment(const AssignmentLine& assignment,
    Settings& settings,
    SeenSettings& seen,
    ConfigError* error) noexcept {
    const auto [line, section, line_number] = assignment;
    const auto separator = line.find('=');
    if (separator == std::string_view::npos) {
        set_error(error, line_number, "expected key=value");
        return false;
    }
    const auto key = trim(line.substr(0U, separator));
    const auto value = trim(line.substr(separator + 1U));
    if (key.empty() || value.empty()) {
        set_error(error, line_number, "empty key or value");
        return false;
    }
    const auto setting = find_setting(SettingName{.section = section, .key = key});
    if (!setting) {
        set_error(error, line_number, "unknown section or setting");
        return false;
    }
    const auto index = seen_index(*setting);
    if (seen[index]) {
        set_error(error, line_number, "duplicate setting");
        return false;
    }
    seen[index] = true;
    if (!assign_setting(*setting, value, settings)) {
        set_error(error, line_number, "setting value is outside its supported range");
        return false;
    }
    return true;
}

void set_error(ConfigError* error, std::size_t line, std::string_view message) noexcept {
    if (error != nullptr) {
        *error = {.line = line, .message = message};
    }
}

void clear_audio(Settings& settings) noexcept {
    for (auto& profile : settings.body) {
        profile.audio_haptic.reset();
    }
}

std::size_t overlay_audio(const inputnext::adapters::AudioHapticBank& bank,
    Settings& settings,
    inputnext::adapters::AudioHapticGame game) noexcept {
    if (!settings.audio_haptics_enabled || bank.game != game) {
        return 0U;
    }
    std::size_t loaded{};
    for (const auto& item : bank.entries) {
        const auto profile = std::ranges::find(body_profiles, item.section, &BodyProfile::section);
        if (profile == body_profiles.end() ||
            !inputnext::adapters::valid_audio_haptic_profile(item.profile)) {
            continue;
        }
        settings.body[static_cast<std::size_t>(profile->effect)].audio_haptic = item.profile;
        ++loaded;
    }
    return loaded;
}

std::size_t count_audio(const Settings& settings) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(settings.body,
        [](const auto& profile) { return profile.audio_haptic.has_value(); }));
}

} // namespace

ConfigStatus parse_config(std::string_view text, Settings& settings, ConfigError* error) noexcept {
    if (text.size() > maximum_config_bytes) {
        set_error(error, 0U, "configuration exceeds the size limit");
        return ConfigStatus::invalid;
    }

    Settings parsed{};
    SeenSettings seen{};
    std::string_view section{"General"};
    std::size_t line_number{};
    while (!text.empty()) {
        ++line_number;
        const auto end = text.find('\n');
        const auto line = trim(text.substr(0U, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1U);
        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1U, line.size() - 2U));
            if (section.empty()) {
                set_error(error, line_number, "empty section name");
                return ConfigStatus::invalid;
            }
            continue;
        }
        const auto assignment =
            AssignmentLine{.text = line, .section = section, .number = line_number};
        if (!parse_assignment(assignment, parsed, seen, error)) {
            return ConfigStatus::invalid;
        }
    }

    if (parsed.critical_exit <= parsed.critical_enter ||
        parsed.led.critical_exit <= parsed.led.critical_enter ||
        !inputnext::adapters::valid_led_settings(parsed.led)) {
        set_error(error, line_number, "critical health thresholds must be ordered and valid");
        return ConfigStatus::invalid;
    }
    settings = parsed;
    if (error != nullptr) {
        *error = {};
    }
    return ConfigStatus::loaded;
}

ConfigStatus
read_config(const std::filesystem::path& path, Settings& settings, ConfigError* error) noexcept {
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            std::error_code ec;
            const bool exists = std::filesystem::exists(path, ec);
            if (!exists && !ec) {
                set_error(error, 0U, {});
                return ConfigStatus::missing;
            }
            set_error(error, 0U, "could not open configuration file");
            return ConfigStatus::io_error;
        }
        input.seekg(0, std::ios::end);
        const auto size = static_cast<std::streamoff>(input.tellg());
        if (size < 0) {
            set_error(error, 0U, "could not determine configuration file size");
            return ConfigStatus::io_error;
        }
        if (std::cmp_greater(size, maximum_config_bytes)) {
            set_error(error, 0U, "configuration exceeds the size limit");
            return ConfigStatus::invalid;
        }
        std::string text(static_cast<std::size_t>(size), '\0');
        input.seekg(0, std::ios::beg);
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (!input) {
            set_error(error, 0U, "could not read configuration file");
            return ConfigStatus::io_error;
        }
        return parse_config(text, settings, error);
    } catch (...) {
        set_error(error, 0U, "could not read configuration file");
        return ConfigStatus::io_error;
    }
}

std::size_t bind_audio_haptics(const inputnext::adapters::AudioHapticBank& bank,
    Settings& settings,
    inputnext::adapters::AudioHapticGame game) noexcept {
    clear_audio(settings);
    return overlay_audio(bank, settings, game);
}

AudioHapticLoadResult load_audio_haptics(const std::filesystem::path& config_path,
    Settings& settings,
    inputnext::adapters::AudioHapticGame game) noexcept {
    using inputnext::adapters::AudioHapticBankStatus;
    using Override = AudioHapticOverrideStatus;
    clear_audio(settings);
    if (!settings.audio_haptics_enabled) {
        return {};
    }
    auto status = Override::not_found;
    try {
        const auto builtin = inputnext::adapters::builtin_audio_haptic_bank(game);
        if (builtin.status == AudioHapticBankStatus::loaded && builtin.bank.game == game) {
            static_cast<void>(overlay_audio(builtin.bank, settings, game));
        }
        auto path = config_path;
        path.replace_extension(L".audiohaptics");
        const auto external = inputnext::adapters::load_audio_haptic_bank(path);
        switch (external.status) {
        case AudioHapticBankStatus::not_found:
            break;
        case AudioHapticBankStatus::invalid:
            status = Override::invalid;
            break;
        case AudioHapticBankStatus::io_error:
            status = Override::io_error;
            break;
        case AudioHapticBankStatus::loaded:
            if (external.bank.game != game) {
                status = Override::wrong_game;
                break;
            }
            static_cast<void>(overlay_audio(external.bank, settings, game));
            status = Override::loaded;
            break;
        }
    } catch (...) {
        status = Override::io_error;
    }
    return {.override_status = status, .loaded_profiles = count_audio(settings)};
}
} // namespace fallout_gamebryo
