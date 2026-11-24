#pragma once

#include <fallout4_adapter/model.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace fallout4_adapter::native {
[[nodiscard]] inline ContactSurface projectile_surface(std::uint32_t projectile_form_id,
    std::string_view material) noexcept {
    if (projectile_form_id == 0 || material.empty()) {
        return ContactSurface::unknown;
    }

    using namespace std::string_view_literals;
    // MNAM values from Fallout4.esm MATT records, including variants of the base surfaces.
    constexpr std::array metal{"Metal"sv,
        "MetalSolid"sv,
        "MetalHeavy"sv,
        "MetalLight"sv,
        "MetalHollow"sv,
        "MetalBarrel"sv,
        "MetalStairs"sv,
        "MaterialMetalStairs"sv,
        "MaterialGroundMetalA"sv,
        "MaterialMetalAuto"sv};
    constexpr std::array stone{"Stone"sv,
        "Concrete"sv,
        "ConcreteStairs"sv,
        "Brick"sv,
        "StoneBroken"sv,
        "StoneHeavy"sv,
        "StoneStairs"sv,
        "StoneAsStairs"sv,
        "StoneBoulderSmall"sv,
        "StoneBoulderMedium"sv,
        "StoneBoulderLarge"sv};
    constexpr std::array wood{"Wood"sv,
        "WoodBarrel"sv,
        "WoodHeavy"sv,
        "WoodLight"sv,
        "WoodStairs"sv,
        "WoodAsStairs"sv,
        "MaterialWoodBoard"sv,
        "MaterialWoodCrate"sv};
    constexpr std::array glass{"Glass"sv, "GlassStairs"sv, "Bottle"sv, "BottleSmall"sv};

    if (std::ranges::contains(metal, material)) {
        return ContactSurface::metal;
    }
    if (std::ranges::contains(stone, material)) {
        return ContactSurface::stone;
    }
    if (std::ranges::contains(wood, material)) {
        return ContactSurface::wood;
    }
    return std::ranges::contains(glass, material) ? ContactSurface::glass : ContactSurface::unknown;
}
} // namespace fallout4_adapter::native
