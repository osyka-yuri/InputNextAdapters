#pragma once

#include "skyrim_adapter/model.hpp"

namespace skyrim_adapter::detail {
enum class RumbleShape : std::uint8_t { steady, fade, double_pulse, double_tap };

struct TriggerRecipe {
    input_next_effects_trigger_v1 payload{};
    std::uint32_t duration_ms{};
    int priority{};
    bool present{};
};

struct RumbleRecipe {
    RumbleShape shape{RumbleShape::steady};
    float low{};
    float high{};
    float second_low{};
    float second_high{};
    MonoTime started_at;
    std::uint32_t duration_ms{};
    std::uint32_t peak_ms{};
    std::uint32_t gap_ms{};
    std::uint32_t second_duration_ms{};
    int priority{};
    bool present{};
};

struct ActionRecipe {
    TriggerRecipe left;
    TriggerRecipe right;
    RumbleRecipe rumble;
    std::optional<BodyCue> body;
};

struct FootstepTuning {
    std::uint32_t duration_ms{};
    float body_level{};
    float fallback_low{};
    float fallback_high{};
    int priority{};
};

[[nodiscard]] constexpr FootstepTuning footstep_tuning(MovementGait gait, bool mounted) noexcept {
    if (mounted) {
        switch (gait) {
        case MovementGait::walking:
            return {
                .duration_ms = 60,
                .body_level = 0.18F,
                .fallback_low = 0.045F,
                .fallback_high = 0.060F,
                .priority = 4,
            };
        case MovementGait::running:
            return {
                .duration_ms = 65,
                .body_level = 0.24F,
                .fallback_low = 0.060F,
                .fallback_high = 0.075F,
                .priority = 4,
            };
        case MovementGait::sprinting:
            return {
                .duration_ms = 70,
                .body_level = 0.30F,
                .fallback_low = 0.080F,
                .fallback_high = 0.100F,
                .priority = 4,
            };
        case MovementGait::unknown:
        case MovementGait::sneaking:
            return {};
        }
    }
    switch (gait) {
    case MovementGait::walking:
        return {
            .duration_ms = 50,
            .body_level = 0.12F,
            .fallback_low = 0.025F,
            .fallback_high = 0.035F,
            .priority = 2,
        };
    case MovementGait::running:
        return {
            .duration_ms = 55,
            .body_level = 0.17F,
            .fallback_low = 0.035F,
            .fallback_high = 0.050F,
            .priority = 2,
        };
    case MovementGait::sprinting:
        return {
            .duration_ms = 60,
            .body_level = 0.22F,
            .fallback_low = 0.050F,
            .fallback_high = 0.075F,
            .priority = 2,
        };
    case MovementGait::sneaking:
        return {
            .duration_ms = 55,
            .body_level = 0.07F,
            .fallback_low = 0.010F,
            .fallback_high = 0.015F,
            .priority = 2,
        };
    case MovementGait::unknown:
        return {};
    }
    return {};
}

struct HeldMagicRecipe {
    TriggerRecipe trigger;
    float rumble_low{};
    float rumble_high{};
    bool rumble_present{};
};

[[nodiscard]] input_next_effects_trigger_v1 trigger(std::uint32_t mode,
    float start,
    float end,
    float strength,
    float frequency = 0.0F) noexcept;
[[nodiscard]] input_next_effects_trigger_v1 trigger_off() noexcept;
[[nodiscard]] ActionRecipe
action_recipe(const Action& action, const Observation& observation, MonoTime now) noexcept;
[[nodiscard]] BodyCue body_phase_recipe(BodyCueKind kind,
    const Observation& observation,
    MonoTime started_at,
    float scale) noexcept;
[[nodiscard]] TriggerRecipe bow_draw_trigger(const Observation& observation,
    bool baseline_set) noexcept;
[[nodiscard]] TriggerRecipe block_trigger(const Observation& observation) noexcept;
[[nodiscard]] HeldMagicRecipe held_magic_recipe(const HandObservation& hand) noexcept;
[[nodiscard]] std::uint32_t elapsed_ms(MonoTime now, MonoTime started_at) noexcept;
} // namespace skyrim_adapter::detail
