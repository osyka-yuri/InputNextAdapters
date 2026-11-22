#pragma once

#include <fallout_gamebryo/model.hpp>

#include <inputnext_adapters/audio_haptics.hpp>
#include <inputnext_adapters/led_policy.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace fallout_gamebryo {
enum class BodyEffect : std::uint8_t {
    shot_pistol,
    shot_rifle,
    shot_shotgun,
    shot_heavy,
    shot_energy,
    shot_launcher,
    shot_generic,
    reload_start,
    reload_finish,
    dry_fire,
    unarmed_swing,
    melee_swing,
    melee_hit,
    hit_dealt,
    hit_taken,
    critical_hit,
    sneak_critical,
    crippled_hit,
    block_impact,
    stagger,
    knockdown,
    explosion,
    explosion_dealt,
    throwing,
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
    landing,
    health_warning,
    ap_warning,
    critical_health,
    radiation,
    ordinary_step,
    run_step,
    sneak_step,
    progress,
    count,
};

struct BodyProfile {
    BodyEffect effect{};
    std::string_view section;
    std::string_view description;
    bool enabled_by_default{true};
};

inline constexpr std::array body_profiles{
    BodyProfile{.effect = BodyEffect::shot_pistol,
        .section = "Body.ShotPistol",
        .description = "Pistol shot"},
    BodyProfile{.effect = BodyEffect::shot_rifle,
        .section = "Body.ShotRifle",
        .description = "Rifle shot"},
    BodyProfile{.effect = BodyEffect::shot_shotgun,
        .section = "Body.ShotShotgun",
        .description = "Shotgun shot"},
    BodyProfile{.effect = BodyEffect::shot_heavy,
        .section = "Body.ShotHeavy",
        .description = "Heavy weapon shot"},
    BodyProfile{.effect = BodyEffect::shot_energy,
        .section = "Body.ShotEnergy",
        .description = "Energy weapon shot"},
    BodyProfile{.effect = BodyEffect::shot_launcher,
        .section = "Body.ShotLauncher",
        .description = "Launcher shot"},
    BodyProfile{.effect = BodyEffect::shot_generic,
        .section = "Body.ShotGeneric",
        .description = "Unclassified shot"},
    BodyProfile{.effect = BodyEffect::reload_start,
        .section = "Body.ReloadStart",
        .description = "Reload started"},
    BodyProfile{.effect = BodyEffect::reload_finish,
        .section = "Body.ReloadFinish",
        .description = "Reload finished"},
    BodyProfile{.effect = BodyEffect::dry_fire,
        .section = "Body.DryFire",
        .description = "Empty weapon"},
    BodyProfile{.effect = BodyEffect::unarmed_swing,
        .section = "Body.UnarmedSwing",
        .description = "Unarmed attack"},
    BodyProfile{.effect = BodyEffect::melee_swing,
        .section = "Body.MeleeSwing",
        .description = "Melee attack"},
    BodyProfile{.effect = BodyEffect::melee_hit,
        .section = "Body.MeleeHit",
        .description = "Melee impact"},
    BodyProfile{.effect = BodyEffect::hit_dealt,
        .section = "Body.HitDealt",
        .description = "Outgoing hit"},
    BodyProfile{.effect = BodyEffect::hit_taken,
        .section = "Body.HitTaken",
        .description = "Incoming hit"},
    BodyProfile{.effect = BodyEffect::critical_hit,
        .section = "Body.CriticalHit",
        .description = "Critical hit dealt"},
    BodyProfile{.effect = BodyEffect::sneak_critical,
        .section = "Body.SneakCritical",
        .description = "Sneak critical hit dealt"},
    BodyProfile{.effect = BodyEffect::crippled_hit,
        .section = "Body.CrippledHit",
        .description = "Crippling hit taken"},
    BodyProfile{.effect = BodyEffect::block_impact,
        .section = "Body.BlockImpact",
        .description = "Incoming hit blocked"},
    BodyProfile{.effect = BodyEffect::stagger,
        .section = "Body.Stagger",
        .description = "Player staggered"},
    BodyProfile{.effect = BodyEffect::knockdown,
        .section = "Body.Knockdown",
        .description = "Player knocked down"},
    BodyProfile{.effect = BodyEffect::explosion,
        .section = "Body.Explosion",
        .description = "Explosion"},
    BodyProfile{.effect = BodyEffect::explosion_dealt,
        .section = "Body.ExplosionDealt",
        .description = "Explosion hit dealt"},
    BodyProfile{.effect = BodyEffect::throwing,
        .section = "Body.Throw",
        .description = "Thrown weapon"},
    BodyProfile{.effect = BodyEffect::vats_select,
        .section = "Body.VatsSelect",
        .description = "VATS target selection"},
    BodyProfile{.effect = BodyEffect::vats_accept,
        .section = "Body.VatsAccept",
        .description = "VATS action accepted"},
    BodyProfile{.effect = BodyEffect::aid, .section = "Body.Aid", .description = "Aid item"},
    BodyProfile{.effect = BodyEffect::equip, .section = "Body.Equip", .description = "Equip item"},
    BodyProfile{.effect = BodyEffect::repair,
        .section = "Body.Repair",
        .description = "Item repaired"},
    BodyProfile{.effect = BodyEffect::craft,
        .section = "Body.Craft",
        .description = "Item crafted"},
    BodyProfile{.effect = BodyEffect::pick_lock,
        .section = "Body.PickLock",
        .description = "Lock opened"},
    BodyProfile{.effect = BodyEffect::break_lockpick,
        .section = "Body.BreakLockpick",
        .description = "Lockpick broken"},
    BodyProfile{.effect = BodyEffect::terminal_success,
        .section = "Body.TerminalSuccess",
        .description = "Terminal hacked"},
    BodyProfile{.effect = BodyEffect::terminal_failure,
        .section = "Body.TerminalFailure",
        .description = "Terminal locked after failed attempts"},
    BodyProfile{.effect = BodyEffect::landing, .section = "Body.Landing", .description = "Landing"},
    BodyProfile{.effect = BodyEffect::health_warning,
        .section = "Body.HealthWarning",
        .description = "Low health"},
    BodyProfile{.effect = BodyEffect::ap_warning,
        .section = "Body.ActionPointWarning",
        .description = "Low action points"},
    BodyProfile{.effect = BodyEffect::critical_health,
        .section = "Body.CriticalHealth",
        .description = "Critical health heartbeat"},
    BodyProfile{.effect = BodyEffect::radiation,
        .section = "Body.Radiation",
        .description = "Radiation"},
    BodyProfile{.effect = BodyEffect::ordinary_step,
        .section = "Body.OrdinaryStep",
        .description = "Walking"},
    BodyProfile{.effect = BodyEffect::run_step,
        .section = "Body.RunStep",
        .description = "Running"},
    BodyProfile{.effect = BodyEffect::sneak_step,
        .section = "Body.SneakStep",
        .description = "Sneaking"},
    BodyProfile{.effect = BodyEffect::progress,
        .section = "Body.Progress",
        .description = "Progress notification",
        .enabled_by_default = false},
};

