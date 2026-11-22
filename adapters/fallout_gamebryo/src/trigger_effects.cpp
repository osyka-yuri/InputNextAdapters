#include <fallout_gamebryo/detail/trigger_effects.hpp>
#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <input_next_effects.h>
#include <inputnext_adapters/output_frame.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fallout_gamebryo::detail {
namespace {
using inputnext::adapters::TriggerIntent;

struct RecoilProfile {
    float frequency;
    std::uint32_t duration_ms;
};

// Entries follow TriggerProfile order.
constexpr std::array recoil_profiles{
    RecoilProfile{.frequency = 94.0F, .duration_ms = 38U},
    RecoilProfile{.frequency = 68.0F, .duration_ms = 42U},
    RecoilProfile{.frequency = 42.0F, .duration_ms = 48U},
    RecoilProfile{.frequency = 36.0F, .duration_ms = 48U},
    RecoilProfile{.frequency = 108.0F, .duration_ms = 34U},
    RecoilProfile{.frequency = 30.0F, .duration_ms = 60U},
    RecoilProfile{.frequency = 70.0F, .duration_ms = 40U},
    RecoilProfile{.frequency = 76.0F, .duration_ms = 46U},
    RecoilProfile{.frequency = 62.0F, .duration_ms = 48U},
    RecoilProfile{.frequency = 56.0F, .duration_ms = 42U},
};
static_assert(recoil_profiles.size() == trigger_profile_count);

[[nodiscard]] std::optional<TriggerProfile> trigger_for(WeaponClass weapon) noexcept {
    switch (weapon) {
    case WeaponClass::pistol:
        return TriggerProfile::pistol;
    case WeaponClass::rifle:
        return TriggerProfile::rifle;
    case WeaponClass::shotgun:
        return TriggerProfile::shotgun;
    case WeaponClass::heavy:
        return TriggerProfile::heavy;
    case WeaponClass::energy:
        return TriggerProfile::energy;
    case WeaponClass::launcher:
        return TriggerProfile::launcher;
    case WeaponClass::generic_firearm:
        return TriggerProfile::generic_firearm;
    case WeaponClass::unarmed:
        return TriggerProfile::unarmed;
    case WeaponClass::melee:
        return TriggerProfile::melee;
    case WeaponClass::thrown:
        return TriggerProfile::thrown;
    case WeaponClass::none:
        return std::nullopt;
    }
    return std::nullopt;
}

bool trigger_ready(const Observation& observation) noexcept {
    return observation.mode == Mode::gameplay && observation.weapon.ready &&
           observation.weapon.drawn && !observation.reloading;
}

void set_trigger(inputnext::adapters::OutputFrame& frame,
    TriggerSide side,
    const TriggerIntent& intent) noexcept {
    switch (side) {
    case TriggerSide::left:
        frame.left = intent;
        break;
    case TriggerSide::right:
        frame.right = intent;
        break;
    case TriggerSide::none:
        break;
    }
}

void write_resistance(inputnext::adapters::OutputFrame& frame,
    const Observation& observation,
    const Settings& settings) noexcept {
    const auto profile = trigger_for(observation.weapon.kind);
    if (!profile) {
        return;
    }
    const auto& adjustment = settings.trigger[static_cast<std::size_t>(*profile)];
    if (!adjustment.enabled) {
        return;
    }
    const auto make_resistance = [&](float scale) {
        TriggerIntent intent{};
        intent.payload.mode = INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK;
        intent.payload.start = std::clamp(adjustment.resistance_start, 0.0F, 1.0F);
        intent.payload.end = 1.0F;
        intent.payload.strength =
            std::clamp(0.30F * adjustment.strength_scale * settings.trigger_scale * scale,
                0.0F,
                0.65F);
        intent.ttl_ms =
            std::clamp(static_cast<std::uint32_t>(snapshot_timeout.count()), 1U, effect_ttl_ms);
        return intent;
    };
    const auto attack = attack_resistance_side(observation);
    if (attack != TriggerSide::none) {
        set_trigger(frame, attack, make_resistance(observation.weapon.automatic ? 0.82F : 1.0F));
    }
    const auto aim = aim_resistance_side(observation);
    if (aim != TriggerSide::none) {
        set_trigger(frame, aim, make_resistance(1.0F));
    }
}
} // namespace

void TriggerEffects::clear() noexcept {
    recoil_.reset();
}

void TriggerEffects::on_shot(const Observation& observation,
    const Event& shot,
    WeaponClass weapon) noexcept {
    if (!trigger_ready(observation) || observation.attack_trigger == TriggerSide::none) {
        return;
    }
    if (const auto profile = trigger_for(weapon)) {
        // At the default scale, automatic recoil is one DualSense strength level higher.
        const float recoil_gain = shot.automatic ? 0.49F : 0.42F;
        recoil_ = Recoil{.at = shot.at,
            .weapon = observation.weapon.id,
            .side = observation.attack_trigger,
            .profile = *profile,
            .strength = shot.strength * recoil_gain};
    }
}

void TriggerEffects::write(inputnext::adapters::OutputFrame& frame,
    const Observation& observation,
    const Settings& settings,
    MonoTime now) noexcept {
    if (!settings.enabled || !settings.triggers_enabled || !trigger_ready(observation)) {
        clear();
        return;
    }
    write_resistance(frame, observation, settings);
    if (!recoil_) {
        return;
    }
    const auto& recoil = *recoil_;
    const auto index = static_cast<std::size_t>(recoil.profile);
    const auto& adjustment = settings.trigger[index];
    const auto& profile = recoil_profiles[index];
    const auto expires = recoil.at + std::chrono::milliseconds{profile.duration_ms};
    if (observation.weapon.id != recoil.weapon || observation.attack_trigger != recoil.side ||
        !adjustment.enabled || now < recoil.at || now >= expires) {
        clear();
        return;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(expires - now);
    if (remaining.count() <= 0) {
        clear();
        return;
    }
    TriggerIntent pulse{};
    pulse.payload.mode = INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION;
    pulse.payload.start = std::clamp(adjustment.resistance_start, 0.0F, 1.0F);
    pulse.payload.end = 1.0F;
    pulse.payload.strength =
        std::clamp(recoil.strength * adjustment.strength_scale * settings.trigger_scale,
            0.0F,
            0.44F);
    pulse.payload.frequency = profile.frequency;
    pulse.ttl_ms = static_cast<std::uint32_t>(remaining.count());
    set_trigger(frame, recoil.side, pulse);
}
} // namespace fallout_gamebryo::detail
