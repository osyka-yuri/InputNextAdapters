#include "fallout4_adapter/detail/landing_detector.hpp"

#include "fallout4_adapter/detail/body_cue_intent.hpp"
#include "fallout4_adapter/effect_settings.hpp"
#include "fallout4_adapter/model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace fallout4_adapter::detail {

void LandingDetector::reset() noexcept {
    previous_landing_.reset();
    previous_landing_at_ = {};
    landing_apex_z_ = 0.0F;
    landing_tracking_ = false;
}

std::optional<BodyCueIntent> LandingDetector::update(Mode mode,
    const std::optional<LandingObservation>& sample,
    MonoTime sampled_at,
    std::chrono::milliseconds maximum_gap) noexcept {
    if (mode != Mode::gameplay || !sample || sample->player_id == 0 || sample->cell_id == 0 ||
        !std::isfinite(sample->x) || !std::isfinite(sample->y) || !std::isfinite(sample->z)) {
        previous_landing_.reset();
        landing_tracking_ = false;
        return std::nullopt;
    }
    if (previous_landing_ && sampled_at <= previous_landing_at_) {
        if (sampled_at < previous_landing_at_) {
            previous_landing_.reset();
            landing_tracking_ = false;
        }
        return std::nullopt;
    }

    std::optional<BodyCueIntent> cue;
    bool continuous = false;
    if (previous_landing_) {
        const auto& previous = *previous_landing_;
        const double dx = static_cast<double>(sample->x) - previous.x;
        const double dy = static_cast<double>(sample->y) - previous.y;
        const double dz = static_cast<double>(sample->z) - previous.z;
        continuous = sample->player_id == previous.player_id &&
                     sample->cell_id == previous.cell_id &&
                     sampled_at - previous_landing_at_ <= maximum_gap &&
                     (dx * dx) + (dy * dy) + (dz * dz) <= 512.0 * 512.0;
    }
    if (!continuous) {
        landing_tracking_ = sample->airborne;
        landing_apex_z_ = sample->z;
    } else if (sample->airborne) {
        landing_apex_z_ = landing_tracking_ ? std::max(landing_apex_z_, sample->z) : sample->z;
        landing_tracking_ = true;
    } else {
        if (previous_landing_->airborne && landing_tracking_) {
            const double descent = static_cast<double>(landing_apex_z_) - sample->z;
            if (descent >= 64.0) {
                const auto amount =
                    static_cast<float>(std::clamp((descent - 64.0) / 448.0, 0.0, 1.0));
                cue = BodyCueIntent{
                    .effect = BodyEffect::landing,
                    .started_at = sampled_at,
                    .intensity = 0.55F + (0.45F * amount),
                    .priority_override = std::nullopt,
                    .duration_multiplier = 1.0F + (0.875F * amount),
                };
            }
        }
        landing_tracking_ = false;
    }
    previous_landing_ = sample;
    previous_landing_at_ = sampled_at;
    return cue;
}

} // namespace fallout4_adapter::detail
