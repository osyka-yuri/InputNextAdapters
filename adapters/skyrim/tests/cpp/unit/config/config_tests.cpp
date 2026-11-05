#include "config_test_support.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/native/config.hpp>
#include <string>
#include <string_view>

using namespace skyrim_adapter::tests::config;

TEST_CASE("skyrim.config.defaults_and_root_values", "[unit][skyrim][config]") {
    using skyrim_adapter::EffectId;
    using namespace skyrim_adapter::native;

    const auto config_storage = std::make_unique<NativeConfig>();
    auto& config = *config_storage;
    INFO("empty optional config retains neutral defaults");
    REQUIRE(parse_config("; defaults\n", config));
    CHECK(config.slot == 0);
    CHECK(config.trigger_scale == 1.0F);
    CHECK(config.rumble_scale == 1.0F);
    CHECK(config.effects.body_enabled);
    CHECK(config.effects.triggers_enabled);
    CHECK(config.effects.respect_game_vibration);
    REQUIRE(!config.effects.body.empty());
    CHECK(config.effects.body.front().enabled);
    CHECK(config.effects.body.front().hd_scale == 1.0F);
    CHECK(config.effects.body.front().classic_scale == 1.0F);
    CHECK(config.effects.body.front().duration_scale == 1.0F);
    CHECK(config.effects.body.front().frequency_hz == 0.0F);
    CHECK(!config.effects.body[static_cast<std::size_t>(EffectId::mounted_walk)].enabled);
    CHECK(!config.effects.body[static_cast<std::size_t>(EffectId::mounted_run)].enabled);
    CHECK(config.effects.body[static_cast<std::size_t>(EffectId::mounted_sprint)].enabled);
    REQUIRE(!config.effects.triggers.empty());
    CHECK(config.effects.triggers.front().enabled);
    CHECK(config.effects.triggers.front().strength_scale == 1.0F);
    CHECK(!config.effects.triggers.front().start.has_value());
    CHECK(!config.effects.triggers.front().frequency_code.has_value());

    INFO("documented config values parse including disabled channels");
    REQUIRE(parse_config("[InputNextSkyrim]\nControllerSlot=3\nTriggerScale=0.25\nRumbleScale=0\n",
        config));
    CHECK(config.slot == 3);
    CHECK(config.trigger_scale == 0.25F);
    CHECK(config.rumble_scale == 0.0F);
    CHECK(config.effects.respect_game_vibration);

    INFO("UTF-8 BOM, CRLF, inline comments, and finite decimal scales are accepted");
    REQUIRE(parse_config(
        "\xEF\xBB\xBF[InputNextSkyrim]\r\nControllerSlot=1 ; selected slot\r\nTriggerScale=1e-1\r\n"
        "RespectGameVibration=false\r\n",
        config));
    CHECK(config.slot == 1);
    CHECK(config.trigger_scale == 0.1F);
    CHECK(config.rumble_scale == 1.0F);
    CHECK(!config.effects.respect_game_vibration);
}

TEST_CASE("skyrim.config.led_defaults_use_positive_detection_range", "[unit][skyrim][config]") {
    using namespace skyrim_adapter::native;

    const auto config_storage = std::make_unique<NativeConfig>();
    auto& config = *config_storage;
    ConfigError error{};
    REQUIRE(parse_config("[InputNextSkyrim]\nControllerSlot=0\n", config, &error));
    CHECK(config.effects.leds.detection_hidden_score == 0);
    CHECK(config.effects.leds.detection_alert_score == 100);
}

