#pragma once

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <inputnext_adapters/output_frame.hpp>

#include <cstdint>
#include <optional>

namespace fallout_gamebryo::detail {
[[nodiscard]] constexpr TriggerSide aim_resistance_side(const Observation& observation) noexcept {
    return observation.aiming ? observation.aim_trigger : TriggerSide::none;
}

[[nodiscard]] constexpr TriggerSide attack_resistance_side(
    const Observation& observation) noexcept {
    return observation.weapon.loaded_ammo == 0U ||
                   observation.attack_trigger == aim_resistance_side(observation)
               ? TriggerSide::none
               : observation.attack_trigger;
}

class TriggerEffects final {
public:
    void clear() noexcept;
    void on_shot(const Observation& observation, const Event& shot, WeaponClass weapon) noexcept;
    void write(inputnext::adapters::OutputFrame& frame,
        const Observation& observation,
        const Settings& settings,
        MonoTime now) noexcept;

private:
    struct Recoil {
        MonoTime at;
        std::uint32_t weapon{};
        TriggerSide side{TriggerSide::none};
        TriggerProfile profile{TriggerProfile::generic_firearm};
        float strength{1.0F};
    };

    std::optional<Recoil> recoil_;
};
} // namespace fallout_gamebryo::detail
