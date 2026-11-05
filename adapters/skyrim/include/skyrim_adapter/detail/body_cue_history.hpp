#pragma once

#include "skyrim_adapter/model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace skyrim_adapter::detail {

enum class BodyCueSlot : std::uint8_t {
    left_held,
    right_held,
    left_transient,
    right_transient,
    body_transient,
};

[[nodiscard]] bool body_cue_active(const BodyCue& cue, MonoTime now) noexcept;

class BodyCueHistory {
public:
    // Clears cue slots while preserving admission fences across policy reset paths.
    void clear_history() noexcept;
    void expire(MonoTime now) noexcept;
    void offer(BodyCueSlot slot, BodyCue cue) noexcept;
    void refresh_held(const Observation& observation,
        MonoTime now,
        bool interrupted,
        const EffectSettings& settings,
        float rumble_scale) noexcept;
    [[nodiscard]] bool held_admission_open(std::size_t hand_index,
        MonoTime sampled_at) const noexcept;
    void fence_held_admission(std::size_t hand_index, MonoTime admission_after) noexcept;
    void fence_all_held_admission(MonoTime admission_after) noexcept;
    [[nodiscard]] bool has_admitted_bow_observation(const Observation& observation) const noexcept;
    [[nodiscard]] std::optional<BodyCue> held_cue(std::size_t hand_index,
        MonoTime now) const noexcept;
    [[nodiscard]] BodyPlan materialize(MonoTime now) const noexcept;

private:
    static constexpr std::size_t slot_index(BodyCueSlot slot) noexcept {
        return static_cast<std::size_t>(slot);
    }

    void refresh_held_hand(const Observation& observation,
        MonoTime now,
        bool interrupted,
        bool dual_concentration,
        std::size_t hand_index,
        const EffectSettings& settings,
        float rumble_scale) noexcept;

    std::array<std::optional<BodyCue>, 5> cues_{};
    std::array<std::optional<MonoTime>, 2> held_admission_after_{};
};

} // namespace skyrim_adapter::detail
