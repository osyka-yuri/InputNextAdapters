#include "config.hpp"
#include "effect_settings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using namespace oblivion_adapter;

TEST_CASE("oblivion.config.defaults_and_global_controls", "[unit][oblivion][config]") {
    EffectSettings settings{};
    ConfigError error{};
    REQUIRE(parse_config("; defaults\r\n", settings, &error));
    CHECK(settings.controller_slot == 0);
    CHECK(settings.rumble_scale == 1.0F);
    CHECK(settings.trigger_scale == 1.0F);
    CHECK(settings.body_enabled);
    CHECK(settings.triggers_enabled);
    CHECK(settings.menu_effects_enabled);
    CHECK(settings.audio_haptics_enabled);
    CHECK_FALSE(settings.pause_on_options);
    CHECK(settings.led.health_enabled);
    CHECK(settings.led.stealth_enabled);
    CHECK(!settings.profiles[profile_index(EffectId::walk)].enabled);
    CHECK(!settings.profiles[profile_index(EffectId::skill_up)].enabled);
    CHECK(settings.profiles[profile_index(EffectId::lock_opened)].enabled);
    CHECK(settings.profiles[profile_index(EffectId::pickpocketed)].enabled);
    CHECK_FALSE(settings.profiles[profile_index(EffectId::trained)].enabled);
    CHECK_FALSE(settings.profiles[profile_index(EffectId::gate_closed)].enabled);

    REQUIRE(parse_config("\xEF\xBB\xBF[InputNextOblivion]\r\nControllerSlot=3\r\n"
                         "RumbleScale=0.25 ; comment\r\nTriggerScale=0\r\n"
                         "BodyEnabled=fAlSe\r\nMenuEffectsEnabled=TRUE\r\n"
                         "AudioHapticsEnabled=false\r\nPauseOnOptions=true\r\n",
        settings,
        &error));
    CHECK(settings.controller_slot == 3);
    CHECK(settings.rumble_scale == 0.25F);
    CHECK(settings.trigger_scale == 0.0F);
    CHECK_FALSE(settings.body_enabled);
    CHECK(settings.menu_effects_enabled);
    CHECK_FALSE(settings.audio_haptics_enabled);
    CHECK(settings.pause_on_options);
}

TEST_CASE("oblivion.config.every_profile_and_audio_frequency_override",
    "[unit][oblivion][config]") {
    std::string contents;
    for (const auto section : effect_sections) {
        contents += '[';
        contents += section;
        contents += "]\nEnabled=false\nHDScale=2\nClassicScale=0.5\n"
                    "DurationScale=0.25\nFrequencyHz=20\n";
    }

    EffectSettings settings{};
    ConfigError error{};
    REQUIRE(parse_config(contents, settings, &error));
    REQUIRE(settings.profiles.size() == effect_sections.size());
    for (const auto& profile : settings.profiles) {
        CHECK_FALSE(profile.enabled);
        CHECK(profile.hd_scale == 2.0F);
        CHECK(profile.classic_scale == 0.5F);
        CHECK(profile.duration_scale == 0.25F);
        if (profile.frequency_hz.has_value()) {
            CHECK(*profile.frequency_hz == 20.0F);
        } else {
            FAIL("config sets a frequency override for every profile");
        }
    }

    REQUIRE(
        parse_config("[Body.Bow.Draw]\nFrequencyHz=DeFaUlT\nDurationScale=2\n", settings, &error));
    CHECK_FALSE(settings.profiles[profile_index(EffectId::bow_draw)].frequency_hz.has_value());
    CHECK(settings.profiles[profile_index(EffectId::bow_draw)].duration_scale == 2.0F);
}

