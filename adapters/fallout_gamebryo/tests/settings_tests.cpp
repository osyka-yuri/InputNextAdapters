#include <catch2/catch_test_macros.hpp>

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/audio_haptics.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>

namespace fallout_gamebryo::tests {
namespace {
using inputnext::adapters::AudioHapticEnvelope;
using inputnext::adapters::AudioHapticGame;
using inputnext::adapters::AudioHapticProfile;

AudioHapticProfile audio_profile() {
    AudioHapticProfile profile{};
    profile.frequency = 95.0F;
    profile.second_frequency = 130.0F;
    profile.envelope.enabled = true;
    profile.envelope.primary[1] = 0.2F;
    profile.envelope.primary[16] = 0.3F;
    profile.envelope.secondary[2] = 0.1F;
    return profile;
}
} // namespace

TEST_CASE("fallout_gamebryo.policy.config_is_bounded_atomic_and_maps_trigger_sides",
    "[fallout-gamebryo][config]") {
    Settings settings{};
    constexpr std::string_view config =
        "[General]\nEnabled=true\nControllerSlot=2\n"
        "[Controls]\nAttackTrigger=left\nAimTrigger=right\n"
        "[Effects]\nBodyScale=0.8\nTriggerScale=0.5\nGaitCues=true\n"
        "[Body.ShotPistol]\nStrengthScale=1.2\nDurationScale=0.8\n"
        "[Trigger.Shotgun]\nResistanceStart=0.42\n"
        "[LED]\nCriticalEnter=0.18\nCriticalExit=0.26\n";
    ConfigError error{};
    CHECK(parse_config(config, settings, &error) == ConfigStatus::loaded);
    CHECK(settings.controller_slot == 2U);
    CHECK(settings.attack_trigger == TriggerSide::left);
    CHECK(settings.aim_trigger == TriggerSide::right);
    CHECK(settings.gait_cues);
    CHECK(settings.body_scale == 0.8F);
    CHECK(settings.body[static_cast<std::size_t>(BodyEffect::shot_pistol)].strength_scale == 1.2F);
    CHECK(settings.trigger[static_cast<std::size_t>(TriggerProfile::shotgun)].resistance_start ==
          0.42F);

    const auto original = std::make_unique<Settings>(settings);
    CHECK(parse_config("[Effects]\nBodyScale=nan\n", settings, &error) == ConfigStatus::invalid);
    CHECK(settings.body_scale == original->body_scale);
    CHECK(parse_config("[Body.ShotPistol]\nEnabled=true\nEnabled=false\n", settings, &error) ==
          ConfigStatus::invalid);
    CHECK(settings.body[static_cast<std::size_t>(BodyEffect::shot_pistol)].enabled ==
          original->body[static_cast<std::size_t>(BodyEffect::shot_pistol)].enabled);
}

TEST_CASE("fallout_gamebryo.policy.audio_overlay_ignores_zero_and_other_game_profiles",
    "[fallout-gamebryo][audio]") {
    Settings settings{};
    inputnext::adapters::AudioHapticBank bank{.game = AudioHapticGame::fallout_new_vegas,
        .entries = {{.section = "Body.ShotPistol", .profile = audio_profile()}}};
    CHECK(bind_audio_haptics(bank, settings, AudioHapticGame::fallout_new_vegas) == 1U);
    const auto& audio_haptic =
        settings.body[static_cast<std::size_t>(BodyEffect::shot_pistol)].audio_haptic;
    REQUIRE(audio_haptic.has_value());
    CHECK(audio_haptic.value_or(AudioHapticProfile{}).frequency == 95.0F);

    auto other_game = bank;
    other_game.game = AudioHapticGame::fallout4;
    CHECK(bind_audio_haptics(other_game, settings, AudioHapticGame::fallout_new_vegas) == 0U);
    CHECK_FALSE(settings.body[static_cast<std::size_t>(BodyEffect::shot_pistol)].audio_haptic);

    bank.entries[0].profile.envelope = AudioHapticEnvelope{.enabled = true};
    CHECK(bind_audio_haptics(bank, settings, AudioHapticGame::fallout_new_vegas) == 0U);
    CHECK_FALSE(settings.body[static_cast<std::size_t>(BodyEffect::shot_pistol)].audio_haptic);
}

TEST_CASE("fallout_gamebryo.policy.loads_only_the_requested_games_builtin_audio_bank",
    "[fallout-gamebryo][audio]") {
    Settings settings{};
    const std::filesystem::path missing_config{"missing-inputnext-fallout-gamebryo/Input.ini"};
    const auto loaded =
        load_audio_haptics(missing_config, settings, AudioHapticGame::fallout_new_vegas);
    CHECK(loaded.override_status == AudioHapticOverrideStatus::not_found);
    CHECK(loaded.loaded_profiles == 25U);
    CHECK(
        settings.body[static_cast<std::size_t>(BodyEffect::shot_pistol)].audio_haptic.has_value());
    CHECK_FALSE(
        settings.body[static_cast<std::size_t>(BodyEffect::shot_generic)].audio_haptic.has_value());
}
} // namespace fallout_gamebryo::tests
