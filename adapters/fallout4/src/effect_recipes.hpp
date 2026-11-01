#pragma once

#include "fallout4_adapter/model.hpp"

#include <inputnext_adapters/body_program.hpp>

#include <cstdint>

namespace fallout4_adapter::detail {
using Cue = BodyEffect;

struct HapticRecipe {
    inputnext::adapters::WaveRecipe wave{};
    std::uint16_t duration_ms{};
    int priority{};
    float fallback_low{};
    float fallback_high{};
};

struct TriggerRecipe {
    float resistance_start{};
    float strength{};
    float secondary_start{};
    float secondary_strength{};
};

struct ShotTriggerRecipe {
    float start{};
    float strength{};
    float device_frequency{};
    std::uint16_t duration_ms{};
};

[[nodiscard]] HapticRecipe recipe_for(Cue cue) noexcept;
[[nodiscard]] TriggerRecipe trigger_recipe_for(WeaponFamily weapon_family) noexcept;
[[nodiscard]] ShotTriggerRecipe shot_trigger_recipe_for(WeaponFamily weapon_family,
    bool automatic) noexcept;
[[nodiscard]] float envelope_gain(const inputnext::adapters::WaveRecipe& wave,
    std::uint32_t duration_ms,
    float elapsed_ms,
    float duration_scale = 1.0F) noexcept;
} // namespace fallout4_adapter::detail