TEST_CASE("skyrim.config.led_settings_are_validated_as_one_group", "[unit][skyrim][config]") {
    using namespace skyrim_adapter::native;

    const auto config_storage = std::make_unique<NativeConfig>();
    auto& config = *config_storage;
    ConfigError error{};
    INFO("shared health settings accept bounded duration and critical thresholds");
    REQUIRE(parse_config("[LED]\nEnabled=false\nHealthEnabled=false\nHealthHoldMs=60000\n"
                         "HealingTransitionMs=0\nCriticalEnter=0\nCriticalExit=1\n",
        config,
        &error));
    CHECK(!config.effects.leds.enabled);
    CHECK(!config.effects.leds.health_enabled);
    CHECK(config.effects.leds.health_hold_ms == 60000U);
    CHECK(config.effects.leds.healing_transition_ms == 0U);
    CHECK(config.effects.leds.critical_enter == 0.0F);
    CHECK(config.effects.leds.critical_exit == 1.0F);

#ifndef INPUTNEXT_SKYRIM_LE_CONFIG
    INFO("SE supports actual stealth and authored raw-score endpoints");
    REQUIRE(parse_config(
        "[LED]\nStealthEnabled=false\nDetectionHiddenScore=-300\nDetectionAlertScore=55\n",
        config,
        &error));
    CHECK(!config.effects.leds.stealth_enabled);
    CHECK(config.effects.leds.detection_hidden_score == -300);
    CHECK(config.effects.leds.detection_alert_score == 55);
#else
    INFO("LE rejects numeric stealth controls because its native source is unavailable");
    CHECK_FALSE(parse_config("[LED]\nStealthEnabled=true\n", config, &error));
    CHECK(error.section == "LED");
    CHECK(error.key == "StealthEnabled");
#endif

    const auto prior_storage = make_sample_prior_config();
    const auto& prior = *prior_storage;
    config = prior;
    INFO("inconsistent hysteresis thresholds reject the whole candidate without partial commit");
    CHECK_FALSE(parse_config("[LED]\nCriticalEnter=0.8\nCriticalExit=0.4\n", config, &error));
    CHECK(error.section == "LED");
    CHECK(same_config(config, prior));
#ifndef INPUTNEXT_SKYRIM_LE_CONFIG
    INFO("unordered authored detection endpoints reject the complete candidate too");
    CHECK_FALSE(
        parse_config("[LED]\nDetectionHiddenScore=20\nDetectionAlertScore=-20\n", config, &error));
    CHECK(error.section == "LED");
    CHECK(same_config(config, prior));
#endif
}

TEST_CASE("skyrim.config.profile_catalogue_mappings", "[unit][skyrim][config]") {
    using namespace skyrim_adapter::native;

    INFO("typed schema exposes all body and trigger profiles");
    CHECK(skyrim_adapter::effect_descriptors.size() == 52);
    CHECK(skyrim_adapter::trigger_descriptors.size() == 11);

    const auto config_storage = std::make_unique<NativeConfig>();
    auto& config = *config_storage;
    std::string profiles{"[InputNextSkyrim]\nBodyEnabled=false\nTriggersEnabled="
                         "false\nRespectGameVibration=false\n"};
    for (const auto& descriptor : skyrim_adapter::effect_descriptors) {
        profiles += '[';
        profiles += descriptor.section;
        profiles += "]\nEnabled=false\nHDScale=1.5\n";
        if (descriptor.classic) {
            profiles += "ClassicScale=0.5\n";
        }
        if (descriptor.transient) {
            profiles += "DurationScale=1.25\n";
        }
        profiles += "FrequencyHz=120\n";
    }
    for (const auto& descriptor : skyrim_adapter::trigger_descriptors) {
        profiles += '[';
        profiles += descriptor.section;
        profiles += "]\nEnabled=false\nStrengthScale=1.5\nResistanceStart=0.25\n";
        if (descriptor.vibration) {
            profiles += "VibrationFrequencyCode=255\n";
        }
    }

    INFO("every catalogued descriptor and supported key parses together");
    CHECK(parse_config(profiles, config));

    INFO("global body, trigger, and game preference controls parse independently");
    CHECK(!config.effects.body_enabled);
    CHECK(!config.effects.triggers_enabled);
    CHECK(!config.effects.respect_game_vibration);

    REQUIRE(config.effects.body.size() >= skyrim_adapter::effect_descriptors.size());
    for (std::size_t index = 0; index < skyrim_adapter::effect_descriptors.size(); ++index) {
        const auto& descriptor = skyrim_adapter::effect_descriptors[index];
        const auto& adjustment = config.effects.body[index];
        const bool has_classic_output =
            descriptor.id != skyrim_adapter::EffectId::bow_tension_drawn &&
            descriptor.id != skyrim_adapter::EffectId::generic_held;

        CAPTURE(index, descriptor.section);
        INFO("body descriptor order matches EffectId array order");
        CHECK(static_cast<std::size_t>(descriptor.id) == index);

        INFO("classic capability is absent only for profiles without an authored classic consumer");
        CHECK(descriptor.classic == has_classic_output);

        INFO("each body profile maps supported tuning keys to its typed adjustment");
        CHECK(!adjustment.enabled);
        CHECK(adjustment.hd_scale == 1.5F);
        CHECK(adjustment.classic_scale == (descriptor.classic ? 0.5F : 1.0F));
        CHECK(adjustment.frequency_hz == 120.0F);
        CHECK(adjustment.duration_scale == (descriptor.transient ? 1.25F : 1.0F));
    }

    REQUIRE(config.effects.triggers.size() >= skyrim_adapter::trigger_descriptors.size());
    for (std::size_t index = 0; index < skyrim_adapter::trigger_descriptors.size(); ++index) {
        const auto& descriptor = skyrim_adapter::trigger_descriptors[index];
        const auto& adjustment = config.effects.triggers[index];

        CAPTURE(index, descriptor.section);
        INFO("trigger descriptor order matches TriggerId array order");
        CHECK(static_cast<std::size_t>(descriptor.id) == index);

        INFO("each trigger profile accepts shared keys and vibration-only device frequency codes");
        CHECK(!adjustment.enabled);
        CHECK(adjustment.strength_scale == 1.5F);
        CHECK(adjustment.start == 0.25F);
        CHECK(adjustment.frequency_code ==
              (descriptor.vibration ? std::optional<unsigned>{255U} : std::nullopt));
    }
}

