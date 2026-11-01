#include "effect_recipes.hpp"

#include "fallout4_adapter/model.hpp"

#include <inputnext_adapters/body_program.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace fallout4_adapter::detail {
namespace {
using inputnext::adapters::WaveRecipe;

[[nodiscard]] constexpr WaveRecipe wave(float frequency,
    float second_frequency,
    float second_mix,
    float level,
    float noise,
    float attack_ms,
    float release_ms,
    float decay_per_second,
    float modulation_hz = 0.0F,
    std::uint16_t first_pulse_ms = 0,
    std::uint16_t pulse_gap_ms = 0,
    std::uint16_t second_pulse_ms = 0) noexcept {
    return {
        .frequency = frequency,
        .second_frequency = second_frequency,
        .second_mix = second_mix,
        .level = level,
        .noise = noise,
        .attack_ms = attack_ms,
        .release_ms = release_ms,
        .decay_per_second = decay_per_second,
        .modulation_hz = modulation_hz,
        .first_pulse_ms = first_pulse_ms,
        .pulse_gap_ms = pulse_gap_ms,
        .second_pulse_ms = second_pulse_ms,
    };
}

[[nodiscard]] constexpr HapticRecipe make_recipe(WaveRecipe waveform,
    std::uint16_t duration_ms,
    int priority,
    float low,
    float high) noexcept {
    return {
        .wave = waveform,
        .duration_ms = duration_ms,
        .priority = priority,
        .fallback_low = low,
        .fallback_high = high,
    };
}

[[nodiscard]] double smooth_gate(double position, double width) noexcept {
    if (position <= 0.0) {
        return 0.0;
    }
    if (position >= width || width <= 0.0) {
        return 1.0;
    }
    const double t = position / width;
    return t * t * (3.0 - (2.0 * t));
}
} // namespace

