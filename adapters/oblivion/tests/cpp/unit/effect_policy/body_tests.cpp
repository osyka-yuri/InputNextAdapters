#include "effect_model.hpp"
#include "effect_policy.hpp"
#include "effect_settings.hpp"
#include "fixtures.hpp"
#include "inputnext_adapters/audio_haptic_profile.hpp"
#include "inputnext_adapters/body_program.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace oblivion_adapter::test::effect_policy {
namespace {

constexpr float interior_knot_gain = 0.4F;

inputnext::adapters::AudioHapticProfile audio_profile(float frequency) {
    inputnext::adapters::AudioHapticEnvelope envelope{};
    envelope.enabled = true;
    for (std::size_t index = 1; index + 1 < envelope.primary.size(); ++index) {
        envelope.primary[index] = interior_knot_gain;
    }
    return {.frequency = frequency, .second_frequency = 0.0F, .envelope = envelope};
}

} // namespace

TEST_CASE("oblivion.effect_policy.body.audio_override_and_synthesized_fallback",
    "[unit][oblivion][effects][body]") {
    EffectSettings settings{};
    settings.profiles[profile_index(EffectId::generic_hit)].audio =
        std::make_shared<const inputnext::adapters::AudioHapticProfile>(audio_profile(325.0F));
    const auto sample = observation(100, 5);
    auto hit = event(EffectEventKind::hit, 100, 5);
    hit.actor_id = sample.player_id;
    hit.target_id = 0x777;
    const std::array events{hit};

    const auto audio = make_policy(settings);
    const auto overridden = audio->update(sample, events, at(100));
    const auto& wave = first_voice(overridden).wave;
    CHECK(std::abs(wave.frequency - 325.0F) < 0.001F);
    CHECK(wave.audio_envelope.enabled);

    settings.audio_haptics_enabled = false;
    const auto synthesized = make_policy(settings);
    const auto fallback = synthesized->update(sample, events, at(100));
    const auto& recipe = first_voice(fallback).wave;
    CHECK(std::abs(recipe.frequency - 58.0F) < 0.001F);
    CHECK_FALSE(recipe.audio_envelope.enabled);
}

TEST_CASE("oblivion.effect_policy.body.held_audio_uses_mean_gain",
    "[unit][oblivion][effects][body]") {
    EffectSettings settings{};
    const auto profile = audio_profile(245.0F);
    settings.profiles[profile_index(EffectId::bow_draw)].audio =
        std::make_shared<const inputnext::adapters::AudioHapticProfile>(profile);
    auto sample = observation(120, 6);
    sample.weapon = Weapon::bow;
    sample.bow = BowPhase::drawing;

    const auto audio = make_policy(settings);
    const auto held = audio->update(sample, {}, at(120));
    REQUIRE(voice_count(held) == 1);
    const auto& voice = first_voice(held);
    CHECK(voice.held);
    CHECK(std::abs(voice.wave.frequency - 245.0F) < 0.001F);
    CHECK_FALSE(voice.wave.audio_envelope.enabled);

    settings.audio_haptics_enabled = false;
    const auto synthesized = make_policy(settings);
    const auto fallback = synthesized->update(sample, {}, at(120));
    const auto& recipe = first_voice(fallback).wave;
    const auto knots = profile.envelope.primary.size();
    const auto mean_gain =
        interior_knot_gain * static_cast<float>(knots - 2) / static_cast<float>(knots);
    CHECK(std::abs(voice.wave.level - (recipe.level * mean_gain)) < 0.001F);
}