TEST_CASE("skyrim.config.inclusive_endpoints_and_default_values", "[unit][skyrim][config]") {
    using namespace skyrim_adapter::native;

    const auto config_storage = std::make_unique<NativeConfig>();
    auto& config = *config_storage;
    INFO("inclusive tuning bounds accept valid endpoint values");
    CHECK(parse_config(
        "[Body.Melee.Unarmed.Swing]\nHDScale=0\nClassicScale=2\nDurationScale=0.25\n"
        "FrequencyHz=20\n[Trigger.Fire]\nStrengthScale=0\nResistanceStart=0\n"
        "VibrationFrequencyCode=0\n[Body.Melee.Sword.Swing]\nHDScale=2\nClassicScale=0\n"
        "DurationScale=2\nFrequencyHz=1000\n[Trigger.Shock]\nStrengthScale=2\n"
        "ResistanceStart=1\nVibrationFrequencyCode=255\n",
        config));
    CHECK(config.effects.body[static_cast<std::size_t>(skyrim_adapter::EffectId::unarmed_swing)]
              .hd_scale == 0.0F);
    CHECK(
        config.effects.triggers[static_cast<std::size_t>(skyrim_adapter::TriggerId::fire)].start ==
        0.0F);
    CHECK(config.effects.triggers[static_cast<std::size_t>(skyrim_adapter::TriggerId::fire)]
              .frequency_code == 0U);
    CHECK(config.effects.body[static_cast<std::size_t>(skyrim_adapter::EffectId::sword_swing)]
              .frequency_hz == 1000.0F);
    CHECK(config.effects.triggers[static_cast<std::size_t>(skyrim_adapter::TriggerId::shock)]
              .frequency_code == 255U);

    std::string defaults;
    for (const auto& descriptor : skyrim_adapter::effect_descriptors) {
        defaults += '[';
        defaults += descriptor.section;
        defaults += "]\nFrequencyHz= DeFaUlT ; built-in frequency\n";
    }
    for (const auto& descriptor : skyrim_adapter::trigger_descriptors) {
        defaults += '[';
        defaults += descriptor.section;
        defaults += "]\nResistanceStart= DeFaUlT\n";
        if (descriptor.vibration) {
            defaults += "VibrationFrequencyCode= DEFAULT\n";
        }
    }
    INFO("default spelling parses for every applicable profile");
    CHECK(parse_config(defaults, config));
    for (const auto& adjustment : config.effects.body) {
        INFO("default HD frequency retains the built-in recipe");
        CHECK(adjustment.frequency_hz == 0.0F);
    }
    for (const auto& adjustment : config.effects.triggers) {
        INFO("default trigger overrides retain built-in device values");
        CHECK(!adjustment.start.has_value());
        CHECK(!adjustment.frequency_code.has_value());
    }
}

