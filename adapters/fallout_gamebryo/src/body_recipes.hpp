#pragma once

#include <fallout_gamebryo/settings.hpp>

#include <inputnext_adapters/body_program.hpp>

#include <cstdint>

namespace fallout_gamebryo::detail {
struct Recipe {
    inputnext::adapters::WaveRecipe wave{};
    std::uint32_t duration_ms{};
    int priority{};
    float fallback_low{};
    float fallback_high{};
};

[[nodiscard]] Recipe recipe_for(BodyEffect effect) noexcept;
} // namespace fallout_gamebryo::detail
