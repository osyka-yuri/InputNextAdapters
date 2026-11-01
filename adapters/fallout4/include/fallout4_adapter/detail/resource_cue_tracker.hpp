#pragma once

#include "fallout4_adapter/detail/body_cue_intent.hpp"

#include <cstdint>
#include <optional>

namespace fallout4_adapter::detail {

struct ResourceObservation {
    std::optional<float> health_ratio;
    std::optional<float> action_points_ratio;
    std::optional<std::uint8_t> critical_count;
};

class ResourceCueTracker {
public:
    void reset() noexcept;
    [[nodiscard]] BodyCueBatch update(const ResourceObservation& observation,
        MonoTime now) noexcept;

private:
    struct ThresholdState {
        float previous{};
        bool has_baseline{};
        bool armed{true};
    };

    ThresholdState health_{};
    ThresholdState action_points_{};
    std::optional<std::uint8_t> previous_critical_count_;
};

} // namespace fallout4_adapter::detail