HapticRecipe recipe_for(Cue cue) noexcept {
    switch (cue) {
    case Cue::pistol_shot:
        return make_recipe(
            wave(112.0F, 76.0F, 0.23F, 0.68F, 0.16F, 1.2F, 30.0F, 5.0F, 0.0F, 24, 8, 22),
            62,
            50,
            0.28F,
            0.34F);
    case Cue::rifle_shot:
        return make_recipe(
            wave(86.0F, 132.0F, 0.32F, 0.66F, 0.23F, 1.0F, 32.0F, 5.2F, 0.0F, 28, 7, 24),
            64,
            50,
            0.32F,
            0.30F);
    case Cue::shotgun_shot:
        return make_recipe(
            wave(52.0F, 98.0F, 0.45F, 0.72F, 0.26F, 0.8F, 42.0F, 5.0F, 0.0F, 32, 8, 27),
            74,
            52,
            0.40F,
            0.34F);
    case Cue::heavy_shot:
        return make_recipe(
            wave(46.0F, 72.0F, 0.38F, 0.70F, 0.20F, 1.2F, 48.0F, 4.6F, 0.0F, 35, 10, 30),
            86,
            52,
            0.42F,
            0.30F);
    case Cue::energy_shot:
        return make_recipe(
            wave(128.0F, 184.0F, 0.34F, 0.58F, 0.12F, 1.0F, 33.0F, 4.2F, 0.0F, 22, 7, 24),
            62,
            50,
            0.23F,
            0.35F);
    case Cue::generic_shot:
        return make_recipe(
            wave(94.0F, 128.0F, 0.28F, 0.54F, 0.18F, 1.2F, 30.0F, 5.0F, 0.0F, 24, 8, 22),
            62,
            48,
            0.26F,
            0.28F);
    case Cue::incoming_hit:
        return make_recipe(
            wave(48.0F, 78.0F, 0.40F, 0.76F, 0.22F, 1.0F, 50.0F, 4.0F, 0.0F, 38, 11, 32),
            90,
            100,
            0.64F,
            0.48F);
    case Cue::blocked_hit:
        return make_recipe(
            wave(142.0F, 205.0F, 0.34F, 0.52F, 0.12F, 0.8F, 10.0F, 5.5F, 0.0F, 26, 8, 26),
            70,
            68,
            0.27F,
            0.32F);
    case Cue::explosive_hit:
        return make_recipe(
            wave(38.0F, 62.0F, 0.45F, 0.78F, 0.24F, 2.0F, 58.0F, 3.6F, 0.0F, 45, 15, 42),
            118,
            100,
            0.70F,
            0.52F);
    case Cue::crippled_hit:
        return make_recipe(
            wave(62.0F, 138.0F, 0.42F, 0.62F, 0.18F, 1.0F, 40.0F, 4.6F, 0.0F, 24, 9, 22),
            64,
            86,
            0.40F,
            0.38F);
    case Cue::outgoing_hit:
        return make_recipe(
            wave(104.0F, 158.0F, 0.36F, 0.55F, 0.12F, 0.8F, 10.0F, 5.0F, 0.0F, 26, 8, 30),
            74,
            70,
            0.32F,
            0.32F);
    case Cue::critical_hit:
        return make_recipe(
            wave(66.0F, 188.0F, 0.48F, 0.68F, 0.16F, 1.0F, 38.0F, 4.4F, 0.0F, 23, 10, 28),
            68,
            90,
            0.46F,
            0.50F);
    case Cue::power_attack:
        return make_recipe(
            wave(50.0F, 92.0F, 0.40F, 0.66F, 0.20F, 1.0F, 44.0F, 4.4F, 0.0F, 30, 9, 25),
            72,
            68,
            0.38F,
            0.32F);
    case Cue::bash:
        return make_recipe(
            wave(74.0F, 142.0F, 0.40F, 0.60F, 0.19F, 0.8F, 38.0F, 4.8F, 0.0F, 22, 8, 20),
            58,
            66,
            0.35F,
            0.34F);
    case Cue::footstep:
        return make_recipe(wave(72.0F, 104.0F, 0.30F, 0.20F, 0.10F, 2.0F, 16.0F, 6.0F),
            56,
            10,
            0.10F,
            0.12F);
    case Cue::landing:
        return make_recipe(wave(54.0F, 82.0F, 0.36F, 0.54F, 0.16F, 2.0F, 36.0F, 5.0F),
            80,
            40,
            0.30F,
            0.23F);
    case Cue::vats_enter:
        return make_recipe(wave(176.0F, 226.0F, 0.28F, 0.28F, 0.05F, 2.0F, 25.0F, 8.0F),
            36,
            18,
            0.09F,
            0.12F);
    case Cue::vats_exit:
        return make_recipe(wave(152.0F, 112.0F, 0.24F, 0.26F, 0.07F, 2.0F, 26.0F, 8.0F),
            36,
            18,
            0.09F,
            0.10F);
    case Cue::health_warning:
        return make_recipe(
            wave(58.0F, 92.0F, 0.42F, 0.48F, 0.14F, 1.0F, 34.0F, 5.0F, 0.0F, 21, 10, 23),
            60,
            84,
            0.30F,
            0.26F);
    case Cue::ap_warning:
        return make_recipe(wave(124.0F, 172.0F, 0.34F, 0.34F, 0.08F, 1.0F, 24.0F, 6.5F),
            42,
            60,
            0.15F,
            0.22F);
    case Cue::critical_ready:
        return make_recipe(wave(160.0F, 224.0F, 0.36F, 0.34F, 0.06F, 1.0F, 24.0F, 6.0F),
            42,
            62,
            0.15F,
            0.24F);
    case Cue::reload_start:
        return make_recipe(wave(102.0F, 156.0F, 0.22F, 0.38F, 0.08F, 2.0F, 28.0F, 5.2F),
            58,
            28,
            0.16F,
            0.20F);
    case Cue::power_armor_enter:
        return make_recipe(wave(54.0F, 88.0F, 0.38F, 0.42F, 0.10F, 2.0F, 32.0F, 4.8F),
            72,
            30,
            0.24F,
            0.16F);
    case Cue::power_armor_exit:
        return make_recipe(wave(118.0F, 166.0F, 0.28F, 0.36F, 0.08F, 2.0F, 28.0F, 5.4F),
            64,
            30,
            0.12F,
            0.20F);
    case Cue::melee_hit:
        return make_recipe(wave(72.0F, 112.0F, 0.30F, 0.48F, 0.14F, 1.2F, 30.0F, 4.8F),
            68,
            70,
            0.28F,
            0.30F);
    case Cue::rotary_shot:
        return make_recipe(wave(70.0F, 112.0F, 0.22F, 0.28F, 0.08F, 0.7F, 16.0F, 6.0F),
            32,
            50,
            0.16F,
            0.12F);
    case Cue::flame_shot:
        return make_recipe(wave(42.0F, 76.0F, 0.24F, 0.22F, 0.17F, 2.0F, 24.0F, 3.8F),
            40,
            48,
            0.12F,
            0.10F);
    case Cue::cryo_shot:
        return make_recipe(wave(150.0F, 210.0F, 0.24F, 0.18F, 0.08F, 1.0F, 24.0F, 4.8F),
            36,
            48,
            0.08F,
            0.16F);
    case Cue::launcher_shot:
        return make_recipe(wave(44.0F, 72.0F, 0.30F, 0.72F, 0.18F, 2.0F, 82.0F, 3.1F),
            140,
            54,
            0.58F,
            0.34F);
    case Cue::ordinary_step:
        return make_recipe(wave(92.0F, 128.0F, 0.22F, 0.10F, 0.06F, 2.0F, 16.0F, 6.0F),
            64,
            8,
            0.05F,
            0.06F);
    case Cue::sprint_step:
        return make_recipe(wave(80.0F, 120.0F, 0.24F, 0.16F, 0.09F, 2.0F, 18.0F, 5.6F),
            56,
            9,
            0.08F,
            0.10F);
    case Cue::count:
        break;
    }
    return make_recipe(wave(94.0F, 128.0F, 0.28F, 0.54F, 0.18F, 1.2F, 30.0F, 5.0F, 0.0F, 24, 8, 22),
        62,
        48,
        0.26F,
        0.28F);
}

