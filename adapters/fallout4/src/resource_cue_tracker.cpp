#include "fallout4_adapter/detail/resource_cue_tracker.hpp"

#include "fallout4_adapter/detail/body_cue_intent.hpp"
#include "fallout4_adapter/effect_settings.hpp"
#include "fallout4_adapter/model.hpp"

#include <cmath>
#include <optional>

namespace fallout4_adapter::detail {
namespace {

struct ResourceThreshold {
    float low{};
    float rearm{};
};

[[nodiscard]] bool valid_ratio(const std::optional<float>& ratio) noexcept {
    return ratio && std::isfinite(*ratio) && *ratio >= 0.0F && *ratio <= 1.0F;
}

[[nodiscard]] std::optional<float> checked_ratio(const std::optional<float>& ratio) noexcept {
    return valid_ratio(ratio) ? ratio : std::nullopt;
}

} // namespace

void ResourceCueTracker::reset() noexcept {
    health_ = {};
    action_points_ = {};
    previous_critical_count_.reset();
}

BodyCueBatch ResourceCueTracker::update(const ResourceObservation& observation,
    MonoTime now) noexcept {
    BodyCueBatch cues{};
    const auto update_threshold = [&cues, now](ThresholdState& state,
                                      const std::optional<float>& optional_value,
                                      ResourceThreshold threshold,
                                      BodyEffect effect) noexcept {
        const auto value = checked_ratio(optional_value);
        if (!value) {
            state = {};
            return;
        }
        if (!state.has_baseline) {
            state.previous = *value;
            state.armed = *value >= threshold.rearm;
            state.has_baseline = true;
            return;
        }
        if (*value >= threshold.rearm) {
            state.armed = true;
        }
        const bool crossed_down =
            state.armed && state.previous > threshold.low && *value <= threshold.low;
        state.previous = *value;
        if (crossed_down) {
            state.armed = false;
            cues.append({.effect = effect,
                .started_at = now,
                .intensity = 1.0F,
                .priority_override = std::nullopt,
                .duration_multiplier = 1.0F});
        }
    };

    update_threshold(health_,
        observation.health_ratio,
        ResourceThreshold{.low = 0.25F, .rearm = 0.33F},
        BodyEffect::health_warning);
    update_threshold(action_points_,
        observation.action_points_ratio,
        ResourceThreshold{.low = 0.20F, .rearm = 0.29F},
        BodyEffect::ap_warning);

    if (!observation.critical_count) {
        previous_critical_count_.reset();
    } else if (!previous_critical_count_) {
        previous_critical_count_ = observation.critical_count;
    } else {
        const bool became_ready = *previous_critical_count_ == 0 && *observation.critical_count > 0;
        previous_critical_count_ = observation.critical_count;
        if (became_ready) {
            cues.append({.effect = BodyEffect::critical_ready,
                .started_at = now,
                .intensity = 1.0F,
                .priority_override = std::nullopt,
                .duration_multiplier = 1.0F});
        }
    }
    return cues;
}

} // namespace fallout4_adapter::detail