TEST_CASE("oblivion.effect_policy.body.profile_scales_and_pcm_peaks",
    "[unit][oblivion][effects][body]") {
    EffectSettings settings{};
    settings.rumble_scale = 0.8F;
    auto& profile = settings.profiles[profile_index(EffectId::generic_hit)];
    profile.hd_scale = 2.0F;
    profile.classic_scale = 2.0F;
    const auto policy = make_policy(settings);
    const auto sample = observation(100, 5);
    auto hit = event(EffectEventKind::hit, 100, 5);
    hit.actor_id = sample.player_id;
    hit.target_id = 0x555;

    const auto frame = policy->update(sample, std::array{hit}, at(100));
    const auto& voice = first_voice(frame);
    CHECK(std::abs(voice.hd_scale - 2.0F) < 0.001F);
    CHECK(voice.wave.level <= 0.75F);
    REQUIRE(frame.rumble);
    const auto& rumble = require_value(frame.rumble, "scaled frame contains classic rumble");
    CHECK(rumble.payload.low_frequency <= 0.6001F);
    CHECK(rumble.payload.high_frequency <= 0.6001F);

    std::array<float, 960> samples{};
    static_cast<void>(inputnext::adapters::build_body_pcm(frame.body, at(110), samples));
    const auto peak =
        std::ranges::max_element(samples, {}, [](float value) { return std::abs(value); });
    REQUIRE(peak != samples.end());
    CHECK(std::abs(*peak) > 0.0F);
    CHECK(std::abs(*peak) <= 0.6001F);
}

TEST_CASE("oblivion.effect_policy.body.nonfinite_settings_produce_finite_waveforms",
    "[unit][oblivion][effects][body]") {
    EffectSettings settings{};
    settings.rumble_scale = std::numeric_limits<float>::quiet_NaN();
    auto& profile = settings.profiles[profile_index(EffectId::generic_hit)];
    profile.hd_scale = std::numeric_limits<float>::infinity();
    profile.classic_scale = std::numeric_limits<float>::quiet_NaN();
    profile.frequency_hz = std::numeric_limits<float>::quiet_NaN();
    const auto policy = make_policy(settings);
    const auto sample = observation(100, 5);
    auto hit = event(EffectEventKind::hit, 100, 5);
    hit.actor_id = sample.player_id;
    hit.target_id = 0x555;

    const auto frame = policy->update(sample, std::array{hit}, at(100));
    const auto& voice = first_voice(frame);
    CHECK(std::isfinite(voice.scale));
    CHECK(std::isfinite(voice.wave.level));
    CHECK(std::isfinite(voice.wave.frequency));
    CHECK(voice.wave.level <= 0.75F);
}

TEST_CASE("oblivion.effect_policy.body.hd_and_classic_mix_independently",
    "[unit][oblivion][effects][body]") {
    EffectSettings settings{};
    settings.profiles[profile_index(EffectId::incoming_damage)].hd_scale = 0.0F;
    settings.profiles[profile_index(EffectId::generic_hit)].classic_scale = 0.0F;
    const auto sample = observation(100, 5);
    std::array<EffectEvent, 6> events{};
    for (std::uint32_t index = 0; index < 5; ++index) {
        auto& damage = events[index];
        damage = event(EffectEventKind::damage, 100, 5, index + 1U);
        damage.actor_id = 0x200U + index;
        damage.target_id = sample.player_id;
        damage.amount = 10.0F;
    }
    auto& hit = events.back();
    hit = event(EffectEventKind::hit, 100, 5, 6);
    hit.actor_id = sample.player_id;
    hit.target_id = 0x444;

    SECTION("The classic-only impacts leave space for an HD hit") {
        const auto policy = make_policy(settings);
        const auto frame = policy->update(sample, events, at(100));
        REQUIRE(voice_count(frame) == 1);
        CHECK(first_voice(frame).wave.frequency == 58.0F);
        REQUIRE(frame.rumble);
        CHECK(require_value(frame.rumble, "classic hit produces rumble").payload.low_frequency >
              0.0F);
    }
    SECTION("The zero master setting mutes both routes") {
        settings.rumble_scale = 0.0F;
        const auto policy = make_policy(settings);
        const auto frame = policy->update(sample, events, at(100));
        CHECK_FALSE(frame.rumble);
        CHECK(voice_count(frame) == 0);
    }
}

} // namespace oblivion_adapter::test::effect_policy