TriggerRecipe trigger_recipe_for(WeaponFamily weapon_family) noexcept {
    switch (weapon_family) {
    case WeaponFamily::pistol:
        return {.resistance_start = 0.67F,
            .strength = 0.24F,
            .secondary_start = 0.84F,
            .secondary_strength = 0.14F};
    case WeaponFamily::rifle:
        return {.resistance_start = 0.56F,
            .strength = 0.34F,
            .secondary_start = 0.84F,
            .secondary_strength = 0.14F};
    case WeaponFamily::shotgun:
        return {.resistance_start = 0.76F,
            .strength = 0.44F,
            .secondary_start = 0.84F,
            .secondary_strength = 0.14F};
    case WeaponFamily::heavy:
        return {.resistance_start = 0.44F,
            .strength = 0.46F,
            .secondary_start = 0.84F,
            .secondary_strength = 0.14F};
    case WeaponFamily::energy:
        return {.resistance_start = 0.33F,
            .strength = 0.24F,
            .secondary_start = 0.84F,
            .secondary_strength = 0.14F};
    case WeaponFamily::generic_firearm:
        return {.resistance_start = 0.62F,
            .strength = 0.24F,
            .secondary_start = 0.84F,
            .secondary_strength = 0.14F};
    case WeaponFamily::rotary:
        return {.resistance_start = 0.33F,
            .strength = 0.28F,
            .secondary_start = 0.84F,
            .secondary_strength = 0.14F};
    case WeaponFamily::flame:
        return {.resistance_start = 0.22F,
            .strength = 0.20F,
            .secondary_start = 0.84F,
            .secondary_strength = 0.14F};
    case WeaponFamily::cryo:
        return {.resistance_start = 0.33F,
            .strength = 0.22F,
            .secondary_start = 0.84F,
            .secondary_strength = 0.14F};
    case WeaponFamily::launcher:
        return {.resistance_start = 0.55F,
            .strength = 0.44F,
            .secondary_start = 0.84F,
            .secondary_strength = 0.14F};
    case WeaponFamily::unarmed:
        return {.resistance_start = 0.67F,
            .strength = 0.18F,
            .secondary_start = 0.33F,
            .secondary_strength = 0.36F};
    case WeaponFamily::melee:
        return {.resistance_start = 0.44F,
            .strength = 0.24F,
            .secondary_start = 0.33F,
            .secondary_strength = 0.36F};
    case WeaponFamily::two_handed_melee:
        return {.resistance_start = 0.33F,
            .strength = 0.38F,
            .secondary_start = 0.33F,
            .secondary_strength = 0.36F};
    case WeaponFamily::none:
    default:
        return {};
    }
}

