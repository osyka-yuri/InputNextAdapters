#pragma once

#include "effect_model.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace oblivion_adapter {

using MenuResults = std::array<std::optional<EffectEventKind>, 2>;

[[nodiscard]] inline MenuResults menu_results(const EffectObservation& previous,
    const EffectObservation& current) noexcept {
    if (!previous.action_stats || !current.action_stats || previous.epoch != current.epoch ||
        previous.player_id != current.player_id || previous.frame >= current.frame ||
        previous.sampled_at >= current.sampled_at ||
        current.sampled_at - previous.sampled_at > effect_max_age ||
        (current.mode != previous.mode && current.mode != EffectMode::gameplay)) {
        return {};
    }
    const auto& before = *previous.action_stats;
    const auto& after = *current.action_stats;
    const auto result = [](bool increased, EffectEventKind kind) -> std::optional<EffectEventKind> {
        return increased ? std::optional{kind} : std::nullopt;
    };
    switch (previous.mode) {
    case EffectMode::lockpick:
        return {result(after.locks_picked > before.locks_picked, EffectEventKind::lock_opened),
            result(after.lockpicks_broken > before.lockpicks_broken, EffectEventKind::pick_broken)};
    case EffectMode::training:
        return {
            result(after.training_sessions > before.training_sessions, EffectEventKind::trained)};
    case EffectMode::container:
        return {result(after.items_pickpocketed > before.items_pickpocketed,
            EffectEventKind::pickpocketed)};
    default:
        return {};
    }
}

// Only samples admitted on the same controller establish and confirm this counter.
class GateCounter final {
public:
    void reset() noexcept {
        previous_.reset();
        loading_ = false;
    }

    void note_loading() noexcept { loading_ = previous_.has_value(); }

    [[nodiscard]] bool observe(const EffectObservation& sample) noexcept {
        if (sample.mode != EffectMode::gameplay || !sample.action_stats || sample.cell_id == 0U) {
            reset();
            return false;
        }
        if (previous_ && sample.sampled_at <= previous_->at) {
            return false;
        }

        const Checkpoint current{.at = sample.sampled_at,
            .epoch = sample.epoch,
            .player = sample.player_id,
            .cell = sample.cell_id,
            .count = sample.action_stats->gates_closed};
        const auto previous = std::exchange(previous_, current);
        const bool loading = std::exchange(loading_, false);
        return previous && previous->epoch == current.epoch && previous->player == current.player &&
               current.count > previous->count &&
               (loading || current.cell != previous->cell ||
                   current.at - previous->at <= effect_max_age);
    }

private:
    struct Checkpoint {
        MonoTime at;
        std::uint64_t epoch{};
        std::uint32_t player{};
        std::uint32_t cell{};
        std::uint32_t count{};
    };

    std::optional<Checkpoint> previous_;
    bool loading_{};
};

} // namespace oblivion_adapter