TEST_CASE("skyrim.config.parser_diagnostics_and_ownership", "[unit][skyrim][config]") {
    using namespace skyrim_adapter::native;

    const auto prior_storage = make_sample_prior_config();
    const auto& prior = *prior_storage;

    struct ExpectedDiagnostic {
        std::size_t line{};
        std::string_view section;
        std::string_view key;
        std::string_view message;
    };

    const auto expect_diagnostic =
        [&prior](std::string_view contents, ExpectedDiagnostic expected, std::string_view label) {
            const auto candidate_storage = std::make_unique<NativeConfig>(prior);
            auto& candidate = *candidate_storage;
            ConfigError error{
                .line = 99,
                .section = "stale section",
                .key = "stale key",
                .message = "stale message",
            };
            INFO(label);
            CHECK(!parse_config(contents, candidate, &error));
            INFO("first parser error reports its physical line, owned context, and expected value");
            CHECK(error.line == expected.line);
            CHECK(error.section == expected.section);
            CHECK(error.key == expected.key);
            CHECK(error.message == expected.message);
            INFO("diagnostic rejection leaves the complete config unchanged");
            CHECK(same_config(candidate, prior));
        };

    expect_diagnostic("[InputNextSkyrim\n",
        ExpectedDiagnostic{
            .line = 1,
            .section = "InputNextSkyrim",
            .key = "",
            .message = "Expected a section header ending with ']'.",
        },
        "malformed section header reports a diagnostic");
    expect_diagnostic("[InputNextSkyrim]\nUnknownScale=1\n",
        ExpectedDiagnostic{
            .line = 2,
            .section = "InputNextSkyrim",
            .key = "UnknownScale",
            .message = "Unknown key in [InputNextSkyrim].",
        },
        "unknown root key reports a diagnostic");
    expect_diagnostic("[InputNextSkyrim]\nControllerSlot=0\nControllerSlot=1\n",
        ExpectedDiagnostic{
            .line = 3,
            .section = "InputNextSkyrim",
            .key = "ControllerSlot",
            .message = "Key appears more than once in this section.",
        },
        "duplicate root key reports a diagnostic");
    expect_diagnostic("[Trigger.BowDraw]\nVibrationFrequencyCode=1\n",
        ExpectedDiagnostic{
            .line = 2,
            .section = "Trigger.BowDraw",
            .key = "VibrationFrequencyCode",
            .message =
                "VibrationFrequencyCode is unsupported for this non-vibration trigger profile.",
        },
        "unsupported trigger capability reports a diagnostic");
    expect_diagnostic("[InputNextSkyrim]\nControllerSlot=4\n",
        ExpectedDiagnostic{
            .line = 2,
            .section = "InputNextSkyrim",
            .key = "ControllerSlot",
            .message = "Expected an integer from 0 to 3.",
        },
        "invalid root value reports its expected range");
    expect_diagnostic(
        "\xEF\xBB\xBF; first line\r\n\r\n# third line\r\n[InputNextSkyrim]\r\n\r\nUnknown=1\r\n",
        ExpectedDiagnostic{
            .line = 6,
            .section = "InputNextSkyrim",
            .key = "Unknown",
            .message = "Unknown key in [InputNextSkyrim].",
        },
        "BOM, CRLF, blanks, and comments retain physical diagnostic line numbering");

    const auto config_storage = std::make_unique<NativeConfig>();
    auto& config = *config_storage;
    ConfigError owned_error{};
    std::string short_lived_contents{"[TemporarySection]\n"};
    INFO("unknown section is rejected");
    CHECK(!parse_config(short_lived_contents, config, &owned_error));
    short_lived_contents.assign("contents replaced after parsing");
    INFO("diagnostic section owns its text beyond the parser input lifetime");
    CHECK(owned_error.section == "TemporarySection");
    CHECK(owned_error.line == 1);

    owned_error = ConfigError{
        .line = 99,
        .section = "stale section",
        .key = "stale key",
        .message = "stale message",
    };
    INFO("successful parse clears a preexisting diagnostic");
    CHECK(parse_config("; valid config\n", config, &owned_error));
    CHECK(owned_error.line == 0);
    CHECK(owned_error.section.empty());
    CHECK(owned_error.key.empty());
    CHECK(owned_error.message.empty());
}