inline constexpr auto body_effect_count = static_cast<std::size_t>(BodyEffect::count);

struct ProfileAdjustment {
    bool enabled{true};
    float strength_scale{1.0F};
    float duration_scale{1.0F};
    std::optional<inputnext::adapters::AudioHapticProfile> audio_haptic;
};

enum class TriggerProfile : std::uint8_t {
    pistol,
    rifle,
    shotgun,
    heavy,
    energy,
    launcher,
    generic_firearm,
    unarmed,
    melee,
    thrown,
    count,
};

struct TriggerProfileDescriptor {
    TriggerProfile profile{};
    std::string_view section;
};

inline constexpr std::array trigger_profiles{
    TriggerProfileDescriptor{.profile = TriggerProfile::pistol, .section = "Trigger.Pistol"},
    TriggerProfileDescriptor{.profile = TriggerProfile::rifle, .section = "Trigger.Rifle"},
    TriggerProfileDescriptor{.profile = TriggerProfile::shotgun, .section = "Trigger.Shotgun"},
    TriggerProfileDescriptor{.profile = TriggerProfile::heavy, .section = "Trigger.Heavy"},
    TriggerProfileDescriptor{.profile = TriggerProfile::energy, .section = "Trigger.Energy"},
    TriggerProfileDescriptor{.profile = TriggerProfile::launcher, .section = "Trigger.Launcher"},
    TriggerProfileDescriptor{.profile = TriggerProfile::generic_firearm,
        .section = "Trigger.GenericFirearm"},
    TriggerProfileDescriptor{.profile = TriggerProfile::unarmed, .section = "Trigger.Unarmed"},
    TriggerProfileDescriptor{.profile = TriggerProfile::melee, .section = "Trigger.Melee"},
    TriggerProfileDescriptor{.profile = TriggerProfile::thrown, .section = "Trigger.Thrown"},
};

