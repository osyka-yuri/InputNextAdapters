#pragma once

#include "fallout4_adapter/model.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace fallout4_adapter::detail {
class MagazineCueTracker {
public:
    void reset() noexcept {
        sample_.reset();
        empty_window_.reset();
        consumed_through_.reset();
        shot_count_ = 0;
    }

    void record_shot(MonoTime shot_at) noexcept {
        if (consumed_through_ && shot_at <= *consumed_through_) {
            return;
        }
        if (shot_count_ < shots_.size()) {
            shots_[shot_count_++] = shot_at;
        } else {
            const auto oldest = std::ranges::min_element(shots_);
            *oldest = std::max(*oldest, shot_at);
        }
    }

    [[nodiscard]] std::optional<MonoTime> update(std::optional<std::uint32_t> loaded_rounds,
        MonoTime sampled_at,
        MonoTime now,
        std::chrono::milliseconds maximum_age) noexcept {
        if (!loaded_rounds) {
            reset();
            return std::nullopt;
        }

        if (!sample_ || sampled_at > sample_->at) {
            auto unchanged_since = sampled_at;
            if (sample_ && sample_->rounds == *loaded_rounds &&
                sampled_at - sample_->at <= maximum_age) {
                unchanged_since = sample_->unchanged_since;
            }
            if (*loaded_rounds != 0) {
                empty_window_.reset();
            } else if (sample_ && sample_->rounds != 0 && sampled_at - sample_->at <= maximum_age) {
                empty_window_ = EmptyWindow{.earliest_shot_at = sample_->shot_floor(),
                    .first_empty_at = sampled_at};
            }
            sample_ = Sample{.rounds = *loaded_rounds,
                .at = sampled_at,
                .unchanged_since = unchanged_since};
        }

        if (empty_window_ && now - empty_window_->first_empty_at > maximum_age) {
            empty_window_.reset();
        }
        const auto earliest =
            empty_window_ ? empty_window_->earliest_shot_at : sample_->shot_floor();
        prune_shots(earliest, now, maximum_age);
        if (!empty_window_) {
            return std::nullopt;
        }

        // A newer shot outside the window must not hide an eligible shot.
        const auto cutoff = empty_window_->first_empty_at + shot_sample_slack;
        std::optional<MonoTime> matched;
        for (const auto shot_at : std::span{shots_}.first(shot_count_)) {
            if (shot_at <= now && shot_at <= cutoff && (!matched || shot_at > *matched)) {
                matched = shot_at;
            }
        }
        if (matched) {
            consumed_through_ = matched;
            empty_window_.reset();
        }
        return matched;
    }

private:
    static constexpr auto shot_sample_slack = std::chrono::milliseconds{40};
    static constexpr std::size_t shot_capacity = 64;

    void
    prune_shots(MonoTime earliest, MonoTime now, std::chrono::milliseconds maximum_age) noexcept {
        const auto active = std::span{shots_}.first(shot_count_);
        const auto removed =
            std::ranges::remove_if(active, [this, earliest, now, maximum_age](MonoTime shot_at) {
                return shot_at < earliest || now - shot_at > maximum_age ||
                       (consumed_through_ && shot_at <= *consumed_through_);
            });
        shot_count_ = static_cast<std::size_t>(removed.begin() - active.begin());
    }

    struct Sample {
        std::uint32_t rounds;
        MonoTime at;
        MonoTime unchanged_since;

        [[nodiscard]] MonoTime shot_floor() const noexcept {
            // Fire can notify before UseAmmo. A count change establishes a new floor.
            return std::max(unchanged_since, at - shot_sample_slack);
        }
    };

    struct EmptyWindow {
        MonoTime earliest_shot_at;
        MonoTime first_empty_at;
    };

    std::optional<Sample> sample_;
    std::optional<EmptyWindow> empty_window_;
    std::optional<MonoTime> consumed_through_;
    std::array<MonoTime, shot_capacity> shots_{};
    std::size_t shot_count_{};
};
} // namespace fallout4_adapter::detail