namespace {

void check_rejected_transactionally(std::string_view contents,
    const skyrim_adapter::native::NativeConfig& prior,
    std::string_view label) {
    using namespace skyrim_adapter::native;
    const auto candidate_storage = std::make_unique<NativeConfig>(prior);
    auto& candidate = *candidate_storage;
    INFO(label);
    CHECK(!parse_config(contents, candidate));
    INFO("invalid config leaves every native setting unchanged");
    CHECK(same_config(candidate, prior));
}

} // namespace

TEST_CASE("skyrim.config.rejects_invalid_structure_keys_and_capabilities",
    "[unit][skyrim][config]") {
    const auto prior_storage = make_sample_prior_config();
    const auto& prior = *prior_storage;
    check_rejected_transactionally("[InputNextSkyrim]\nControllerSlot=0\nControllerSlot=1\n",
        prior,
        "duplicate root keys fail closed");
    check_rejected_transactionally("[InputNextSkyrim]\n[InputNextSkyrim]\n",
        prior,
        "duplicate sections fail closed");
    check_rejected_transactionally("[Body.Voice]\nHDScale=1\nHDScale=2\n",
        prior,
        "duplicate profile keys fail closed");
    check_rejected_transactionally("[Other]\nControllerSlot=0\n",
        prior,
        "unrecognized sections fail closed");
    check_rejected_transactionally("[InputNextSkyrim]\nUnknownScale=1\n",
        prior,
        "unknown root keys expose config typos");
    check_rejected_transactionally("[InputNextSkyrim]\nSlot=0\n",
        prior,
        "unreleased Slot root alias is rejected");
    check_rejected_transactionally("[InputNextSkyrim]\nCriticalHealth=true\n",
        prior,
        "obsolete global CriticalHealth setting is rejected");
    check_rejected_transactionally("[Body.Voice]\nUnknownScale=1\n",
        prior,
        "unknown body keys expose config typos");
    check_rejected_transactionally("[Trigger.BowDraw]\nVibrationFrequencyCode=1\n",
        prior,
        "unsupported device frequency fields fail closed");
    check_rejected_transactionally("[Trigger.Fire]\nStart=0.5\n",
        prior,
        "unreleased Start key is rejected");
    check_rejected_transactionally("[Trigger.Fire]\nFrequencyCode=1\n",
        prior,
        "unreleased FrequencyCode key is rejected");
    check_rejected_transactionally("[Body.Magic.Fire.Concentration]\nDurationScale=1.5\n",
        prior,
        "duration tuning is rejected for held effects");
    check_rejected_transactionally("[Body.Bow.TensionDrawn]\nClassicScale=1.25\n",
        prior,
        "ClassicScale is rejected transactionally for bow_tension_drawn");
    check_rejected_transactionally("[Body.Magic.Generic.Concentration]\nClassicScale=1.25\n",
        prior,
        "ClassicScale is rejected transactionally for generic_held");
    check_rejected_transactionally("ControllerSlot=0\n",
        prior,
        "keys before a section fail closed");
    check_rejected_transactionally("[InputNextSkyrim\nControllerSlot=0\n",
        prior,
        "malformed sections fail closed");
    check_rejected_transactionally("[InputNextSkyrim]\nControllerSlot\n",
        prior,
        "malformed key/value lines fail closed");
    check_rejected_transactionally("[InputNextSkyrim]\nBodyEnabled=yes\n",
        prior,
        "invalid booleans fail closed");
}

