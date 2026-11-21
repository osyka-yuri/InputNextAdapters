#include "body_effects.hpp"

#include "effect_model.hpp"
#include "effect_settings.hpp"

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

namespace oblivion_adapter {
namespace {
using inputnext::adapters::BodyVoice;
using inputnext::adapters::OutputFrame;
using inputnext::adapters::RumbleIntent;
using inputnext::adapters::WaveRecipe;

constexpr float body_peak = 0.75F;
constexpr std::uint32_t refresh_ttl_ms = 100;

struct Carriers {
    float frequency{};
    float second_frequency{};
    float second_mix{};
};

struct Texture {
    float level;
    float noise;
};

struct Envelope {
    float attack_ms;
    float release_ms;
    float decay_per_second;
};

struct Motors {
    float low;
    float high;
};

struct Heartbeat {
    std::uint16_t first_ms;
    std::uint16_t gap_ms;
    std::uint16_t second_ms;
};

struct RecipeSpec {
    std::uint32_t duration_ms{};
    int priority{};
    Carriers carriers{};
    Texture texture{};
    Envelope envelope{};
    Motors motors{};
    Heartbeat beats{};
};

constexpr Heartbeat heartbeat{.first_ms = 38, .gap_ms = 90, .second_ms = 55};
constexpr std::uint32_t heartbeat_duration_ms =
    static_cast<std::uint32_t>(heartbeat.first_ms) + heartbeat.gap_ms + heartbeat.second_ms;

constexpr RecipeSpec recipe_spec(EffectId effect) noexcept {
    // Each recipe lists duration, priority, carriers, level and noise, envelope,
    // classic motor levels and optional beat timing.
    switch (effect) {
    case EffectId::unarmed_swing:
        return {.duration_ms = 160,
            .priority = 42,
            .carriers = {.frequency = 98},
            .texture = {.level = .36F, .noise = .04F},
            .envelope = {.attack_ms = 9, .release_ms = 110, .decay_per_second = 5},
            .motors = {.low = .12F, .high = .18F}};
    case EffectId::blade_one_hand_swing:
        return {.duration_ms = 180,
            .priority = 44,
            .carriers = {.frequency = 112},
            .texture = {.level = .4F, .noise = .08F},
            .envelope = {.attack_ms = 4, .release_ms = 125, .decay_per_second = 4},
            .motors = {.low = .16F, .high = .2F}};
    case EffectId::blade_two_hand_swing:
        return {.duration_ms = 220,
            .priority = 46,
            .carriers = {.frequency = 72, .second_frequency = 118, .second_mix = .24F},
            .texture = {.level = .42F, .noise = .05F},
            .envelope = {.attack_ms = 5, .release_ms = 155, .decay_per_second = 3},
            .motors = {.low = .2F, .high = .18F}};
    case EffectId::blunt_one_hand_swing:
        return {.duration_ms = 185,
            .priority = 43,
            .carriers = {.frequency = 58, .second_frequency = 92, .second_mix = .45F},
            .texture = {.level = .42F, .noise = .13F},
            .envelope = {.attack_ms = 2, .release_ms = 145, .decay_per_second = 4},
            .motors = {.low = .22F, .high = .16F}};
    case EffectId::blunt_two_hand_swing:
        return {.duration_ms = 230,
            .priority = 45,
            .carriers = {.frequency = 44, .second_frequency = 72, .second_mix = .5F},
            .texture = {.level = .48F, .noise = .18F},
            .envelope = {.attack_ms = 1, .release_ms = 180, .decay_per_second = 3},
            .motors = {.low = .27F, .high = .18F}};
    case EffectId::unarmed_hit:
        return {.duration_ms = 135,
            .priority = 105,
            .carriers = {.frequency = 66, .second_frequency = 118, .second_mix = .3F},
            .texture = {.level = .58F, .noise = .23F},
            .envelope = {.attack_ms = 1, .release_ms = 105, .decay_per_second = 5},
            .motors = {.low = .38F, .high = .32F}};
    case EffectId::blade_one_hand_hit:
        return {.duration_ms = 150,
            .priority = 112,
            .carriers = {.frequency = 78, .second_frequency = 142, .second_mix = .3F},
            .texture = {.level = .62F, .noise = .28F},
            .envelope = {.attack_ms = 1, .release_ms = 125, .decay_per_second = 4},
            .motors = {.low = .45F, .high = .38F}};
    case EffectId::blade_two_hand_hit:
        return {.duration_ms = 180,
            .priority = 114,
            .carriers = {.frequency = 57, .second_frequency = 105, .second_mix = .38F},
            .texture = {.level = .7F, .noise = .24F},
            .envelope = {.attack_ms = 1, .release_ms = 150, .decay_per_second = 3},
            .motors = {.low = .52F, .high = .44F}};
    case EffectId::blunt_one_hand_hit:
        return {.duration_ms = 165,
            .priority = 116,
            .carriers = {.frequency = 43, .second_frequency = 78, .second_mix = .54F},
            .texture = {.level = .7F, .noise = .35F},
            .envelope = {.attack_ms = 1, .release_ms = 140, .decay_per_second = 4},
            .motors = {.low = .54F, .high = .38F}};
    case EffectId::blunt_two_hand_hit:
        return {.duration_ms = 205,
            .priority = 120,
            .carriers = {.frequency = 32, .second_frequency = 64, .second_mix = .58F},
            .texture = {.level = .74F, .noise = .32F},
            .envelope = {.attack_ms = 1, .release_ms = 170, .decay_per_second = 3},
            .motors = {.low = .62F, .high = .45F}};
    case EffectId::generic_hit:
        return {.duration_ms = 145,
            .priority = 96,
            .carriers = {.frequency = 58, .second_frequency = 96, .second_mix = .35F},
            .texture = {.level = .55F, .noise = .22F},
            .envelope = {.attack_ms = 1, .release_ms = 115, .decay_per_second = 4},
            .motors = {.low = .38F, .high = .3F}};
    case EffectId::incoming_damage:
        return {.duration_ms = 190,
            .priority = 110,
            .carriers = {.frequency = 42, .second_frequency = 76, .second_mix = .48F},
            .texture = {.level = .72F, .noise = .3F},
            .envelope = {.attack_ms = 1, .release_ms = 175, .decay_per_second = 3},
            .motors = {.low = .58F, .high = .46F}};
    case EffectId::block_hit:
        return {.duration_ms = 155,
            .priority = 98,
            .carriers = {.frequency = 52, .second_frequency = 132, .second_mix = .4F},
            .texture = {.level = .64F, .noise = .25F},
            .envelope = {.attack_ms = 1, .release_ms = 125, .decay_per_second = 4},
            .motors = {.low = .48F, .high = .42F}};
    case EffectId::recoil:
        return {.duration_ms = 135,
            .priority = 94,
            .carriers = {.frequency = 74, .second_frequency = 122, .second_mix = .3F},
            .texture = {.level = .48F, .noise = .2F},
            .envelope = {.attack_ms = 1, .release_ms = 110, .decay_per_second = 5},
            .motors = {.low = .34F, .high = .32F}};
    case EffectId::stagger:
        return {.duration_ms = 235,
            .priority = 105,
            .carriers = {.frequency = 36, .second_frequency = 68, .second_mix = .53F},
            .texture = {.level = .62F, .noise = .31F},
            .envelope = {.attack_ms = 1, .release_ms = 220, .decay_per_second = 2},
            .motors = {.low = .44F, .high = .58F}};
    case EffectId::dodge:
        return {.duration_ms = 105,
            .priority = 66,
            .carriers = {.frequency = 106},
            .texture = {.level = .4F, .noise = .03F},
            .envelope = {.attack_ms = 14, .release_ms = 75, .decay_per_second = 5},
            .motors = {.low = .2F, .high = .28F}};
    case EffectId::knockout:
        return {.duration_ms = 310,
            .priority = 125,
            .carriers = {.frequency = 27, .second_frequency = 49, .second_mix = .58F},
            .texture = {.level = .72F, .noise = .12F},
            .envelope = {.attack_ms = 1, .release_ms = 280, .decay_per_second = 2},
            .motors = {.low = .52F, .high = .64F}};
    case EffectId::bow_draw:
        return {.duration_ms = 170,
            .priority = 30,
            .carriers = {.frequency = 74, .second_frequency = 128, .second_mix = .38F},
            .texture = {.level = .28F, .noise = .03F},
            .envelope = {.attack_ms = 0, .release_ms = 65, .decay_per_second = 0},
            .motors = {.low = .1F, .high = .12F}};
    case EffectId::bow_attached:
        return {.duration_ms = 170,
            .priority = 28,
            .carriers = {.frequency = 54, .second_frequency = 92, .second_mix = .42F},
            .texture = {.level = .3F, .noise = .03F},
            .envelope = {.attack_ms = 0, .release_ms = 70, .decay_per_second = 0},
            .motors = {.low = .08F, .high = .1F}};
    case EffectId::bow_shot:
        return {.duration_ms = 175,
            .priority = 107,
            .carriers = {.frequency = 52, .second_frequency = 132, .second_mix = .34F},
            .texture = {.level = .62F, .noise = .22F},
            .envelope = {.attack_ms = 1, .release_ms = 130, .decay_per_second = 3},
            .motors = {.low = .46F, .high = .4F}};
    case EffectId::bow_hit:
        return {.duration_ms = 150,
            .priority = 100,
            .carriers = {.frequency = 51, .second_frequency = 115, .second_mix = .36F},
            .texture = {.level = .58F, .noise = .27F},
            .envelope = {.attack_ms = 1, .release_ms = 125, .decay_per_second = 4},
            .motors = {.low = .42F, .high = .34F}};
    case EffectId::casting:
        return {.duration_ms = 160,
            .priority = 34,
            .carriers = {.frequency = 60, .second_frequency = 98, .second_mix = .34F},
            .texture = {.level = .26F, .noise = .02F},
            .envelope = {.attack_ms = 0, .release_ms = 70, .decay_per_second = 0},
            .motors = {.low = .06F, .high = .08F}};
    case EffectId::fire:
        return {.duration_ms = 105,
            .priority = 72,
            .carriers = {.frequency = 35, .second_frequency = 74, .second_mix = .38F},
            .texture = {.level = .55F, .noise = .14F},
            .envelope = {.attack_ms = 2, .release_ms = 115, .decay_per_second = 5},
            .motors = {.low = .16F, .high = .3F}};
    case EffectId::frost:
        return {.duration_ms = 120,
            .priority = 75,
            .carriers = {.frequency = 45, .second_frequency = 94, .second_mix = .32F},
            .texture = {.level = .52F, .noise = .09F},
            .envelope = {.attack_ms = 2, .release_ms = 150, .decay_per_second = 3},
            .motors = {.low = .24F, .high = .17F}};
    case EffectId::shock:
        return {.duration_ms = 95,
            .priority = 88,
            .carriers = {.frequency = 102, .second_frequency = 188, .second_mix = .16F},
            .texture = {.level = .54F, .noise = .2F},
            .envelope = {.attack_ms = 2, .release_ms = 75, .decay_per_second = 8},
            .motors = {.low = .08F, .high = .34F}};
    case EffectId::healing:
        return {.duration_ms = 150,
            .priority = 67,
            .carriers = {.frequency = 41, .second_frequency = 82, .second_mix = .5F},
            .texture = {.level = .35F, .noise = .04F},
            .envelope = {.attack_ms = 3, .release_ms = 165, .decay_per_second = 2},
            .motors = {.low = .08F, .high = .1F}};
    case EffectId::shield:
        return {.duration_ms = 180,
            .priority = 57,
            .carriers = {.frequency = 54, .second_frequency = 118, .second_mix = .34F},
            .texture = {.level = .38F, .noise = .12F},
            .envelope = {.attack_ms = 1, .release_ms = 105, .decay_per_second = 5},
            .motors = {.low = .18F, .high = .16F}};
    case EffectId::generic_magic:
        return {.duration_ms = 115,
            .priority = 64,
            .carriers = {.frequency = 58, .second_frequency = 110, .second_mix = .32F},
            .texture = {.level = .48F, .noise = .11F},
            .envelope = {.attack_ms = 1, .release_ms = 100, .decay_per_second = 4},
            .motors = {.low = .14F, .high = .2F}};
    case EffectId::magic_applied:
        return {.duration_ms = 105,
            .priority = 65,
            .carriers = {.frequency = 64, .second_frequency = 126, .second_mix = .32F},
            .texture = {.level = .5F, .noise = .14F},
            .envelope = {.attack_ms = 1, .release_ms = 110, .decay_per_second = 4},
            .motors = {.low = .16F, .high = .24F}};
    case EffectId::heartbeat:
        return {.duration_ms = heartbeat_duration_ms,
            .priority = 55,
            .carriers = {.frequency = 46, .second_frequency = 76, .second_mix = .35F},
            .texture = {.level = .43F, .noise = .04F},
            .envelope = {.attack_ms = 1, .release_ms = 0, .decay_per_second = 0},
            .motors = {.low = .16F, .high = .08F},
            .beats = heartbeat};
    case EffectId::magicka_depleted:
        return {.duration_ms = 150,
            .priority = 73,
            .carriers = {.frequency = 38, .second_frequency = 72, .second_mix = .52F},
            .texture = {.level = .45F, .noise = .04F},
            .envelope = {.attack_ms = 2, .release_ms = 145, .decay_per_second = 2},
            .motors = {.low = .1F, .high = .14F}};
    case EffectId::fatigue_depleted:
        return {.duration_ms = 165,
            .priority = 78,
            .carriers = {.frequency = 31, .second_frequency = 63, .second_mix = .53F},
            .texture = {.level = .55F, .noise = .06F},
            .envelope = {.attack_ms = 2, .release_ms = 160, .decay_per_second = 2},
            .motors = {.low = .13F, .high = .12F}};
    case EffectId::landing:
        return {.duration_ms = 180,
            .priority = 114,
            .carriers = {.frequency = 36, .second_frequency = 72, .second_mix = .5F},
            .texture = {.level = .64F, .noise = .32F},
            .envelope = {.attack_ms = 1, .release_ms = 140, .decay_per_second = 3},
            .motors = {.low = .5F, .high = .4F}};
    case EffectId::water_dive:
        return {.duration_ms = 160,
            .priority = 58,
            .carriers = {.frequency = 42, .second_frequency = 82, .second_mix = .43F},
            .texture = {.level = .34F, .noise = .05F},
            .envelope = {.attack_ms = 2, .release_ms = 110, .decay_per_second = 2},
            .motors = {.low = .1F, .high = .12F}};
    case EffectId::water_surface:
        return {.duration_ms = 165,
            .priority = 76,
            .carriers = {.frequency = 48, .second_frequency = 94, .second_mix = .4F},
            .texture = {.level = .4F, .noise = .07F},
            .envelope = {.attack_ms = 2, .release_ms = 105, .decay_per_second = 4},
            .motors = {.low = .14F, .high = .18F}};
    case EffectId::mount:
        return {.duration_ms = 180,
            .priority = 89,
            .carriers = {.frequency = 40, .second_frequency = 74, .second_mix = .48F},
            .texture = {.level = .4F, .noise = .1F},
            .envelope = {.attack_ms = 2, .release_ms = 135, .decay_per_second = 2},
            .motors = {.low = .22F, .high = .2F}};
    case EffectId::dismount:
        return {.duration_ms = 150,
            .priority = 90,
            .carriers = {.frequency = 47, .second_frequency = 86, .second_mix = .42F},
            .texture = {.level = .42F, .noise = .04F},
            .envelope = {.attack_ms = 2, .release_ms = 115, .decay_per_second = 3},
            .motors = {.low = .18F, .high = .2F}};
    case EffectId::walk:
        return {.duration_ms = 55,
            .priority = 8,
            .carriers = {.frequency = 76},
            .texture = {.level = .2F, .noise = .02F},
            .envelope = {.attack_ms = 2, .release_ms = 44, .decay_per_second = 6},
            .motors = {.low = .025F, .high = .035F}};
    case EffectId::run:
        return {.duration_ms = 55,
            .priority = 7,
            .carriers = {.frequency = 64, .second_frequency = 104, .second_mix = .24F},
            .texture = {.level = .25F, .noise = .06F},
            .envelope = {.attack_ms = 1, .release_ms = 45, .decay_per_second = 6},
            .motors = {.low = .04F, .high = .055F}};
    case EffectId::sneak:
        return {.duration_ms = 45,
            .priority = 8,
            .carriers = {.frequency = 94},
            .texture = {.level = .16F, .noise = .02F},
            .envelope = {.attack_ms = 3, .release_ms = 36, .decay_per_second = 7},
            .motors = {.low = .018F, .high = .024F}};
    case EffectId::mounted_walk:
        return {.duration_ms = 55,
            .priority = 7,
            .carriers = {.frequency = 49, .second_frequency = 83, .second_mix = .22F},
            .texture = {.level = .2F, .noise = .07F},
            .envelope = {.attack_ms = 1, .release_ms = 45, .decay_per_second = 5},
            .motors = {.low = .035F, .high = .05F}};
    case EffectId::mounted_run:
        return {.duration_ms = 55,
            .priority = 7,
            .carriers = {.frequency = 41, .second_frequency = 72, .second_mix = .26F},
            .texture = {.level = .24F, .noise = .08F},
            .envelope = {.attack_ms = 1, .release_ms = 46, .decay_per_second = 5},
            .motors = {.low = .05F, .high = .065F}};
    case EffectId::drink_potion:
        return {.duration_ms = 100,
            .priority = 45,
            .carriers = {.frequency = 66, .second_frequency = 142, .second_mix = .25F},
            .texture = {.level = .42F, .noise = .02F},
            .envelope = {.attack_ms = 2, .release_ms = 75, .decay_per_second = 4},
            .motors = {.low = .08F, .high = .1F}};
    case EffectId::eat_ingredient:
        return {.duration_ms = 95,
            .priority = 42,
            .carriers = {.frequency = 87},
            .texture = {.level = .3F, .noise = .03F},
            .envelope = {.attack_ms = 1, .release_ms = 80, .decay_per_second = 3},
            .motors = {.low = .06F, .high = .08F}};
    case EffectId::equip:
        return {.duration_ms = 95,
            .priority = 50,
            .carriers = {.frequency = 78, .second_frequency = 144, .second_mix = .24F},
            .texture = {.level = .4F, .noise = .05F},
            .envelope = {.attack_ms = 1, .release_ms = 100, .decay_per_second = 3},
            .motors = {.low = .1F, .high = .12F}};
    case EffectId::unequip:
        return {.duration_ms = 100,
            .priority = 43,
            .carriers = {.frequency = 60, .second_frequency = 118, .second_mix = .28F},
            .texture = {.level = .32F, .noise = .04F},
            .envelope = {.attack_ms = 1, .release_ms = 95, .decay_per_second = 3},
            .motors = {.low = .08F, .high = .1F}};
    case EffectId::drop_item:
        return {.duration_ms = 105,
            .priority = 53,
            .carriers = {.frequency = 70, .second_frequency = 126, .second_mix = .25F},
            .texture = {.level = .35F, .noise = .06F},
            .envelope = {.attack_ms = 1, .release_ms = 92, .decay_per_second = 3},
            .motors = {.low = .09F, .high = .11F}};
    case EffectId::create_potion:
        return {.duration_ms = 220,
            .priority = 81,
            .carriers = {.frequency = 34, .second_frequency = 66, .second_mix = .48F},
            .texture = {.level = .48F, .noise = .03F},
            .envelope = {.attack_ms = 3, .release_ms = 180, .decay_per_second = 2},
            .motors = {.low = .12F, .high = .15F}};
    case EffectId::enchant:
        return {.duration_ms = 240,
            .priority = 84,
            .carriers = {.frequency = 29, .second_frequency = 54, .second_mix = .5F},
            .texture = {.level = .5F, .noise = .02F},
            .envelope = {.attack_ms = 3, .release_ms = 210, .decay_per_second = 2},
            .motors = {.low = .14F, .high = .17F}};
    case EffectId::create_spell:
        return {.duration_ms = 235,
            .priority = 83,
            .carriers = {.frequency = 36, .second_frequency = 72, .second_mix = .42F},
            .texture = {.level = .46F, .noise = .03F},
            .envelope = {.attack_ms = 3, .release_ms = 190, .decay_per_second = 2},
            .motors = {.low = .12F, .high = .16F}};
    case EffectId::lock_opened:
        return {.duration_ms = 130,
            .priority = 57,
            .carriers = {.frequency = 66, .second_frequency = 122, .second_mix = .28F},
            .texture = {.level = .48F, .noise = .02F},
            .envelope = {.attack_ms = 2, .release_ms = 85, .decay_per_second = 4},
            .motors = {.low = .1F, .high = .12F}};
    case EffectId::pick_broken:
        return {.duration_ms = 185,
            .priority = 82,
            .carriers = {.frequency = 42, .second_frequency = 76, .second_mix = .46F},
            .texture = {.level = .56F, .noise = .16F},
            .envelope = {.attack_ms = 1, .release_ms = 165, .decay_per_second = 2},
            .motors = {.low = .22F, .high = .18F}};
    case EffectId::vampire_feed:
        return {.duration_ms = 165,
            .priority = 73,
            .carriers = {.frequency = 32, .second_frequency = 61, .second_mix = .48F},
            .texture = {.level = .39F, .noise = .07F},
            .envelope = {.attack_ms = 2, .release_ms = 130, .decay_per_second = 2},
            .motors = {.low = .1F, .high = .13F}};
    case EffectId::soul_trap:
        return {.duration_ms = 180,
            .priority = 70,
            .carriers = {.frequency = 28, .second_frequency = 58, .second_mix = .5F},
            .texture = {.level = .4F, .noise = .03F},
            .envelope = {.attack_ms = 2, .release_ms = 155, .decay_per_second = 2},
            .motors = {.low = .1F, .high = .13F}};
    case EffectId::pickpocketed:
        return {.duration_ms = 75,
            .priority = 55,
            .carriers = {.frequency = 112, .second_frequency = 168, .second_mix = .26F},
            .texture = {.level = .29F, .noise = .02F},
            .envelope = {.attack_ms = 2, .release_ms = 50, .decay_per_second = 6},
            .motors = {.low = .035F, .high = .08F}};
    case EffectId::skill_up:
        return {.duration_ms = 215,
            .priority = 79,
            .carriers = {.frequency = 37, .second_frequency = 73, .second_mix = .43F},
            .texture = {.level = .43F, .noise = .02F},
            .envelope = {.attack_ms = 3, .release_ms = 165, .decay_per_second = 2},
            .motors = {.low = .1F, .high = .14F}};
    case EffectId::quest_complete:
        return {.duration_ms = 280,
            .priority = 88,
            .carriers = {.frequency = 24, .second_frequency = 51, .second_mix = .48F},
            .texture = {.level = .51F, .noise = .02F},
            .envelope = {.attack_ms = 3, .release_ms = 220, .decay_per_second = 2},
            .motors = {.low = .16F, .high = .19F}};
    case EffectId::discovered:
        return {.duration_ms = 200,
            .priority = 74,
            .carriers = {.frequency = 36, .second_frequency = 70, .second_mix = .42F},
            .texture = {.level = .4F, .noise = .03F},
            .envelope = {.attack_ms = 3, .release_ms = 165, .decay_per_second = 2},
            .motors = {.low = .1F, .high = .14F}};
    case EffectId::trained:
        return {.duration_ms = 155,
            .priority = 60,
            .carriers = {.frequency = 74, .second_frequency = 124, .second_mix = .32F},
            .texture = {.level = .36F, .noise = .02F},
            .envelope = {.attack_ms = 5, .release_ms = 110, .decay_per_second = 4},
            .motors = {.low = .075F, .high = .11F}};
    case EffectId::gate_closed:
        return {.duration_ms = 350,
            .priority = 86,
            .carriers = {.frequency = 33, .second_frequency = 71, .second_mix = .34F},
            .texture = {.level = .54F, .noise = .09F},
            .envelope = {.attack_ms = 3, .release_ms = 280, .decay_per_second = 2},
            .motors = {.low = .16F, .high = .13F}};
    case EffectId::count:
        break;
    }
    return {};
}

float bounded(float value, float low, float high) noexcept {
    return std::isfinite(value) ? std::clamp(value, low, high) : 1.0F;
}

std::uint32_t scaled_ms(std::uint32_t duration, float scale) noexcept {
    return static_cast<std::uint32_t>(std::lround(static_cast<double>(duration) * scale));
}

std::uint64_t voice_id(EffectId effect, const CueOrigin& origin) noexcept {
    std::uint64_t id = 14695981039346656037ULL;
    const std::array values{static_cast<std::uint64_t>(effect),
        static_cast<std::uint64_t>(origin.at.time_since_epoch().count()),
        origin.frame,
        origin.sequence,
        static_cast<std::uint64_t>(origin.source_id),
        static_cast<std::uint64_t>(origin.target_id)};
    for (auto value : values) {
        for (unsigned byte = 0; byte < 8; ++byte) {
            id ^= static_cast<std::uint8_t>(value);
            id *= 1099511628211ULL;
            value >>= 8U;
        }
    }
    return id == 0 ? 1 : id;
}

void apply_frequency_override(WaveRecipe& wave, std::optional<float> frequency) noexcept {
    if (!frequency || !std::isfinite(*frequency) || *frequency < 20.0F || *frequency > 1'000.0F) {
        return;
    }
    if (wave.second_frequency > 0.0F) {
        const float ratio = wave.second_frequency / wave.frequency;
        wave.second_frequency = std::clamp(*frequency * ratio, 20.0F, 1'000.0F);
    }
    wave.frequency = *frequency;
}

} // namespace

struct BodyEffects::RenderState {
    std::array<RankedVoice, std::tuple_size_v<decltype(OutputFrame{}.body.voices)>> hd_voices{};
    int classic_priority{std::numeric_limits<int>::min()};
};

std::uint32_t base_effect_duration_ms(EffectId effect) noexcept {
    return recipe_spec(effect).duration_ms;
}

BodyEffects::BodyEffects(const EffectSettings& settings) noexcept
    : master_(settings.body_enabled ? bounded(settings.rumble_scale, 0.0F, 1.0F) : 0.0F) {
    if (master_ == 0.0F) {
        return;
    }
    for (std::size_t index = 0; index < profiles_.size(); ++index) {
        const auto& adjustment = settings.profiles[index];
        if (!adjustment.enabled) {
            continue;
        }
        const auto effect = static_cast<EffectId>(index);
        const RecipeSpec spec = recipe_spec(effect);
        const auto duration_scale = bounded(adjustment.duration_scale, 0.25F, 2.0F);
        const auto classic_scale = master_ * bounded(adjustment.classic_scale, 0.0F, 2.0F);
        auto& profile = profiles_[index];
        profile = {
            .wave =
                {
                    .frequency = spec.carriers.frequency,
                    .second_frequency = spec.carriers.second_frequency,
                    .second_mix = spec.carriers.second_mix,
                    .level = spec.texture.level,
                    .noise = spec.texture.noise,
                    .attack_ms = spec.envelope.attack_ms,
                    .release_ms = spec.envelope.release_ms,
                    .decay_per_second = spec.envelope.decay_per_second,
                    .first_pulse_ms = spec.beats.first_ms,
                    .pulse_gap_ms = spec.beats.gap_ms,
                    .second_pulse_ms = spec.beats.second_ms,
                },
            .held_second_mix = spec.carriers.second_mix,
            .hd_scale = bounded(adjustment.hd_scale, 0.0F, 2.0F),
            .duration_scale = duration_scale,
            .classic_low = spec.motors.low * classic_scale,
            .classic_high = spec.motors.high * classic_scale,
            .duration_ms = scaled_ms(base_effect_duration_ms(effect), duration_scale),
            .first_beat_end_ms = scaled_ms(spec.beats.first_ms, duration_scale),
            .second_beat_start_ms =
                scaled_ms(static_cast<std::uint32_t>(spec.beats.first_ms) + spec.beats.gap_ms,
                    duration_scale),
            .priority = spec.priority,
            .enabled = true,
        };
        if (profile.hd_scale == 0.0F && classic_scale == 0.0F) {
            profile.enabled = false;
            continue;
        }
        if (settings.audio_haptics_enabled && adjustment.audio &&
            inputnext::adapters::apply_audio_haptic_profile(profile.wave, *adjustment.audio)) {
            // Use the mean of the validated nonzero envelope for sustained textures.
            const auto& audio = adjustment.audio->envelope;
            double primary = 0.0;
            double secondary = 0.0;
            for (std::size_t knot = 0; knot < audio.primary.size(); ++knot) {
                primary += audio.primary[knot];
                secondary += audio.secondary[knot];
            }
            const double total = primary + secondary;
            profile.held_gain =
                static_cast<float>(total / static_cast<double>(audio.primary.size()));
            profile.held_second_mix = static_cast<float>(secondary / total);
        }
        apply_frequency_override(profile.wave, adjustment.frequency_hz);
    }
}

void BodyEffects::reset() noexcept {
    cues_ = {};
}

void BodyEffects::expire(MonoTime now) noexcept {
    for (auto& cue : cues_) {
        if (cue.effect != EffectId::count && now >= cue.origin.at &&
            now - cue.origin.at >=
                std::chrono::milliseconds{profiles_[profile_index(cue.effect)].duration_ms}) {
            cue.effect = EffectId::count;
        }
    }
}

void BodyEffects::cancel(EffectId effect) noexcept {
    for (auto& cue : cues_) {
        if (cue.effect == effect) {
            cue.effect = EffectId::count;
        }
    }
}

bool BodyEffects::profile_enabled(EffectId effect) const noexcept {
    return effect != EffectId::count && profiles_[profile_index(effect)].enabled;
}

void BodyEffects::play(EffectId effect,
    CueOrigin origin,
    float strength,
    CuePriority priority) noexcept {
    if (!profile_enabled(effect) || !std::isfinite(strength) || strength <= 0.0F) {
        return;
    }
    strength = std::min(strength, 1.25F);
    auto rank = profiles_[profile_index(effect)].priority;
    if (priority == CuePriority::impact) {
        rank = std::max(rank, recipe_spec(EffectId::incoming_damage).priority);
    }
    const auto stable_id = voice_id(effect, origin);
    const auto repeated = std::ranges::find_if(cues_, [&](const Cue& cue) {
        return cue.effect != EffectId::count && cue.stable_id == stable_id;
    });
    if (repeated != cues_.end()) {
        repeated->strength = std::max(repeated->strength, strength);
        repeated->priority = std::max(repeated->priority, rank);
        return;
    }

    auto slot = std::ranges::find(cues_, EffectId::count, &Cue::effect);
    if (slot == cues_.end()) {
        slot = std::ranges::min_element(cues_, {}, &Cue::priority);
        if (slot->priority > rank) {
            return;
        }
    }
    *slot = {.effect = effect,
        .origin = origin,
        .stable_id = stable_id,
        .strength = strength,
        .priority = rank};
}

bool BodyEffects::cue_elapsed_ms(const Cue& cue,
    bool sustained,
    MonoTime now,
    std::uint32_t& elapsed_ms) const noexcept {
    if (cue.effect == EffectId::count || now < cue.origin.at) {
        return false;
    }
    const auto& profile = profiles_[profile_index(cue.effect)];
    if (!profile.enabled) {
        return false;
    }
    if (sustained) {
        elapsed_ms = 0;
        return true;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - cue.origin.at).count();
    if (std::cmp_greater_equal(elapsed, profile.duration_ms)) {
        return false;
    }
    elapsed_ms = static_cast<std::uint32_t>(elapsed);
    return true;
}

bool BodyEffects::held_has_onset(const HeldBodyEffect& hold,
    const Profile& profile,
    MonoTime now) const noexcept {
    return std::ranges::any_of(cues_, [&](const Cue& cue) {
        if (cue.effect != hold.effect || cue.origin.at > now) {
            return false;
        }
        return now - cue.origin.at < std::chrono::milliseconds{profile.duration_ms};
    });
}

BodyEffects::Cue BodyEffects::held_cue(const HeldBodyEffect& hold,
    const Profile& profile) noexcept {
    const CueOrigin origin{.at = hold.started_at, .source_id = hold.source_id};
    return {.effect = hold.effect,
        .origin = origin,
        .stable_id = voice_id(hold.effect, origin),
        .strength = 1.0F,
        .priority = profile.priority};
}

void BodyEffects::add_hd_voice(RenderState& state,
    const Cue& cue,
    const Profile& profile,
    bool sustained) noexcept {
    if (profile.hd_scale <= 0.0F) {
        return;
    }
    const auto slot = std::ranges::find_if(state.hd_voices, [&](const RankedVoice& voice) {
        return !voice.profile || cue.priority > voice.priority ||
               (cue.priority == voice.priority && cue.origin.at < voice.origin.at);
    });
    if (slot == state.hd_voices.end()) {
        return;
    }
    std::move_backward(slot, state.hd_voices.end() - 1, state.hd_voices.end());
    *slot = {.profile = &profile,
        .origin = cue.origin,
        .stable_id = cue.stable_id,
        .strength = cue.strength,
        .priority = cue.priority,
        .held = sustained};
}

void BodyEffects::add_classic_voice(RenderState& state,
    OutputFrame& frame,
    const Cue& cue,
    const Profile& profile,
    bool sustained,
    std::uint32_t elapsed_ms) const noexcept {
    if (cue.priority <= state.classic_priority ||
        (profile.classic_low == 0.0F && profile.classic_high == 0.0F)) {
        return;
    }

    std::uint32_t ttl = refresh_ttl_ms;
    if (!sustained) {
        auto end_ms = profile.duration_ms;
        if (profile.first_beat_end_ms != 0) {
            if (elapsed_ms < profile.first_beat_end_ms) {
                end_ms = profile.first_beat_end_ms;
            } else if (elapsed_ms < profile.second_beat_start_ms) {
                return;
            }
        }
        ttl = std::min(ttl, end_ms - elapsed_ms);
    }

    const float peak = body_peak * master_;
    frame.rumble = RumbleIntent{
        .payload = {.low_frequency = std::min(peak, profile.classic_low * cue.strength),
            .high_frequency = std::min(peak, profile.classic_high * cue.strength)},
        .ttl_ms = ttl};
    state.classic_priority = cue.priority;
}

void BodyEffects::mix_cue(RenderState& state,
    OutputFrame& frame,
    const Cue& cue,
    bool sustained,
    MonoTime now) const noexcept {
    std::uint32_t elapsed_ms = 0;
    if (!cue_elapsed_ms(cue, sustained, now, elapsed_ms)) {
        return;
    }
    const auto& profile = profiles_[profile_index(cue.effect)];
    add_hd_voice(state, cue, profile, sustained);
    add_classic_voice(state, frame, cue, profile, sustained, elapsed_ms);
}

void BodyEffects::render_voice(OutputFrame& frame,
    const RankedVoice& ranked,
    std::size_t index) const noexcept {
    const auto& profile = *ranked.profile;
    auto wave = profile.wave;
    wave.level = std::min(body_peak, wave.level * ranked.strength);
    if (ranked.held) {
        wave.level = std::min(body_peak, wave.level * profile.held_gain);
        wave.second_mix = profile.held_second_mix;
        wave.audio_envelope = {};
    }
    frame.body.voices[index] = BodyVoice{
        .wave = wave,
        .started_at = ranked.origin.at,
        .duration_ms =
            ranked.held ? std::numeric_limits<std::uint32_t>::max() : profile.duration_ms,
        .stable_id = ranked.stable_id,
        .sequence = ranked.origin.sequence,
        .source_id = ranked.origin.source_id,
        .priority = ranked.priority,
        .scale = master_,
        .hd_scale = profile.hd_scale,
        .duration_scale = profile.duration_scale,
        .position_gain = 0.65F,
        .channel_mask = 0x3U,
        .held = ranked.held,
    };
}

void BodyEffects::render(OutputFrame& frame,
    std::span<const HeldBodyEffect> held,
    MonoTime now) const noexcept {
    if (master_ == 0.0F) {
        return;
    }

    RenderState state;
    for (const auto& cue : cues_) {
        if (cue.effect != EffectId::count) {
            mix_cue(state, frame, cue, false, now);
        }
    }
    for (const auto& hold : held) {
        if (hold.effect == EffectId::count) {
            continue;
        }
        const auto& profile = profiles_[profile_index(hold.effect)];
        if (!held_has_onset(hold, profile, now)) {
            mix_cue(state, frame, held_cue(hold, profile), true, now);
        }
    }

    for (std::size_t index = 0; index < state.hd_voices.size(); ++index) {
        const auto& ranked = state.hd_voices[index];
        if (!ranked.profile) {
            break;
        }
        render_voice(frame, ranked, index);
    }
}

} // namespace oblivion_adapter
