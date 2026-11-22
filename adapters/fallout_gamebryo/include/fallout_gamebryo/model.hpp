#pragma once

#include <inputnext_adapters/led_policy.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fallout_gamebryo {
using inputnext::adapters::LedHealthSample;
using inputnext::adapters::LedStealthSample;
using inputnext::adapters::MonoTime;

inline constexpr std::size_t event_capacity = 64;
inline constexpr auto snapshot_timeout = std::chrono::milliseconds{150};
inline constexpr std::uint32_t effect_ttl_ms = 150;

enum class Mode : std::uint8_t { unavailable, gameplay, vats_selection, vats_playback, menu };
enum class Menu : std::uint8_t { none, inventory, lockpick, terminal, repair, crafting, other };
enum class TriggerSide : std::uint8_t { none, left, right };
enum class Gait : std::uint8_t { none, walk, run, sneak };
enum class PlayerReaction : std::uint8_t { steady, staggered, knocked_down };
enum class HitOutcome : std::uint8_t { ordinary, critical, sneak_critical, crippled, blocked };
enum class WeaponClass : std::uint8_t {
    none,
    unarmed,
    melee,
    pistol,
    rifle,
    shotgun,
    heavy,
    energy,
    launcher,
    thrown,
    generic_firearm,
};

[[nodiscard]] constexpr bool is_firearm(WeaponClass kind) noexcept {
    return kind != WeaponClass::none && kind != WeaponClass::unarmed &&
           kind != WeaponClass::melee && kind != WeaponClass::thrown;
}

struct Weapon {
    std::uint32_t id{};
    WeaponClass kind{WeaponClass::none};
    // Report magazine rounds only for the currently equipped firearm when the count is known.
    std::optional<std::uint32_t> loaded_ammo{std::nullopt};
    bool drawn{};
    bool ready{};
    bool automatic{};
};

struct Movement {
    std::uint32_t cell{};
    float height{};
    float x{};
    float y{};
    bool airborne{};
    Gait gait{Gait::none};
};

struct Observation {
    MonoTime at{MonoTime::duration::zero()};
    std::uint64_t epoch{};
    std::uint64_t frame{};
    std::uint32_t player{};
    Mode mode{Mode::unavailable};
    Menu menu{Menu::none};
    Weapon weapon{};
    TriggerSide attack_trigger{TriggerSide::none};
    TriggerSide aim_trigger{TriggerSide::none};
    bool alive{};
    bool focused{};
    bool in_combat{};
    bool aiming{};
    bool reloading{};
    std::optional<LedHealthSample> health{std::nullopt};
    std::optional<LedStealthSample> stealth{std::nullopt};
    std::optional<float> action_points_ratio{std::nullopt};
    std::optional<float> radiation{std::nullopt};
    std::optional<std::uint32_t> radiation_sources{std::nullopt};
    std::optional<Movement> movement{std::nullopt};
    std::optional<PlayerReaction> reaction{std::nullopt};
};

enum class EventKind : std::uint8_t {
    shot,
    dry_fire,
    melee_swing,
    hit,
    explosion,
    explosion_dealt,
    thrown,
    vats_select,
    vats_accept,
    aid,
    equip,
    repair,
    craft,
    pick_lock,
    break_lockpick,
    terminal_success,
    terminal_failure,
    progress,
};

struct Event {
    MonoTime at{MonoTime::duration::zero()};
    std::uint64_t epoch{};
    std::uint64_t frame{};
    // The producer assigns a shared identity to repeated reports of the same action.
    std::uint64_t action{};
    Mode mode{Mode::unavailable};
    Menu menu{Menu::none};
    EventKind kind{EventKind::shot};
    std::uint32_t source{};
    std::uint32_t target{};
    std::uint32_t weapon{};
    WeaponClass weapon_class{WeaponClass::none};
    HitOutcome hit_outcome{HitOutcome::ordinary};
    float strength{1.0F};
    bool automatic{};
    bool confirmed_damage{};
};

[[nodiscard]] constexpr bool menu_allows(Menu menu, EventKind kind) noexcept {
    switch (menu) {
    case Menu::inventory:
        return kind == EventKind::aid || kind == EventKind::equip;
    case Menu::lockpick:
        return kind == EventKind::pick_lock || kind == EventKind::break_lockpick;
    case Menu::terminal:
        return kind == EventKind::terminal_success || kind == EventKind::terminal_failure;
    case Menu::repair:
        return kind == EventKind::repair;
    case Menu::crafting:
        return kind == EventKind::craft;
    case Menu::none:
    case Menu::other:
        return false;
    }
    return false;
}
} // namespace fallout_gamebryo