TEST_CASE("skyrim.config.rejects_non_finite_tuning", "[unit][skyrim][config]") {
    const auto prior_storage = make_sample_prior_config();
    const auto& prior = *prior_storage;
    check_rejected_transactionally("[InputNextSkyrim]\nRumbleScale=nan\n",
        prior,
        "nonfinite global scales fail closed");
    check_rejected_transactionally("[Body.Voice]\nHDScale=inf\n",
        prior,
        "nonfinite HD scales fail closed");
    check_rejected_transactionally("[Body.Voice]\nClassicScale=-inf\n",
        prior,
        "nonfinite classic scales fail closed");
    check_rejected_transactionally("[Body.Voice]\nDurationScale=nan\n",
        prior,
        "nonfinite duration scales fail closed");
    check_rejected_transactionally("[Body.Voice]\nFrequencyHz=inf\n",
        prior,
        "nonfinite body frequencies fail closed");
    check_rejected_transactionally("[Trigger.Fire]\nStrengthScale=nan\n",
        prior,
        "nonfinite trigger scales fail closed");
    check_rejected_transactionally("[Trigger.Fire]\nResistanceStart=inf\n",
        prior,
        "nonfinite trigger starts fail closed");
}

TEST_CASE("skyrim.config.rejects_tuning_bounds_and_default_values", "[unit][skyrim][config]") {
    const auto prior_storage = make_sample_prior_config();
    const auto& prior = *prior_storage;
    check_rejected_transactionally("[InputNextSkyrim]\nControllerSlot=4\n",
        prior,
        "out-of-range slot fails config validation");
    check_rejected_transactionally("[InputNextSkyrim]\nTriggerScale=1.1\n",
        prior,
        "global scales stay 0..1");
    check_rejected_transactionally("[InputNextSkyrim]\nTriggerScale=-0.1\n",
        prior,
        "global scale lower bounds fail closed");
    check_rejected_transactionally("[Body.Voice]\nHDScale=2.01\n",
        prior,
        "HD scale upper bound fails closed");
    check_rejected_transactionally("[Body.Voice]\nClassicScale=-0.01\n",
        prior,
        "classic scale lower bound fails closed");
    check_rejected_transactionally("[Body.Voice]\nDurationScale=0.24\n",
        prior,
        "duration scale lower bound fails closed");
    check_rejected_transactionally("[Body.Voice]\nDurationScale=2.01\n",
        prior,
        "duration scale upper bound fails closed");
    check_rejected_transactionally("[Body.Voice]\nFrequencyHz=19.9\n",
        prior,
        "body frequency excludes sub-20 Hz values");
    check_rejected_transactionally("[Body.Voice]\nFrequencyHz=0\n",
        prior,
        "numeric zero is not an HD frequency override");
    check_rejected_transactionally("[Body.Voice]\nFrequencyHz=-0.0\n",
        prior,
        "signed zero is not an HD frequency override");
    check_rejected_transactionally("[Body.Voice]\nFrequencyHz=0e0\n",
        prior,
        "scientific zero is not an HD frequency override");
    check_rejected_transactionally("[Body.Voice]\nFrequencyHz=defaults\n",
        prior,
        "misspelled default fails closed");
    check_rejected_transactionally("[Body.Voice]\nFrequencyHz=1000.1\n",
        prior,
        "body frequency upper bound fails closed");
    check_rejected_transactionally("[Trigger.Fire]\nStrengthScale=2.01\n",
        prior,
        "trigger scale upper bound fails closed");
    check_rejected_transactionally("[Trigger.Fire]\nResistanceStart=1.01\n",
        prior,
        "trigger start upper bound fails closed");
    check_rejected_transactionally("[Trigger.Fire]\nVibrationFrequencyCode=256\n",
        prior,
        "device frequency code upper bound fails closed");
}

TEST_CASE("skyrim.config.rejects_late_multi_profile_transaction", "[unit][skyrim][config]") {
    const auto prior_storage = make_sample_prior_config();
    const auto& prior = *prior_storage;
    check_rejected_transactionally(
        "[InputNextSkyrim]\nControllerSlot=3\nTriggerScale=0.25\nRumbleScale=0\n"
        "BodyEnabled=true\nTriggersEnabled=true\nRespectGameVibration=true\n"
        "[Body.Melee.Sword.Hit]\nEnabled=true\nHDScale=1\nClassicScale=1\nDurationScale="
        "1\nFrequencyHz=800\n"
        "[Trigger.Fire]\nEnabled=true\nStrengthScale=1.5\nResistanceStart=0.8\n"
        "VibrationFrequencyCode=1\nStrengthScale=1.25\n",
        prior,
        "late invalid value rejects the entire multi-profile config transaction");
}

