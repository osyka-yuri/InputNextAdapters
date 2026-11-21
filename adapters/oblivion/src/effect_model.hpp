#pragma once

#include "model.hpp"

#include <inputnext_adapters/body_program.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace oblivion_adapter {

using inputnext::adapters::MonoTime;

inline constexpr std::size_t effect_inbox_capacity = 64;
inline constexpr auto effect_max_age = std::chrono::milliseconds{150};
inline constexpr float critical_health_exit = 0.25F;

enum class EffectMode : std::uint8_t {
    unavailable,
    loading,
    gameplay,
    menu,
    inventory,
    magic,
    alchemy,
    enchantment,
    spellmaking,
    lockpick,
    training,
    container,
};

enum class Weapon : std::uint8_t {
    unknown,
    unarmed,
    blade_one_hand,
    blade_two_hand,
    blunt_one_hand,
    blunt_two_hand,
    staff,
    bow,
};
enum class BowPhase : std::uint8_t { none, drawing, attached };
enum class MagicKind : std::uint8_t { generic, fire, frost, shock, healing, shield };
enum class CastOrigin : std::uint8_t { spell, scroll, staff, power };
enum class Gait : std::uint8_t { none, walk, run, sneak, mounted_walk, mounted_run };
enum class Ground : std::uint8_t { unknown, grounded, airborne };

struct HorizontalPosition {
    float x{};
    float y{};
    std::uint32_t mover_id{};
};

struct ActionStats {
    std::uint32_t locks_picked{};
    std::uint32_t lockpicks_broken{};
    std::uint32_t training_sessions{};
    std::uint32_t items_pickpocketed{};
    std::uint32_t gates_closed{};
};

struct EffectObservation {
    MonoTime sampled_at;
    std::uint64_t epoch{};
    std::uint64_t frame{};
    EffectMode mode{EffectMode::unavailable};
    bool alive{};
    bool in_combat{};
    PlayerHealth health{};
    std::optional<float> magicka_ratio{std::nullopt};
    std::optional<float> fatigue_ratio{std::nullopt};
    std::uint32_t player_id{};
    std::uint32_t cell_id{};
    std::optional<float> position_z{std::nullopt};
    std::optional<HorizontalPosition> horizontal_position{std::nullopt};
    Weapon weapon{Weapon::unknown};
    std::uint32_t weapon_id{};
    BowPhase bow{BowPhase::none};
    bool melee_attack{};
    bool power_attack{};
    bool blocking{};
    bool shield{};
    bool block_hit{};
    bool casting{};
    std::optional<bool> mounted{std::nullopt};
    bool swimming{};
    Ground ground{Ground::unknown};
    Gait gait{Gait::none};
    std::optional<bool> sneak_detected{std::nullopt};
    std::optional<ActionStats> action_stats{std::nullopt};
};

[[nodiscard]] constexpr BowPhase gameplay_bow_phase(const EffectObservation& sample) noexcept {
    return sample.weapon == Weapon::bow ? sample.bow : BowPhase::none;
}

enum class EffectEventKind : std::uint8_t {
    attack,
    hit,
    hit_with,
    damage,
    bow_draw,
    bow_attach,
    bow_shot,
    cast,
    magic_applied,
    recoil,
    stagger,
    dodge,
    block_impact,
    knockout,
    fall_impact,
    water_dive,
    water_surface,
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
    skill_up,
    quest_complete,
    discovered,
    trained,
    pickpocketed,
    gate_closed,
};

struct EffectEvent {
    EffectEventKind kind{};
    MonoTime at;
    std::uint64_t epoch{};
    std::uint64_t frame{};
    std::uint64_t sequence{};
    EffectMode mode{EffectMode::unavailable};
    std::uint32_t actor_id{};
    std::uint32_t target_id{};
    std::uint32_t source_id{};
    Weapon weapon{Weapon::unknown};
    MagicKind magic{MagicKind::generic};
    CastOrigin cast_origin{CastOrigin::spell};
    float amount{};
    bool melee_confirmed{};
    bool power_attack{};
};

// Admit only confirmed actions in their matching menus.
[[nodiscard]] constexpr bool menu_effect(EffectEventKind kind, EffectMode mode) noexcept {
    switch (kind) {
    case EffectEventKind::drink_potion:
    case EffectEventKind::eat_ingredient:
        return mode == EffectMode::inventory || mode == EffectMode::magic;
    case EffectEventKind::equip:
    case EffectEventKind::unequip:
    case EffectEventKind::drop_item:
        return mode == EffectMode::inventory;
    case EffectEventKind::create_potion:
        return mode == EffectMode::alchemy;
    case EffectEventKind::enchant:
        return mode == EffectMode::enchantment;
    case EffectEventKind::create_spell:
        return mode == EffectMode::spellmaking;
    case EffectEventKind::lock_opened:
    case EffectEventKind::pick_broken:
        return mode == EffectMode::lockpick;
    case EffectEventKind::trained:
        return mode == EffectMode::training;
    case EffectEventKind::pickpocketed:
        return mode == EffectMode::container;
    default:
        return false;
    }
}

} // namespace oblivion_adapter
