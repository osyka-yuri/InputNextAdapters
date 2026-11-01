#pragma once

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/led_policy.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace fallout4_adapter {

enum class BodyEffect : std::uint8_t {
    pistol_shot,
    rifle_shot,
    shotgun_shot,
    heavy_shot,
    energy_shot,
    generic_shot,
    incoming_hit,
    blocked_hit,
    explosive_hit,
    crippled_hit,
    outgoing_hit,
    critical_hit,
    power_attack,
    bash,
    footstep,
    landing,
    vats_enter,
    vats_exit,
    health_warning,
    ap_warning,
    critical_ready,
    reload_start,
    power_armor_enter,
    power_armor_exit,
    melee_hit,
    rotary_shot,
    flame_shot,
    cryo_shot,
    launcher_shot,
    ordinary_step,
    sprint_step,
    count,
};

struct BodyEffectDescriptor {
    BodyEffect effect{};
    std::string_view section;
};

inline constexpr std::array body_effect_descriptors{
    BodyEffectDescriptor{.effect = BodyEffect::pistol_shot, .section = "Body.PistolShot"},
    BodyEffectDescriptor{.effect = BodyEffect::rifle_shot, .section = "Body.RifleShot"},
    BodyEffectDescriptor{.effect = BodyEffect::shotgun_shot, .section = "Body.ShotgunShot"},
    BodyEffectDescriptor{.effect = BodyEffect::heavy_shot, .section = "Body.HeavyShot"},
    BodyEffectDescriptor{.effect = BodyEffect::energy_shot, .section = "Body.EnergyShot"},
    BodyEffectDescriptor{.effect = BodyEffect::generic_shot, .section = "Body.GenericShot"},
    BodyEffectDescriptor{.effect = BodyEffect::incoming_hit, .section = "Body.IncomingHit"},
    BodyEffectDescriptor{.effect = BodyEffect::blocked_hit, .section = "Body.BlockedHit"},
    BodyEffectDescriptor{.effect = BodyEffect::explosive_hit, .section = "Body.ExplosiveHit"},
    BodyEffectDescriptor{.effect = BodyEffect::crippled_hit, .section = "Body.CrippledHit"},
    BodyEffectDescriptor{.effect = BodyEffect::outgoing_hit, .section = "Body.OutgoingHit"},
    BodyEffectDescriptor{.effect = BodyEffect::critical_hit, .section = "Body.CriticalHit"},
    BodyEffectDescriptor{.effect = BodyEffect::power_attack, .section = "Body.PowerAttack"},
    BodyEffectDescriptor{.effect = BodyEffect::bash, .section = "Body.Bash"},
    BodyEffectDescriptor{.effect = BodyEffect::footstep, .section = "Body.PowerArmorStep"},
    BodyEffectDescriptor{.effect = BodyEffect::landing, .section = "Body.Landing"},
    BodyEffectDescriptor{.effect = BodyEffect::vats_enter, .section = "Body.VatsEnter"},
    BodyEffectDescriptor{.effect = BodyEffect::vats_exit, .section = "Body.VatsExit"},
    BodyEffectDescriptor{.effect = BodyEffect::health_warning, .section = "Body.HealthWarning"},
    BodyEffectDescriptor{.effect = BodyEffect::ap_warning, .section = "Body.ActionPointWarning"},
    BodyEffectDescriptor{.effect = BodyEffect::critical_ready, .section = "Body.CriticalReady"},
    BodyEffectDescriptor{.effect = BodyEffect::reload_start, .section = "Body.ReloadStart"},
    BodyEffectDescriptor{.effect = BodyEffect::power_armor_enter,
        .section = "Body.PowerArmorEnter"},
    BodyEffectDescriptor{.effect = BodyEffect::power_armor_exit, .section = "Body.PowerArmorExit"},
    BodyEffectDescriptor{.effect = BodyEffect::melee_hit, .section = "Body.MeleeHit"},
    BodyEffectDescriptor{.effect = BodyEffect::rotary_shot, .section = "Body.RotaryShot"},
    BodyEffectDescriptor{.effect = BodyEffect::flame_shot, .section = "Body.FlameShot"},
    BodyEffectDescriptor{.effect = BodyEffect::cryo_shot, .section = "Body.CryoShot"},
    BodyEffectDescriptor{.effect = BodyEffect::launcher_shot, .section = "Body.LauncherShot"},
    BodyEffectDescriptor{.effect = BodyEffect::ordinary_step, .section = "Body.OrdinaryStep"},
    BodyEffectDescriptor{.effect = BodyEffect::sprint_step, .section = "Body.SprintStep"},
};

inline constexpr auto body_effect_count = static_cast<std::size_t>(BodyEffect::count);

struct BodyAdjustment {
    bool enabled{true};
    float hd_scale{1.0F};
    float classic_scale{1.0F};
    float duration_scale{1.0F};
    std::optional<float> frequency_hz;
    std::optional<inputnext::adapters::AudioHapticProfile> audio_haptic;
};

enum class TriggerProfile : std::uint8_t {
    pistol,
    rifle,
    shotgun,
    heavy,
    energy,
    generic_firearm,
    unarmed,
    melee,
    two_handed_melee,
    rotary,
    flame,
    cryo,
    launcher,
    count,
};

struct TriggerProfileDescriptor {
    TriggerProfile profile{};
    std::string_view section;
};

inline constexpr std::array trigger_profile_descriptors{
    TriggerProfileDescriptor{.profile = TriggerProfile::pistol, .section = "Trigger.Weapon.Pistol"},
    TriggerProfileDescriptor{.profile = TriggerProfile::rifle, .section = "Trigger.Weapon.Rifle"},
    TriggerProfileDescriptor{.profile = TriggerProfile::shotgun,
        .section = "Trigger.Weapon.Shotgun"},
    TriggerProfileDescriptor{.profile = TriggerProfile::heavy, .section = "Trigger.Weapon.Heavy"},
    TriggerProfileDescriptor{.profile = TriggerProfile::energy, .section = "Trigger.Weapon.Energy"},
    TriggerProfileDescriptor{.profile = TriggerProfile::generic_firearm,
        .section = "Trigger.Weapon.GenericFirearm"},
    TriggerProfileDescriptor{.profile = TriggerProfile::unarmed,
        .section = "Trigger.Weapon.Unarmed"},
    TriggerProfileDescriptor{.profile = TriggerProfile::melee, .section = "Trigger.Weapon.Melee"},
    TriggerProfileDescriptor{.profile = TriggerProfile::two_handed_melee,
        .section = "Trigger.Weapon.TwoHandedMelee"},
    TriggerProfileDescriptor{.profile = TriggerProfile::rotary, .section = "Trigger.Weapon.Rotary"},
    TriggerProfileDescriptor{.profile = TriggerProfile::flame, .section = "Trigger.Weapon.Flame"},
    TriggerProfileDescriptor{.profile = TriggerProfile::cryo, .section = "Trigger.Weapon.Cryo"},
    TriggerProfileDescriptor{.profile = TriggerProfile::launcher,
        .section = "Trigger.Weapon.Launcher"},
};

inline constexpr auto trigger_profile_count = static_cast<std::size_t>(TriggerProfile::count);

struct TriggerAdjustment {
    bool enabled{true};
    float strength_scale{1.0F};
    std::optional<float> resistance_start;
    std::optional<std::uint16_t> vibration_frequency_code;
};

[[nodiscard]] constexpr auto default_body_adjustments() noexcept {
    std::array<BodyAdjustment, body_effect_count> adjustments{};
    adjustments[static_cast<std::size_t>(BodyEffect::ordinary_step)].enabled = false;
    return adjustments;
}

struct EffectSettings {
    bool body_enabled{true};
    bool audio_haptics_enabled{true};
    bool triggers_enabled{true};
    inputnext::adapters::LedSettings leds{};
    std::array<BodyAdjustment, body_effect_count> body{default_body_adjustments()};
    std::array<TriggerAdjustment, trigger_profile_count> triggers{};
};

} // namespace fallout4_adapter