TEST_CASE("skyrim.config.input_size_boundary_and_rejection", "[unit][skyrim][config]") {
    using namespace skyrim_adapter::native;
    constexpr std::size_t limit_bytes = std::size_t{64} * 1024;
    constexpr std::size_t oversized_bytes = limit_bytes + 1;

    INFO("valid configuration at exactly 64 KiB boundary parses successfully");
    {
        std::string valid_content = "[InputNextSkyrim]\nControllerSlot=1\n; ";
        valid_content.append(limit_bytes - valid_content.size() - 1, 'x');
        valid_content.push_back('\n');
        REQUIRE(valid_content.size() == limit_bytes);

        const auto config_storage = std::make_unique<NativeConfig>();
        auto& config = *config_storage;
        ConfigError error{
            .line = 99,
            .section = "stale section",
            .key = "stale key",
            .message = "stale message",
        };
        REQUIRE(parse_config(valid_content, config, &error));
        CHECK(config.slot == 1);
        CHECK(error.line == 0);
        CHECK(error.section.empty());
        CHECK(error.key.empty());
        CHECK(error.message.empty());
    }

    INFO("oversized configuration at 64 KiB + 1 byte is rejected transactionally");
    {
        std::string oversized_content = "[InputNextSkyrim]\nControllerSlot=1\n; ";
        oversized_content.append(oversized_bytes - oversized_content.size() - 1, 'x');
        oversized_content.push_back('\n');
        REQUIRE(oversized_content.size() == oversized_bytes);

        const auto prior_storage = make_sample_prior_config();
        const auto& prior = *prior_storage;
        const auto candidate_storage = std::make_unique<NativeConfig>(prior);
        auto& candidate = *candidate_storage;
        ConfigError error{
            .line = 99,
            .section = "stale section",
            .key = "stale key",
            .message = "stale message",
        };
        CHECK(!parse_config(oversized_content, candidate, &error));
        CHECK(error.line == 0);
        CHECK(error.section.empty());
        CHECK(error.key.empty());
        CHECK(error.message == "configuration exceeds 64 KiB");
        CHECK(same_config(candidate, prior));
    }

    INFO("oversized configuration with UTF-8 BOM is rejected transactionally");
    {
        std::string bom_oversized = "\xEF\xBB\xBF[InputNextSkyrim]\nControllerSlot=1\n; ";
        bom_oversized.append(oversized_bytes - bom_oversized.size() - 1, 'x');
        bom_oversized.push_back('\n');
        REQUIRE(bom_oversized.size() == oversized_bytes);

        const auto prior_storage = make_sample_prior_config();
        const auto& prior = *prior_storage;
        const auto candidate_storage = std::make_unique<NativeConfig>(prior);
        auto& candidate = *candidate_storage;
        ConfigError error{
            .line = 99,
            .section = "stale section",
            .key = "stale key",
            .message = "stale message",
        };
        CHECK(!parse_config(bom_oversized, candidate, &error));
        CHECK(error.line == 0);
        CHECK(error.section.empty());
        CHECK(error.key.empty());
        CHECK(error.message == "configuration exceeds 64 KiB");
        CHECK(same_config(candidate, prior));
    }

    INFO("large oversized input rejection leaves prior configuration untouched");
    {
        constexpr std::size_t large_bytes = std::size_t{128} * 1024;
        std::string large_oversized(large_bytes, ';');
        large_oversized.back() = '\n';

        const auto prior_storage = make_sample_prior_config();
        const auto& prior = *prior_storage;
        const auto candidate_storage = std::make_unique<NativeConfig>(prior);
        auto& candidate = *candidate_storage;
        ConfigError error{
            .line = 99,
            .section = "stale section",
            .key = "stale key",
            .message = "stale message",
        };
        CHECK(!parse_config(large_oversized, candidate, &error));
        CHECK(error.line == 0);
        CHECK(error.section.empty());
        CHECK(error.key.empty());
        CHECK(error.message == "configuration exceeds 64 KiB");
        CHECK(same_config(candidate, prior));
    }
}