TEST_CASE("oblivion.config.led_validation_and_atomic_failure", "[unit][oblivion][config]") {
    EffectSettings settings{};
    ConfigError error{};
    REQUIRE(parse_config("[LED]\nEnabled=false\nStealthEnabled=false\nHealthHoldMs=60000\n"
                         "HealingTransitionMs=0\nCriticalEnter=0\nCriticalExit=1\n",
        settings,
        &error));
    CHECK_FALSE(settings.led.enabled);
    CHECK_FALSE(settings.led.stealth_enabled);
    CHECK(settings.led.health_hold_ms == 60000U);
    CHECK(settings.led.healing_transition_ms == 0U);
    CHECK(settings.led.critical_enter == 0.0F);
    CHECK(settings.led.critical_exit == 1.0F);

    settings.controller_slot = 2;
    settings.rumble_scale = 0.4F;
    CHECK_FALSE(
        parse_config("[InputNextOblivion]\nControllerSlot=1\nRumbleScale=nan\n", settings, &error));
    CHECK(error.line == 3);
    CHECK(!error.message.empty());
    CHECK(settings.controller_slot == 2);
    CHECK(settings.rumble_scale == 0.4F);

    CHECK_FALSE(parse_config("[LED]\nCriticalEnter=0.8\nCriticalExit=0.4\n", settings, &error));
    CHECK(error.line == 3);
    CHECK(settings.controller_slot == 2);
}

TEST_CASE("oblivion.config.duplicates_unknowns_and_bounds_are_rejected",
    "[unit][oblivion][config]") {
    EffectSettings settings{};
    ConfigError error{};
    CHECK_FALSE(parse_config("[Body.Bow.Draw]\nEnabled=true\n[body.bow.draw]\nEnabled=false\n",
        settings,
        &error));
    CHECK(error.message == "Section appears more than once.");
    CHECK_FALSE(
        parse_config("[InputNextOblivion]\nRumbleScale=1\nrumbleScale=0\n", settings, &error));
    CHECK(error.message == "Key appears more than once in this section.");
    CHECK_FALSE(parse_config("[Body.Bow.Draw]\nFrequencyHz=19.99\n", settings, &error));
    CHECK_FALSE(parse_config("[Body.Bow.Draw]\nDurationScale=2.01\n", settings, &error));
    CHECK_FALSE(parse_config("[Unknown]\nEnabled=true\n", settings, &error));
    CHECK_FALSE(parse_config("[InputNextOblivion]\nPauseOnOptions=yes\n", settings, &error));
    CHECK_FALSE(parse_config("[LED]\nStealthEnabled=yes\n", settings, &error));

    const std::string too_large((64U * 1024U) + 1U, ' ');
    CHECK_FALSE(parse_config(too_large, settings, &error));
    CHECK(error.line == 0);
    CHECK(error.message == "configuration exceeds 64 KiB");
}

TEST_CASE("oblivion.config.file_status_and_failed_reads_are_atomic", "[unit][oblivion][config]") {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("inputnext-oblivion-config-" + std::to_string(stamp));
    REQUIRE(std::filesystem::create_directory(directory));
    const auto file = directory / "InputNextOblivion.ini";
    std::error_code ignored;

    EffectSettings settings{};
    settings.controller_slot = 2;
    ConfigError error{};
    CHECK(read_config_file(file, settings, &error) == ConfigStatus::missing);
    CHECK(settings.controller_slot == 0);

    {
        std::ofstream output(file);
        REQUIRE(output.good());
        output << "[InputNextOblivion]\nControllerSlot=1\n";
    }
    CHECK(read_config_file(file, settings, &error) == ConfigStatus::loaded);
    CHECK(settings.controller_slot == 1);

    {
        std::ofstream output(file);
        REQUIRE(output.good());
        output << "[InputNextOblivion]\nControllerSlot=4\n";
    }
    settings.controller_slot = 3;
    CHECK(read_config_file(file, settings, &error) == ConfigStatus::invalid);
    CHECK(settings.controller_slot == 3);
    CHECK(error.line == 2);

    std::filesystem::remove(file, ignored);
    std::filesystem::remove(directory, ignored);
}
