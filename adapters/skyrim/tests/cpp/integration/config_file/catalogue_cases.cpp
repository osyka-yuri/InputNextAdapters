#include "config/config_test_support.hpp"
#include "config_file_cases.hpp"
#include "config_file_support.hpp"
#include "support/native_test_checks.hpp"
#include <filesystem>
#include <memory>
#include <set>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/native/config.hpp>
#include <string>

namespace skyrim_adapter::tests::config_file {
void shipped_config_parsing_and_defaults(const std::filesystem::path& working_path,
    const std::filesystem::path& reference_path) {
    using namespace skyrim_adapter::native;
    std::string working_text;
    std::string reference_text;
    check(read_text_file(working_path, working_text), "working Skyrim config file is readable");
    check(read_text_file(reference_path, reference_text),
        "reference Skyrim config file is readable");
    if (working_text.empty() || reference_text.empty()) {
        check(false, "both shipped Skyrim config files contain their documented controls");
        return;
    }
    ConfigError working_error{.line = 99,
        .section = "old section",
        .key = "old key",
        .message = "old message"};
    ConfigError reference_error{.line = 99,
        .section = "old section",
        .key = "old key",
        .message = "old message"};
    const auto working = std::make_unique<NativeConfig>();
    const auto reference = std::make_unique<NativeConfig>();
    check(read_config_file(working_path, *working, &working_error) == ConfigStatus::loaded,
        "working Skyrim config parses through the production file reader");
    check(read_config_file(reference_path, *reference, &reference_error) == ConfigStatus::loaded,
        "reference Skyrim config parses through the production file reader");
    check(working_error.line == 0 && working_error.section.empty() && working_error.key.empty() &&
              working_error.message.empty() && reference_error.line == 0 &&
              reference_error.section.empty() && reference_error.key.empty() &&
              reference_error.message.empty(),
        "successful config reads clear stale diagnostics");
    const auto neutral = std::make_unique<NativeConfig>();
    check(skyrim_adapter::tests::config::same_config(*working, *neutral) &&
              skyrim_adapter::tests::config::same_config(*reference, *neutral) &&
              skyrim_adapter::tests::config::same_config(*working, *reference),
        "both shipped config files produce identical neutral defaults");
}

void working_config_catalogue(const std::filesystem::path& working_path) {
    std::string working_text;
    check(read_text_file(working_path, working_text), "working Skyrim config file is readable");
    if (working_text.empty()) {
        check(false, "working Skyrim config contains its documented controls");
        return;
    }
    std::set<std::string> root_keys{"ControllerSlot",
        "RumbleScale",
        "BodyEnabled",
        "RespectGameVibration"};
#ifndef INPUTNEXT_SKYRIM_LE_CONFIG
    root_keys.emplace("TriggerScale");
    root_keys.emplace("TriggersEnabled");
#endif
    std::set<std::string> led_keys{"Enabled",
        "HealthEnabled",
        "HealthHoldMs",
        "HealingTransitionMs",
        "CriticalEnter",
        "CriticalExit"};
#ifndef INPUTNEXT_SKYRIM_LE_CONFIG
    led_keys.insert("StealthEnabled");
    led_keys.insert("DetectionHiddenScore");
    led_keys.insert("DetectionAlertScore");
#endif
    ConfigCatalogue working_catalogue;
    const bool working_catalogue_read = extract_catalogue(working_text, working_catalogue);
    check(working_catalogue_read &&
              working_catalogue ==
                  ConfigCatalogue{{"InputNextSkyrim", root_keys}, {"LED", led_keys}},
        "working config contains the global controls and supported LED settings");
}

void reference_config_catalogue(const std::filesystem::path& reference_path) {
    using namespace skyrim_adapter;
    using namespace skyrim_adapter::native;
    std::string reference_text;
    check(read_text_file(reference_path, reference_text),
        "reference Skyrim config file is readable");
    if (reference_text.empty()) {
        check(false, "reference Skyrim config lists all supported profile controls");
        return;
    }
    ConfigCatalogue const expected_reference = expected_reference_catalogue();
    ConfigCatalogue reference_catalogue;
    const bool reference_catalogue_read = extract_catalogue(reference_text, reference_catalogue);
    check(reference_catalogue_read && reference_catalogue == expected_reference,
        "reference config lists supported profile keys with exact capabilities");
#ifdef INPUTNEXT_SKYRIM_LE_CONFIG
    check(effect_descriptors.size() == 52 && trigger_descriptors.size() == 11 &&
              expected_reference.size() == 22,
        "LE reference lists only its 20 supported body profiles and shared controls");
#else
    check(effect_descriptors.size() == 52 && trigger_descriptors.size() == 11 &&
              expected_reference.size() == 65,
        "reference config covers every body, trigger, and LED setting");
#endif
}
} // namespace skyrim_adapter::tests::config_file
