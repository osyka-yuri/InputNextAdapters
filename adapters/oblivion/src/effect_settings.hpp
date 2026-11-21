#pragma once

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/led_policy.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace oblivion_adapter {

enum class EffectId : std::uint8_t {
    unarmed_swing,
    blade_one_hand_swing,
    blade_two_hand_swing,
    blunt_one_hand_swing,
    blunt_two_hand_swing,
    unarmed_hit,
    blade_one_hand_hit,
    blade_two_hand_hit,
    blunt_one_hand_hit,
    blunt_two_hand_hit,
    generic_hit,
    incoming_damage,
    block_hit,
    recoil,
    stagger,
    dodge,
    knockout,
    bow_draw,
    bow_attached,
    bow_shot,
    bow_hit,
    casting,
    fire,
    frost,
    shock,
    healing,
    shield,
    generic_magic,
    magic_applied,
    heartbeat,
    magicka_depleted,
    fatigue_depleted,
    landing,
    water_dive,
    water_surface,
    mount,
    dismount,
    walk,
    run,
    sneak,
    mounted_walk,
    mounted_run,
    drink_potion,
    eat_ingredient,
    equip,
    unequip,
    drop_item,
    create_potion,
    enchant,
    create_spell,
    lock_opened,
    pick_broken,
    vampire_feed,
    soul_trap,
    pickpocketed,
    skill_up,
    quest_complete,
    discovered,
    trained,
    gate_closed,
    count,
};

[[nodiscard]] constexpr std::size_t profile_index(EffectId effect) noexcept {
    return static_cast<std::size_t>(effect);
}

inline constexpr auto effect_sections = std::to_array<std::string_view>({
    "Body.Melee.Unarmed.Swing",
    "Body.Melee.BladeOneHand.Swing",
    "Body.Melee.BladeTwoHand.Swing",
    "Body.Melee.BluntOneHand.Swing",
    "Body.Melee.BluntTwoHand.Swing",
    "Body.Melee.Unarmed.Hit",
    "Body.Melee.BladeOneHand.Hit",
    "Body.Melee.BladeTwoHand.Hit",
    "Body.Melee.BluntOneHand.Hit",
    "Body.Melee.BluntTwoHand.Hit",
    "Body.Melee.Generic.Hit",
    "Body.Combat.IncomingDamage",
    "Body.Combat.BlockHit",
    "Body.Combat.Recoil",
    "Body.Combat.Stagger",
    "Body.Combat.Dodge",
    "Body.Combat.Knockout",
    "Body.Bow.Draw",
    "Body.Bow.Attached",
    "Body.Bow.Shot",
    "Body.Bow.Hit",
    "Body.Magic.Casting",
    "Body.Magic.Fire",
    "Body.Magic.Frost",
    "Body.Magic.Shock",
    "Body.Magic.Healing",
    "Body.Magic.Shield",
    "Body.Magic.Generic",
    "Body.Magic.Applied",
    "Body.Health.Heartbeat",
    "Body.Resource.MagickaDepleted",
    "Body.Resource.FatigueDepleted",
    "Body.World.Landing",
    "Body.World.WaterDive",
    "Body.World.WaterSurface",
    "Body.World.Mount",
    "Body.World.Dismount",
    "Body.World.Walk",
    "Body.World.Run",
    "Body.World.Sneak",
    "Body.World.MountedWalk",
    "Body.World.MountedRun",
    "Body.Action.DrinkPotion",
    "Body.Action.EatIngredient",
    "Body.Action.Equip",
    "Body.Action.Unequip",
    "Body.Action.DropItem",
    "Body.Action.CreatePotion",
    "Body.Action.Enchant",
    "Body.Action.CreateSpell",
    "Body.Action.LockOpened",
    "Body.Action.PickBroken",
    "Body.Action.VampireFeed",
    "Body.Action.SoulTrap",
    "Body.Action.Pickpocket",
    "Body.Action.SkillUp",
    "Body.Action.QuestComplete",
    "Body.Action.Discovered",
    "Body.Action.Training",
    "Body.Action.GateClosed",
});
static_assert(effect_sections.size() == profile_index(EffectId::count));

struct ProfileAdjustment {
    bool enabled{true};
    float hd_scale{1.0F};
    float classic_scale{1.0F};
    float duration_scale{1.0F};
    std::optional<float> frequency_hz;
    std::shared_ptr<const inputnext::adapters::AudioHapticProfile> audio;
};

struct EffectSettings {
    EffectSettings() noexcept {
        for (auto index = profile_index(EffectId::walk);
            index <= profile_index(EffectId::mounted_run);
            ++index) {
            profiles[index].enabled = false;
        }
        for (auto index = profile_index(EffectId::skill_up); index < profiles.size(); ++index) {
            profiles[index].enabled = false;
        }
    }

    std::uint32_t controller_slot{};
    float rumble_scale{1.0F};
    float trigger_scale{1.0F};
    bool body_enabled{true};
    bool triggers_enabled{true};
    bool menu_effects_enabled{true};
    bool audio_haptics_enabled{true};
    inputnext::adapters::LedSettings led{.radiation_enabled = false};
    std::array<ProfileAdjustment, effect_sections.size()> profiles;
};

} // namespace oblivion_adapter
