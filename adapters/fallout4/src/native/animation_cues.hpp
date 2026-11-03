#pragma once

#include <fallout4_adapter/model.hpp>

#include <optional>
#include <string_view>

namespace fallout4_adapter::native {
struct AnimationCue {
    EventKind kind{};
    bool power_armor_only{};

    [[nodiscard]] constexpr bool allowed(bool in_power_armor) const noexcept {
        return !power_armor_only || in_power_armor;
    }
};

[[nodiscard]] constexpr std::optional<AnimationCue> animation_cue(std::string_view tag) noexcept {
    if (tag == "FootLeft" || tag == "FootRight") {
        return AnimationCue{.kind = EventKind::footstep};
    }
    // Landings use continuously sampled physics for both ordinary and armored jumps.
    // Do not emit a second cue from PowerArmorHardLanding.
    return std::nullopt;
}

} // namespace fallout4_adapter::native
