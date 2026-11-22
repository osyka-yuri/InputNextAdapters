#include "body_recipes.hpp"

#include <fallout_gamebryo/settings.hpp>
#include <inputnext_adapters/body_program.hpp>

#include <cstdint>

namespace fallout_gamebryo::detail {
using inputnext::adapters::WaveRecipe;

namespace {
constexpr WaveRecipe wave(float frequency,
    float second,
    float mix,
    float level,
    float noise,
    float attack,
    float release,
    float decay,
    std::uint16_t pulse,
    std::uint16_t gap = 0U,
    std::uint16_t second_pulse = 0U) noexcept {
    return {.frequency = frequency,
        .second_frequency = second,
        .second_mix = mix,
        .level = level,
        .noise = noise,
        .attack_ms = attack,
        .release_ms = release,
        .decay_per_second = decay,
        .first_pulse_ms = pulse,
        .pulse_gap_ms = gap,
        .second_pulse_ms = second_pulse};
}
} // namespace

[[nodiscard]] Recipe recipe_for(BodyEffect effect) noexcept {
    switch (effect) {
    case BodyEffect::shot_pistol:
        return {.wave = wave(112, 76, .23F, .64F, .15F, 1, 30, 5.0F, 24, 8, 22),
            .duration_ms = 90U,
            .priority = 52,
            .fallback_low = .28F,
            .fallback_high = .32F};
    case BodyEffect::shot_rifle:
        return {.wave = wave(86, 132, .32F, .65F, .20F, 1, 32, 5.2F, 28, 7, 24),
            .duration_ms = 105U,
            .priority = 52,
            .fallback_low = .30F,
            .fallback_high = .30F};
    case BodyEffect::shot_shotgun:
        return {.wave = wave(52, 98, .45F, .70F, .25F, 1, 42, 5.0F, 32, 8, 27),
            .duration_ms = 125U,
            .priority = 55,
            .fallback_low = .39F,
            .fallback_high = .33F};
    case BodyEffect::shot_heavy:
        return {.wave = wave(46, 72, .38F, .70F, .20F, 1, 48, 4.6F, 35, 10, 30),
            .duration_ms = 115U,
            .priority = 55,
            .fallback_low = .41F,
            .fallback_high = .30F};
    case BodyEffect::shot_energy:
        return {.wave = wave(128, 184, .34F, .58F, .12F, 1, 33, 4.2F, 22, 7, 24),
            .duration_ms = 98U,
            .priority = 52,
            .fallback_low = .23F,
            .fallback_high = .35F};
    case BodyEffect::shot_launcher:
        return {.wave = wave(44, 72, .30F, .72F, .18F, 2, 82, 3.1F, 42, 11, 38),
            .duration_ms = 145U,
            .priority = 62,
            .fallback_low = .57F,
            .fallback_high = .34F};
    case BodyEffect::shot_generic:
        return {.wave = wave(94, 128, .28F, .54F, .18F, 1, 30, 5.0F, 24, 8, 22),
            .duration_ms = 62U,
            .priority = 48,
            .fallback_low = .26F,
            .fallback_high = .28F};
    case BodyEffect::reload_start:
        return {.wave = wave(92, 154, .28F, .34F, .10F, 2, 28, 5.2F, 22),
            .duration_ms = 90U,
            .priority = 26,
            .fallback_low = .14F,
            .fallback_high = .16F};
    case BodyEffect::reload_finish:
        return {.wave = wave(148, 210, .30F, .38F, .08F, 1, 22, 5.0F, 20, 8, 18),
            .duration_ms = 90U,
            .priority = 32,
            .fallback_low = .12F,
            .fallback_high = .20F};
    case BodyEffect::dry_fire:
        return {.wave = wave(168, 102, .18F, .86F, .05F, 1, 24, 6.4F, 22),
            .duration_ms = 75U,
            .priority = 38,
            .fallback_low = .09F,
            .fallback_high = .27F};
    case BodyEffect::unarmed_swing:
        return {.wave = wave(68, 104, .34F, .40F, .17F, 3, 30, 5.0F, 26),
            .duration_ms = 100U,
            .priority = 34,
            .fallback_low = .19F,
            .fallback_high = .21F};
    case BodyEffect::melee_swing:
        return {.wave = wave(58, 92, .38F, .50F, .18F, 2, 36, 4.6F, 30),
            .duration_ms = 115U,
            .priority = 38,
            .fallback_low = .26F,
            .fallback_high = .23F};
    case BodyEffect::melee_hit:
        return {.wave = wave(72, 112, .30F, .52F, .15F, 1, 30, 4.8F, 26, 8, 24),
            .duration_ms = 135U,
            .priority = 74,
            .fallback_low = .30F,
            .fallback_high = .30F};
    case BodyEffect::hit_dealt:
        return {.wave = wave(104, 158, .36F, .48F, .12F, 1, 10, 5.0F, 24, 8, 24),
            .duration_ms = 64U,
            .priority = 68,
            .fallback_low = .27F,
            .fallback_high = .27F};
    case BodyEffect::hit_taken:
        return {.wave = wave(48, 78, .40F, .75F, .22F, 1, 50, 4.0F, 38, 11, 32),
            .duration_ms = 90U,
            .priority = 108,
            .fallback_low = .62F,
            .fallback_high = .48F};
    case BodyEffect::critical_hit:
        return {.wave = wave(126, 72, .28F, .62F, .15F, 1, 34, 4.8F, 28, 10, 32),
            .duration_ms = 110U,
            .priority = 88,
            .fallback_low = .36F,
            .fallback_high = .42F};
    case BodyEffect::sneak_critical:
        return {.wave = wave(158, 94, .34F, .58F, .09F, 1, 32, 4.6F, 20, 8, 38),
            .duration_ms = 118U,
            .priority = 92,
            .fallback_low = .31F,
            .fallback_high = .45F};
    case BodyEffect::crippled_hit:
        return {.wave = wave(46, 86, .46F, .70F, .20F, 1, 48, 3.9F, 34, 12, 28),
            .duration_ms = 116U,
            .priority = 110,
            .fallback_low = .63F,
            .fallback_high = .43F};
    case BodyEffect::block_impact:
        return {.wave = wave(82, 144, .39F, .58F, .12F, 1, 34, 4.6F, 25, 8, 22),
            .duration_ms = 100U,
            .priority = 111,
            .fallback_low = .40F,
            .fallback_high = .34F};
    case BodyEffect::stagger:
        return {.wave = wave(54, 102, .41F, .65F, .17F, 1, 45, 4.0F, 31, 14, 26),
            .duration_ms = 116U,
            .priority = 114,
            .fallback_low = .68F,
            .fallback_high = .51F};
    case BodyEffect::knockdown:
        return {.wave = wave(36, 64, .36F, .73F, .19F, 2, 58, 3.7F, 43, 18, 36),
            .duration_ms = 150U,
            .priority = 116,
            .fallback_low = .76F,
            .fallback_high = .54F};
    case BodyEffect::explosion:
        return {.wave = wave(38, 62, .45F, .78F, .24F, 2, 58, 3.6F, 45, 15, 42),
            .duration_ms = 160U,
            .priority = 112,
            .fallback_low = .70F,
            .fallback_high = .52F};
    case BodyEffect::explosion_dealt:
        return {.wave = wave(68, 118, .39F, .54F, .15F, 1, 38, 4.5F, 28, 9, 24),
            .duration_ms = 110U,
            .priority = 76,
            .fallback_low = .43F,
            .fallback_high = .36F};
    case BodyEffect::throwing:
        return {.wave = wave(64, 106, .34F, .46F, .16F, 1, 34, 4.6F, 26, 8, 23),
            .duration_ms = 150U,
            .priority = 47,
            .fallback_low = .26F,
            .fallback_high = .25F};
    case BodyEffect::vats_select:
        return {.wave = wave(184, 232, .25F, .24F, .05F, 2, 22, 8.0F, 14),
            .duration_ms = 75U,
            .priority = 18,
            .fallback_low = .08F,
            .fallback_high = .10F};
    case BodyEffect::vats_accept:
        return {.wave = wave(154, 216, .30F, .34F, .07F, 1, 24, 7.0F, 18, 7, 14),
            .duration_ms = 100U,
            .priority = 30,
            .fallback_low = .11F,
            .fallback_high = .13F};
    case BodyEffect::aid:
        return {.wave = wave(206, 278, .34F, .30F, .04F, 3, 50, 6.0F, 0),
            .duration_ms = 135U,
            .priority = 30,
            .fallback_low = .10F,
            .fallback_high = .16F};
    case BodyEffect::equip:
        return {.wave = wave(138, 94, .26F, .28F, .08F, 1, 24, 6.0F, 15),
            .duration_ms = 90U,
            .priority = 24,
            .fallback_low = .10F,
            .fallback_high = .12F};
    case BodyEffect::repair:
        return {.wave = wave(126, 182, .32F, .36F, .07F, 2, 38, 5.0F, 18, 8, 18),
            .duration_ms = 100U,
            .priority = 32,
            .fallback_low = .15F,
            .fallback_high = .18F};
    case BodyEffect::craft:
        return {.wave = wave(188, 264, .38F, .39F, .06F, 2, 42, 5.2F, 20, 10, 22),
            .duration_ms = 62U,
            .priority = 34,
            .fallback_low = .15F,
            .fallback_high = .19F};
    case BodyEffect::pick_lock:
        return {.wave = wave(240, 320, .36F, .35F, .04F, 1, 25, 6.0F, 18),
            .duration_ms = 110U,
            .priority = 40,
            .fallback_low = .15F,
            .fallback_high = .17F};
    case BodyEffect::break_lockpick:
        return {.wave = wave(92, 156, .34F, .42F, .12F, 1, 31, 4.8F, 22, 10, 18),
            .duration_ms = 80U,
            .priority = 42,
            .fallback_low = .22F,
            .fallback_high = .18F};
    case BodyEffect::terminal_success:
        return {.wave = wave(214, 294, .32F, .36F, .04F, 1, 32, 6.0F, 18, 8, 16),
            .duration_ms = 92U,
            .priority = 40,
            .fallback_low = .10F,
            .fallback_high = .20F};
    case BodyEffect::terminal_failure:
        return {.wave = wave(92, 132, .30F, .46F, .09F, 2, 36, 4.8F, 28, 11, 24),
            .duration_ms = 112U,
            .priority = 42,
            .fallback_low = .26F,
            .fallback_high = .16F};
    case BodyEffect::landing:
        return {.wave = wave(54, 82, .36F, .50F, .16F, 2, 36, 5.0F, 0),
            .duration_ms = 76U,
            .priority = 42,
            .fallback_low = .28F,
            .fallback_high = .22F};
    case BodyEffect::health_warning:
        return {.wave = wave(58, 92, .42F, .43F, .14F, 1, 34, 5.0F, 21, 10, 23),
            .duration_ms = 60U,
            .priority = 84,
            .fallback_low = .28F,
            .fallback_high = .24F};
    case BodyEffect::ap_warning:
        return {.wave = wave(124, 172, .34F, .34F, .08F, 1, 24, 6.5F, 0),
            .duration_ms = 42U,
            .priority = 56,
            .fallback_low = .14F,
            .fallback_high = .21F};
    case BodyEffect::critical_health:
        return {.wave = wave(48, 72, .42F, .37F, .12F, 2, 30, 4.8F, 20, 50, 20),
            .duration_ms = 104U,
            .priority = 86,
            .fallback_low = .24F,
            .fallback_high = .22F};
    case BodyEffect::radiation:
        return {.wave = wave(174, 242, .33F, .26F, .06F, 3, 44, 5.8F, 0),
            .duration_ms = 68U,
            .priority = 22,
            .fallback_low = .08F,
            .fallback_high = .14F};
    case BodyEffect::ordinary_step:
        return {.wave = wave(92, 128, .22F, .12F, .06F, 2, 16, 6.0F, 0),
            .duration_ms = 54U,
            .priority = 8,
            .fallback_low = .05F,
            .fallback_high = .06F};
    case BodyEffect::run_step:
        return {.wave = wave(80, 120, .24F, .18F, .09F, 2, 18, 5.6F, 0),
            .duration_ms = 50U,
            .priority = 10,
            .fallback_low = .08F,
            .fallback_high = .10F};
    case BodyEffect::sneak_step:
        return {.wave = wave(136, 192, .25F, .08F, .03F, 2, 14, 7.0F, 0),
            .duration_ms = 46U,
            .priority = 7,
            .fallback_low = .03F,
            .fallback_high = .04F};
    case BodyEffect::progress:
        return {.wave = wave(234, 312, .32F, .32F, .04F, 2, 35, 6.0F, 0),
            .duration_ms = 58U,
            .priority = 28,
            .fallback_low = .11F,
            .fallback_high = .16F};
    case BodyEffect::count:
        break;
    }
    return {.wave = wave(90, 130, .25F, .30F, .08F, 1, 30, 5.0F, 0),
        .duration_ms = 50U,
        .priority = 20,
        .fallback_low = .12F,
        .fallback_high = .12F};
}
} // namespace fallout_gamebryo::detail
