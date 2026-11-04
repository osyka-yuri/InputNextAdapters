#pragma once

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/led_policy.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace skyrim_adapter {
enum class EffectId : std::size_t {
    unarmed_swing,
    dagger_swing,
    sword_swing,
    axe_swing,
    mace_swing,
    two_handed_swing,
    unarmed_hit,
    dagger_hit,
    sword_hit,
    axe_hit,
    mace_hit,
    two_handed_hit,
    incoming_hit,
    shield_hit,
    bow_draw,
    bow_full_draw,
    bow_release,
    bow_tension_draw,
    bow_tension_attached,
    bow_tension_drawn,
    crossbow_shot,
    fire_release,
    frost_release,
    shock_release,
    healing_release,
    ward_release,
    generic_release,
    fire_held,
    frost_held,
    shock_held,
    healing_held,
    ward_held,
    generic_held,
    voice,
    critical_health,
    stamina_depleted,
    stagger,
    knockdown,
    killmove,
    landing,
    survival_cold,
    survival_hunger,
    survival_exhaustion,
    werewolf,
    vampire_lord,
    footstep_walk,
    footstep_run,
    footstep_sprint,
    footstep_sneak,
    mounted_walk,
    mounted_run,
    mounted_sprint,
    count,
};

struct EffectDescriptor {
    EffectId id;
    std::string_view section;
    bool transient;
    bool classic{true};
};

inline constexpr std::array<EffectDescriptor, static_cast<std::size_t>(EffectId::count)>
    effect_descriptors{
        {
            {.id = EffectId::unarmed_swing,
                .section = "Body.Melee.Unarmed.Swing",
                .transient = true},
            {.id = EffectId::dagger_swing, .section = "Body.Melee.Dagger.Swing", .transient = true},
            {.id = EffectId::sword_swing, .section = "Body.Melee.Sword.Swing", .transient = true},
            {.id = EffectId::axe_swing, .section = "Body.Melee.Axe.Swing", .transient = true},
            {.id = EffectId::mace_swing, .section = "Body.Melee.Mace.Swing", .transient = true},
            {.id = EffectId::two_handed_swing,
                .section = "Body.Melee.TwoHanded.Swing",
                .transient = true},
            {.id = EffectId::unarmed_hit, .section = "Body.Melee.Unarmed.Hit", .transient = true},
            {.id = EffectId::dagger_hit, .section = "Body.Melee.Dagger.Hit", .transient = true},
            {.id = EffectId::sword_hit, .section = "Body.Melee.Sword.Hit", .transient = true},
            {.id = EffectId::axe_hit, .section = "Body.Melee.Axe.Hit", .transient = true},
            {.id = EffectId::mace_hit, .section = "Body.Melee.Mace.Hit", .transient = true},
            {.id = EffectId::two_handed_hit,
                .section = "Body.Melee.TwoHanded.Hit",
                .transient = true},
            {.id = EffectId::incoming_hit, .section = "Body.IncomingHit", .transient = true},
            {.id = EffectId::shield_hit, .section = "Body.ShieldHit", .transient = true},
            {.id = EffectId::bow_draw, .section = "Body.Bow.Draw", .transient = true},
            {.id = EffectId::bow_full_draw, .section = "Body.Bow.FullDraw", .transient = true},
            {.id = EffectId::bow_release, .section = "Body.Bow.Release", .transient = true},
            {.id = EffectId::bow_tension_draw,
                .section = "Body.Bow.TensionDraw",
                .transient = false},
            {.id = EffectId::bow_tension_attached,
                .section = "Body.Bow.TensionAttached",
                .transient = false},
            {.id = EffectId::bow_tension_drawn,
                .section = "Body.Bow.TensionDrawn",
                .transient = false,
                .classic = false},
            {.id = EffectId::crossbow_shot, .section = "Body.Crossbow.Shot", .transient = true},
            {.id = EffectId::fire_release, .section = "Body.Magic.Fire.Release", .transient = true},
            {.id = EffectId::frost_release,
                .section = "Body.Magic.Frost.Release",
                .transient = true},
            {.id = EffectId::shock_release,
                .section = "Body.Magic.Shock.Release",
                .transient = true},
            {.id = EffectId::healing_release,
                .section = "Body.Magic.Healing.Release",
                .transient = true},
            {.id = EffectId::ward_release, .section = "Body.Magic.Ward.Release", .transient = true},
            {.id = EffectId::generic_release,
                .section = "Body.Magic.Generic.Release",
                .transient = true},
            {.id = EffectId::fire_held,
                .section = "Body.Magic.Fire.Concentration",
                .transient = false},
            {.id = EffectId::frost_held,
                .section = "Body.Magic.Frost.Concentration",
                .transient = false},
            {.id = EffectId::shock_held,
                .section = "Body.Magic.Shock.Concentration",
                .transient = false},
            {.id = EffectId::healing_held,
                .section = "Body.Magic.Healing.Concentration",
                .transient = false},
            {.id = EffectId::ward_held,
                .section = "Body.Magic.Ward.Concentration",
                .transient = false},
            {
                .id = EffectId::generic_held,
                .section = "Body.Magic.Generic.Concentration",
                .transient = false,
                .classic = false,
            },
            {.id = EffectId::voice, .section = "Body.Voice", .transient = true},
            {.id = EffectId::critical_health, .section = "Body.CriticalHealth", .transient = true},
            {.id = EffectId::stamina_depleted,
                .section = "Body.World.StaminaDepleted",
                .transient = true},
            {.id = EffectId::stagger, .section = "Body.World.Stagger", .transient = true},
            {.id = EffectId::knockdown, .section = "Body.World.Knockdown", .transient = true},
            {.id = EffectId::killmove, .section = "Body.World.Killmove", .transient = true},
            {.id = EffectId::landing, .section = "Body.World.Landing", .transient = true},
            {.id = EffectId::survival_cold, .section = "Body.Survival.Cold", .transient = true},
            {.id = EffectId::survival_hunger, .section = "Body.Survival.Hunger", .transient = true},
            {.id = EffectId::survival_exhaustion,
                .section = "Body.Survival.Exhaustion",
                .transient = true},
            {.id = EffectId::werewolf,
                .section = "Body.Transformation.Werewolf",
                .transient = true},
            {.id = EffectId::vampire_lord,
                .section = "Body.Transformation.VampireLord",
                .transient = true},
            {.id = EffectId::footstep_walk, .section = "Body.Footstep.Walk", .transient = true},
            {.id = EffectId::footstep_run, .section = "Body.Footstep.Run", .transient = true},
            {.id = EffectId::footstep_sprint, .section = "Body.Footstep.Sprint", .transient = true},
            {.id = EffectId::footstep_sneak, .section = "Body.Footstep.Sneak", .transient = true},
            {.id = EffectId::mounted_walk, .section = "Body.Mounted.Walk", .transient = true},
            {.id = EffectId::mounted_run, .section = "Body.Mounted.Run", .transient = true},
            {.id = EffectId::mounted_sprint, .section = "Body.Mounted.Sprint", .transient = true},
        },
};

struct EffectAdjustment {
    bool enabled{true};
    float hd_scale{1.0F};
    float classic_scale{1.0F};
    float duration_scale{1.0F};
    float frequency_hz{};
    std::optional<inputnext::adapters::AudioHapticProfile> audio_haptic;
};

inline constexpr auto default_body_adjustments = [] {
    std::array<EffectAdjustment, static_cast<std::size_t>(EffectId::count)> adjustments{};
    adjustments[static_cast<std::size_t>(EffectId::footstep_walk)].enabled = false;
    adjustments[static_cast<std::size_t>(EffectId::footstep_run)].enabled = false;
    adjustments[static_cast<std::size_t>(EffectId::footstep_sneak)].enabled = false;
    adjustments[static_cast<std::size_t>(EffectId::mounted_walk)].enabled = false;
    adjustments[static_cast<std::size_t>(EffectId::mounted_run)].enabled = false;
    return adjustments;
}();

enum class TriggerId : std::size_t {
    bow_draw,
    weapon_block,
    shield_block,
    shield_hit,
    magic_charge,
    fire,
    frost,
    shock,
    healing,
    ward,
    generic,
    count,
};

struct TriggerDescriptor {
    TriggerId id;
    std::string_view section;
    bool vibration;
};

inline constexpr std::array<TriggerDescriptor, static_cast<std::size_t>(TriggerId::count)>
    trigger_descriptors{
        {
            {.id = TriggerId::bow_draw, .section = "Trigger.BowDraw", .vibration = false},
            {.id = TriggerId::weapon_block, .section = "Trigger.WeaponBlock", .vibration = false},
            {.id = TriggerId::shield_block, .section = "Trigger.ShieldBlock", .vibration = false},
            {.id = TriggerId::shield_hit, .section = "Trigger.ShieldHit", .vibration = false},
            {.id = TriggerId::magic_charge, .section = "Trigger.MagicCharge", .vibration = false},
            {.id = TriggerId::fire, .section = "Trigger.Fire", .vibration = true},
            {.id = TriggerId::frost, .section = "Trigger.Frost", .vibration = true},
            {.id = TriggerId::shock, .section = "Trigger.Shock", .vibration = true},
            {.id = TriggerId::healing, .section = "Trigger.Healing", .vibration = false},
            {.id = TriggerId::ward, .section = "Trigger.Ward", .vibration = false},
            {.id = TriggerId::generic, .section = "Trigger.Generic", .vibration = false},
        },
};

struct TriggerAdjustment {
    bool enabled{true};
    float strength_scale{1.0F};
    std::optional<float> start;
    std::optional<unsigned> frequency_code;
};

struct EffectSettings {
    bool body_enabled{true};
    bool audio_haptics_enabled{true};
    bool triggers_enabled{true};
    bool respect_game_vibration{true};
    inputnext::adapters::LedSettings leds{};
    std::array<EffectAdjustment, static_cast<std::size_t>(EffectId::count)> body{
        default_body_adjustments};
    std::array<TriggerAdjustment, static_cast<std::size_t>(TriggerId::count)> triggers{};
};
} // namespace skyrim_adapter