inline constexpr auto trigger_profile_count = static_cast<std::size_t>(TriggerProfile::count);

struct TriggerAdjustment {
    bool enabled{true};
    float strength_scale{1.0F};
    float resistance_start{0.48F};
};

struct Settings {
    bool enabled{true};
    bool body_enabled{true};
    bool triggers_enabled{true};
    bool menu_effects{true};
    bool audio_haptics_enabled{true};
    bool resource_cues{true};
    bool ui_cues{true};
    bool gait_cues{};
    bool diagnostics{};
    std::uint32_t controller_slot{};
    TriggerSide attack_trigger{TriggerSide::right};
    TriggerSide aim_trigger{TriggerSide::left};
    float body_scale{0.65F};
    float trigger_scale{0.65F};
    float critical_enter{0.20F};
    float critical_exit{0.25F};
    float health_warning{0.35F};
    float action_point_warning{0.15F};
    float radiation_warning{0.50F};
    // The detection source reports four ordered game states from Hidden through Danger.
    inputnext::adapters::LedSettings led{.detection_alert_score = 3};
    std::array<ProfileAdjustment, body_effect_count> body{};
    std::array<TriggerAdjustment, trigger_profile_count> trigger{};

    Settings() noexcept {
        for (const auto& descriptor : body_profiles) {
            body[static_cast<std::size_t>(descriptor.effect)].enabled =
                descriptor.enabled_by_default;
        }
    }
};

enum class ConfigStatus : std::uint8_t { loaded, missing, invalid, io_error };

struct ConfigError {
    std::size_t line{};
    std::string_view message;
};

enum class AudioHapticOverrideStatus : std::uint8_t {
    not_found,
    loaded,
    invalid,
    wrong_game,
    io_error
};

struct AudioHapticLoadResult {
    AudioHapticOverrideStatus override_status{AudioHapticOverrideStatus::not_found};
    std::size_t loaded_profiles{};
};

[[nodiscard]] ConfigStatus
parse_config(std::string_view text, Settings& settings, ConfigError* error = nullptr) noexcept;
[[nodiscard]] ConfigStatus read_config(const std::filesystem::path& path,
    Settings& settings,
    ConfigError* error = nullptr) noexcept;
[[nodiscard]] std::size_t bind_audio_haptics(const inputnext::adapters::AudioHapticBank& bank,
    Settings& settings,
    inputnext::adapters::AudioHapticGame game) noexcept;
[[nodiscard]] AudioHapticLoadResult load_audio_haptics(const std::filesystem::path& config_path,
    Settings& settings,
    inputnext::adapters::AudioHapticGame game) noexcept;
} // namespace fallout_gamebryo
