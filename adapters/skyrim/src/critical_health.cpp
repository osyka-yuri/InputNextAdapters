#include "skyrim_adapter/detail/critical_health.hpp"

#include "skyrim_adapter/detail/body_cue_history.hpp"
#include "skyrim_adapter/effect_settings.hpp"
#include "skyrim_adapter/model.hpp"

#include "effect_tuning.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace skyrim_adapter::detail {
namespace {
using Milliseconds = std::chrono::milliseconds;

float unit_value(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    return std::clamp(value, 0.0F, 1.0F);
}
} // namespace

void CriticalHealthHeartbeat::reset() noexcept {
    baseline_set_ = false;
    active_ = false;
    next_heartbeat_at_ = MonoTime{};
}

std::optional<BodyCue> CriticalHealthHeartbeat::update(const Observation& observation,
    MonoTime now,
    bool body_observation_fresh,
    const EffectSettings& settings,
    float rumble_scale) noexcept {
    if (!body_observation_fresh) {
        // Body-only history waits for a new native sample after an audio-instance rebind.
        return std::nullopt;
    }

    if (!body_profile_admitted(settings, EffectId::critical_health) || !observation.in_combat ||
        !observation.health_ratio || !std::isfinite(*observation.health_ratio)) {
        reset();
        return std::nullopt;
    }

    const float health = unit_value(*observation.health_ratio);
    if (!baseline_set_) {
        baseline_set_ = true;
        active_ = health < 0.20F;
        next_heartbeat_at_ = now + Milliseconds{1050};
    } else if (active_ && health >= 0.25F) {
        active_ = false;
    } else if (!active_ && health < 0.20F) {
        active_ = true;
        next_heartbeat_at_ = now + Milliseconds{1050};
    }

    if (!active_ || now < next_heartbeat_at_) {
        return std::nullopt;
    }

    BodyCue cue{};
    cue.kind = BodyCueKind::critical_health;
    cue.started_at = now;
    cue.duration_ms = 200;
    cue.priority = 5;
    cue.scale = rumble_scale;
    if (!tune_body_cue(cue, settings) || !body_cue_active(cue, now)) {
        next_heartbeat_at_ = now + Milliseconds{1050};
        return std::nullopt;
    }

    next_heartbeat_at_ = now + Milliseconds{1050};
    return cue;
}

} // namespace skyrim_adapter::detail
