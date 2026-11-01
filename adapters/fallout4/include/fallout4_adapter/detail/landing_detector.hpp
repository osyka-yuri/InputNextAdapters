#pragma once

#include "fallout4_adapter/detail/body_cue_intent.hpp"

#include <chrono>
#include <optional>

namespace fallout4_adapter::detail {

class LandingDetector {
public:
    void reset() noexcept;
    [[nodiscard]] std::optional<BodyCueIntent> update(Mode mode,
        const std::optional<LandingObservation>& sample,
        MonoTime sampled_at,
        std::chrono::milliseconds maximum_gap) noexcept;

private:
    std::optional<LandingObservation> previous_landing_;
    MonoTime previous_landing_at_;
    float landing_apex_z_{};
    bool landing_tracking_{};
};

} // namespace fallout4_adapter::detail
