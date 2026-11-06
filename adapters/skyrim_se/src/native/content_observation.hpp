#pragma once

#include "skyrim_adapter/content_observation.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace RE {
class Actor;
}

namespace skyrim_adapter::native {
// Call only from an SKSE game task after DataLoaded. The resolver retains process-lifetime form pointers internally;
// returned observations contain no RE pointers.
[[nodiscard]] ContentObservation read_content_observation(const RE::Actor& actor) noexcept;

namespace detail {
enum class SurvivalGlobal : std::uint8_t { mode_enabled, cold_need, hunger_need, exhaustion_need };

enum class GlobalValueType : char { short_integer = 's', long_integer = 'l', floating = 'f' };

struct GlobalBinding {
    std::uint32_t local_form_id{};
    std::string_view plugin;
    std::string_view editor_id;
    GlobalValueType value_type{GlobalValueType::short_integer};
};

[[nodiscard]] constexpr GlobalBinding global_binding(SurvivalGlobal global) noexcept {
    constexpr std::string_view survival_plugin{"ccQDRSSE001-SurvivalMode.esl"};
    switch (global) {
    case SurvivalGlobal::mode_enabled:
        return {
            .local_form_id = 0x00000826,
            .plugin = survival_plugin,
            .editor_id = "Survival_ModeEnabled",
            .value_type = GlobalValueType::short_integer,
        };
    case SurvivalGlobal::cold_need:
        return {
            .local_form_id = 0x0000081B,
            .plugin = survival_plugin,
            .editor_id = "Survival_ColdNeedValue",
            .value_type = GlobalValueType::floating,
        };
    case SurvivalGlobal::hunger_need:
        return {
            .local_form_id = 0x0000081A,
            .plugin = survival_plugin,
            .editor_id = "Survival_HungerNeedValue",
            .value_type = GlobalValueType::floating,
        };
    case SurvivalGlobal::exhaustion_need:
        return {
            .local_form_id = 0x00000816,
            .plugin = survival_plugin,
            .editor_id = "Survival_ExhaustionNeedValue",
            .value_type = GlobalValueType::floating,
        };
    }
    return {};
}

[[nodiscard]] constexpr bool global_binding_metadata_matches(const GlobalBinding& binding,
    std::string_view editor_id,
    char value_type_code) noexcept {
    return !binding.plugin.empty() && !binding.editor_id.empty() &&
           binding.editor_id == editor_id &&
           static_cast<char>(binding.value_type) == value_type_code;
}

enum class SurvivalNeed : std::uint8_t { cold, hunger, exhaustion };

[[nodiscard]] constexpr std::optional<unsigned> need_stage(SurvivalNeed need,
    float value) noexcept {
    std::array<float, 5> thresholds{};
    float maximum{};
    switch (need) {
    case SurvivalNeed::cold:
        thresholds = {50.0F, 120.0F, 300.0F, 500.0F, 800.0F};
        maximum = 1000.0F;
        break;
    case SurvivalNeed::hunger:
        thresholds = {80.0F, 160.0F, 340.0F, 520.0F, 770.0F};
        maximum = 1000.0F;
        break;
    case SurvivalNeed::exhaustion:
        thresholds = {80.0F, 160.0F, 340.0F, 560.0F, 800.0F};
        maximum = 960.0F;
        break;
    default:
        return std::nullopt;
    }

    if (!(value >= 0.0F) || value > maximum) {
        return std::nullopt;
    }

    unsigned stage{};
    for (const float threshold : thresholds) {
        if (value >= threshold) {
            ++stage;
        }
    }
    return stage;
}
} // namespace detail
} // namespace skyrim_adapter::native
