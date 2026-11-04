#pragma once

#include "skyrim_adapter/model.hpp"

namespace skyrim_adapter::detail {
[[nodiscard]] EffectId effect_id_for_body_cue(const BodyCue& cue) noexcept;
[[nodiscard]] bool body_profile_admitted(const EffectSettings& settings, EffectId id) noexcept;
[[nodiscard]] TriggerId trigger_id_for_spell(SpellKind spell) noexcept;
[[nodiscard]] TriggerId trigger_id_for_action(const Action& action,
    const Observation& observation) noexcept;
[[nodiscard]] TriggerId trigger_id_for_blocking(const Observation& observation) noexcept;
[[nodiscard]] TriggerId trigger_id_for_held_magic(const HandObservation& hand) noexcept;

void sanitize_effect_settings(EffectSettings& settings) noexcept;
[[nodiscard]] bool tune_body_cue(BodyCue& cue, const EffectSettings& settings) noexcept;

struct ClassicRumbleTuning {
    float& low;
    float& high;
    float& second_low;
    float& second_high;
    std::uint32_t& duration_ms;
    std::uint32_t& peak_ms;
    std::uint32_t& gap_ms;
    std::uint32_t& second_duration_ms;
};

void tune_classic_recipe(const BodyCue& cue, ClassicRumbleTuning tuning) noexcept;
[[nodiscard]] bool tune_trigger_payload(input_next_effects_trigger_v1& payload,
    TriggerId id,
    const EffectSettings& settings) noexcept;
} // namespace skyrim_adapter::detail
