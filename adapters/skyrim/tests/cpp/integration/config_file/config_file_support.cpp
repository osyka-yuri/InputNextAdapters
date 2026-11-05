#include "config_file_support.hpp"
#include "support/native_test_checks.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <set>
#include <skyrim_adapter/effect_settings.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace skyrim_adapter::tests::config_file {
namespace {
std::string_view trim_ini(std::string_view value) {
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
} // namespace

bool extract_catalogue(std::string_view contents, ConfigCatalogue& catalogue) {
    std::istringstream lines{std::string{contents}};
    std::string line;
    std::string current_section;
    while (std::getline(lines, line)) {
        const auto trimmed = trim_ini(line);
        if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.size() >= 3 && trimmed.back() == ']') {
            current_section = std::string{trim_ini(trimmed.substr(1, trimmed.size() - 2))};
            if (current_section.empty() || !catalogue.try_emplace(current_section).second) {
                return false;
            }
            continue;
        }
        const auto equals = trimmed.find('=');
        if (current_section.empty() || equals == std::string_view::npos) {
            return false;
        }
        const auto key = trim_ini(trimmed.substr(0, equals));
        if (key.empty() || !catalogue[current_section].emplace(std::string{key}).second) {
            return false;
        }
    }
    return true;
}

bool read_text_file(const std::filesystem::path& path, std::string& contents) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    contents.assign(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
    return !file.bad();
}

ConfigCatalogue expected_reference_catalogue() {
    using namespace skyrim_adapter;
    std::set<std::string> root_keys{
        "ControllerSlot",
        "RumbleScale",
        "BodyEnabled",
        "AudioHapticsEnabled",
        "RespectGameVibration",
    };
#ifndef INPUTNEXT_SKYRIM_LE_CONFIG
    root_keys.emplace("TriggerScale");
    root_keys.emplace("TriggersEnabled");
#endif
    ConfigCatalogue expected{{"InputNextSkyrim", root_keys}};
    std::set<std::string> led_keys{"Enabled",
        "HealthEnabled",
        "HealthHoldMs",
        "HealingTransitionMs",
        "CriticalEnter",
        "CriticalExit"};
#ifndef INPUTNEXT_SKYRIM_LE_CONFIG
    led_keys.emplace("StealthEnabled");
    led_keys.emplace("DetectionHiddenScore");
    led_keys.emplace("DetectionAlertScore");
#endif
    expected.emplace("LED", std::move(led_keys));

#ifdef INPUTNEXT_SKYRIM_LE_CONFIG
    const std::set<std::string_view> supported_le_body_profiles{
        "Body.Melee.Unarmed.Swing",
        "Body.Melee.Dagger.Swing",
        "Body.Melee.Sword.Swing",
        "Body.Melee.Axe.Swing",
        "Body.Melee.Mace.Swing",
        "Body.Melee.TwoHanded.Swing",
        "Body.Melee.Unarmed.Hit",
        "Body.Melee.Dagger.Hit",
        "Body.Melee.Sword.Hit",
        "Body.Melee.Axe.Hit",
        "Body.Melee.Mace.Hit",
        "Body.Melee.TwoHanded.Hit",
        "Body.IncomingHit",
        "Body.Bow.Draw",
        "Body.Bow.Release",
        "Body.Voice",
        "Body.CriticalHealth",
        "Body.World.StaminaDepleted",
        "Body.Transformation.Werewolf",
        "Body.Transformation.VampireLord",
    };
#endif
    for (const auto& descriptor : effect_descriptors) {
#ifdef INPUTNEXT_SKYRIM_LE_CONFIG
        if (!supported_le_body_profiles.contains(descriptor.section)) {
            continue;
        }
#endif
        std::set<std::string> keys{"Enabled", "HDScale", "FrequencyHz"};
        if (descriptor.classic) {
            keys.emplace("ClassicScale");
        }
        if (descriptor.transient) {
            keys.emplace("DurationScale");
        }
        expected.emplace(std::string{descriptor.section}, std::move(keys));
    }
#ifndef INPUTNEXT_SKYRIM_LE_CONFIG
    for (const auto& descriptor : trigger_descriptors) {
        std::set<std::string> keys{"Enabled", "StrengthScale", "ResistanceStart"};
        if (descriptor.vibration) {
            keys.emplace("VibrationFrequencyCode");
        }
        expected.emplace(std::string{descriptor.section}, std::move(keys));
    }
#endif
    return expected;
}

TemporaryConfigDirectory::~TemporaryConfigDirectory() noexcept {
    try {
        if (owns_directory_) {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
            if (error) {
                ++failure_count();
                (void)std::fputs("FAIL: temporary configuration directory cleanup failed\n",
                    stderr);
            }
        }
    } catch (...) {
        ++failure_count();
        (void)std::fputs("FAIL: temporary configuration directory cleanup threw\n", stderr);
    }
}

bool TemporaryConfigDirectory::create() {
    std::error_code error;
    const auto temporary_root = std::filesystem::temp_directory_path(error);
    if (error) {
        return false;
    }
    static std::atomic<unsigned long long> sequence{};
    for (unsigned int attempt = 0; attempt != 128; ++attempt) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto unique = sequence.fetch_add(1, std::memory_order_relaxed);
        auto candidate =
            temporary_root / ("inputnext-skyrim-config-tests-" + std::to_string(nonce) + "-" +
                                 std::to_string(unique) + "-" + std::to_string(attempt));
        error.clear();
        if (std::filesystem::create_directory(candidate, error)) {
            path_ = std::move(candidate);
            owns_directory_ = true;
            return true;
        }
        if (error && error != std::errc::file_exists) {
            return false;
        }
    }
    return false;
}

std::filesystem::path TemporaryConfigDirectory::file(std::string_view name) const {
    return path_ / std::string{name};
}

TemporaryConfigDirectory::operator bool() const noexcept {
    return owns_directory_;
}
} // namespace skyrim_adapter::tests::config_file
