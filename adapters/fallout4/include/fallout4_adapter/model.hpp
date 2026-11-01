#pragma once

#include <fallout4_adapter/effect_settings.hpp>
#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <chrono>
#include <cstdint>
#include <optional>

namespace fallout4_adapter {
using inputnext::adapters::MonoTime;
using inputnext::adapters::OutputFrame;

enum class Mode : std::uint8_t {
    suspended,
    gameplay,
    vats_selection,
    vats_playback,
    transition,
    menu
};
enum class TriggerSide : std::uint8_t { none, left, right };
enum class WeaponFamily : std::uint8_t {
    none,
    unarmed,
    melee,
    pistol,
    rifle,
    shotgun,
    heavy,
    energy,
    two_handed_melee,
    generic_firearm,
    rotary,
    flame,
    cryo,
    launcher,
};

struct WeaponSnapshot {
    // Zero means that the native layer could not identify a current weapon instance.
    std::uint64_t identity{};
    WeaponFamily family{WeaponFamily::none};
    bool automatic{};
    bool drawn{};
    bool ready{};
};

// Value-only physics facts captured in the existing game-thread observation.
struct LandingObservation {
    std::uint32_t player_id{};
    std::uint32_t cell_id{};
    float x{}, y{}, z{};
    bool airborne{};
};

struct Observation {
    MonoTime sampled_at;
    inputnext::adapters::LedObservation led{};
    Mode mode{Mode::suspended};
    WeaponSnapshot weapon{};
    TriggerSide attack_trigger{TriggerSide::none};
    // Fallout 4's mapped SecondaryAttack action, used to aim or block.
    TriggerSide aim_trigger{TriggerSide::none};
    bool aiming{};
    bool reloading{};
    bool blocking{};
    bool power_armor{};
    bool sprinting{};
    std::optional<float> health_ratio;
    std::optional<float> action_points_ratio;
    std::optional<std::uint8_t> critical_count;
    std::optional<LandingObservation> landing;
};

enum class EventKind : std::uint8_t { shot, hit_received, hit_dealt, footstep };

struct PolicyEvent {
    EventKind kind{EventKind::shot};
    MonoTime arrived_at;
    std::uint64_t sequence{};
    std::uint64_t weapon_identity{};
    // The native layer supplies a finite normalized intensity in [0, 1]; this is never raw damage.
    float magnitude{1.0F};
    // True for finite nonnegative native healthDamage, including a known zero.
    bool health_damage_known{};
    // Set only from finite positive TESHitEvent healthDamage for an incoming hit.
    bool confirmed_positive_health_damage{};
    bool critical{};
    bool blocked{};
    bool power_attack{};
    bool bash{};
    bool explosion{};
    bool crippled_limb{};
    bool melee{};
    bool power_armor{};
    bool sprinting{};
};

struct PolicyConfig {
    bool enabled{true};
    float trigger_scale{0.65F};
    float body_scale{0.65F};
    bool resource_cues{true};
    bool ui_cues{};
    EffectSettings effects{};
};
} // namespace fallout4_adapter