ShotTriggerRecipe shot_trigger_recipe_for(WeaponFamily weapon_family, bool automatic) noexcept {
    switch (weapon_family) {
    case WeaponFamily::rotary:
        return {.start = 0.22F, .strength = 0.32F, .device_frequency = 68.0F, .duration_ms = 24};
    case WeaponFamily::flame:
        return {.start = 0.11F, .strength = 0.24F, .device_frequency = 32.0F, .duration_ms = 36};
    case WeaponFamily::cryo:
        return {.start = 0.22F, .strength = 0.22F, .device_frequency = 110.0F, .duration_ms = 32};
    case WeaponFamily::launcher:
        return {.start = 0.44F, .strength = 0.44F, .device_frequency = 30.0F, .duration_ms = 48};
    default:
        break;
    }
    if (!automatic) {
        return {.start = 0.0F, .strength = 0.20F, .device_frequency = 76.0F, .duration_ms = 48};
    }

    switch (weapon_family) {
    case WeaponFamily::pistol:
        return {.start = 0.33F, .strength = 0.36F, .device_frequency = 86.0F, .duration_ms = 36};
    case WeaponFamily::rifle:
        return {.start = 0.22F, .strength = 0.38F, .device_frequency = 62.0F, .duration_ms = 40};
    case WeaponFamily::shotgun:
        return {.start = 0.33F, .strength = 0.42F, .device_frequency = 42.0F, .duration_ms = 48};
    case WeaponFamily::heavy:
        return {.start = 0.11F, .strength = 0.44F, .device_frequency = 38.0F, .duration_ms = 48};
    case WeaponFamily::energy:
        return {.start = 0.22F, .strength = 0.34F, .device_frequency = 104.0F, .duration_ms = 32};
    case WeaponFamily::generic_firearm:
        return {.start = 0.33F, .strength = 0.36F, .device_frequency = 70.0F, .duration_ms = 40};
    case WeaponFamily::rotary:
    case WeaponFamily::flame:
    case WeaponFamily::cryo:
    case WeaponFamily::launcher:
        break;
    case WeaponFamily::unarmed:
    case WeaponFamily::melee:
    case WeaponFamily::two_handed_melee:
    case WeaponFamily::none:
    default:
        return {.start = 0.0F, .strength = 0.20F, .device_frequency = 76.0F, .duration_ms = 48};
    }
    return {.start = 0.0F, .strength = 0.20F, .device_frequency = 76.0F, .duration_ms = 48};
}

float envelope_gain(const WaveRecipe& wave,
    std::uint32_t duration_ms,
    float elapsed_ms,
    float duration_scale) noexcept {
    if (duration_ms == 0 || !std::isfinite(elapsed_ms) || elapsed_ms < 0.0F ||
        !std::isfinite(duration_scale) || duration_scale < 0.25F || duration_scale > 2.0F ||
        elapsed_ms >= static_cast<float>(duration_ms)) {
        return 0.0F;
    }

    const auto age = static_cast<double>(elapsed_ms) / static_cast<double>(duration_scale);
    const auto duration = static_cast<double>(duration_ms) / static_cast<double>(duration_scale);
    const double attack = std::max(0.0, static_cast<double>(wave.attack_ms));
    const double release = std::max(0.0, static_cast<double>(wave.release_ms));
    const double edge = std::min(3.0, std::max(0.5, duration * 0.20));
    double gain = smooth_gate(age, attack > 0.0 ? attack : edge) *
                  smooth_gate(duration - age, release > 0.0 ? release : edge);

    if (wave.first_pulse_ms != 0) {
        const auto first_end = static_cast<double>(wave.first_pulse_ms);
        const double second_start = first_end + static_cast<double>(wave.pulse_gap_ms);
        const double second_end = second_start + static_cast<double>(wave.second_pulse_ms);
        if (age < first_end) {
            gain *= smooth_gate(age, 1.5) * smooth_gate(first_end - age, 1.5);
        } else if (age >= second_start && age < second_end) {
            gain *= smooth_gate(age - second_start, 1.5) * smooth_gate(second_end - age, 1.5);
        } else {
            return 0.0F;
        }
    }

    if (wave.decay_per_second > 0.0F) {
        gain *= std::exp(-static_cast<double>(wave.decay_per_second) * age / 1000.0);
    }
    if (wave.modulation_hz > 0.0F) {
        const double seconds = age / 1000.0;
        gain *= 0.58 + (0.42 * std::sin(2.0 * std::numbers::pi * wave.modulation_hz * seconds));
    }
    return std::isfinite(gain) ? static_cast<float>(std::clamp(gain, 0.0, 1.0)) : 0.0F;
}
} // namespace fallout4_adapter::detail
