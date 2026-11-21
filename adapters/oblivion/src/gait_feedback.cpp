#include "gait_feedback.hpp"
#include "effect_model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace oblivion_adapter {
namespace {

constexpr auto kMaximumSampleGap = effect_max_age;
constexpr auto kMinimumCueInterval = std::chrono::milliseconds{250};
constexpr unsigned kStableIntervals = 3;
constexpr double kMinimumSpeed = 8.0;
// Oblivion positions are in world units. This generous sanity bound tolerates
// fast mounts and speed modifiers while rejecting frame-scale teleports.
constexpr double kMaximumSpeed = 1800.0;
// Treat tiny offsets as coordinate noise; net travel accumulates against the
// motion anchor until it crosses this floor or the anchor expires.
constexpr double kMinimumDisplacement = 0.25;
constexpr double kMinimumDirectionCosine = 0.5; // 60 degrees
constexpr double kMaximumSpeedRatio = 3.0;

bool is_mounted_gait(Gait gait) noexcept {
    return gait == Gait::mounted_walk || gait == Gait::mounted_run;
}

bool is_supported_gait(Gait gait) noexcept {
    switch (gait) {
    case Gait::walk:
    case Gait::run:
    case Gait::sneak:
    case Gait::mounted_walk:
    case Gait::mounted_run:
        return true;
    case Gait::none:
        return false;
    }
    return false;
}

bool usable(const EffectObservation& sample, const HorizontalPosition& position) noexcept {
    const bool mounted = is_mounted_gait(sample.gait);
    return sample.mode == EffectMode::gameplay && sample.alive && sample.epoch != 0U &&
           sample.player_id != 0U && sample.cell_id != 0U && position.mover_id != 0U &&
           std::isfinite(position.x) && std::isfinite(position.y) &&
           sample.ground == Ground::grounded && !sample.swimming &&
           is_supported_gait(sample.gait) && mounted == sample.mounted.value_or(false) &&
           mounted == (position.mover_id != sample.player_id);
}

double stride_length(Gait gait) noexcept {
    // Nominal travel per step in Oblivion world units. These deliberately broad
    // gait-specific distances produce natural cadence without exposing every poll.
    switch (gait) {
    case Gait::walk:
        return 70.0;
    case Gait::run:
        return 135.0;
    case Gait::sneak:
        return 50.0;
    case Gait::mounted_walk:
        return 175.0;
    case Gait::mounted_run:
        return 300.0;
    case Gait::none:
        return 0.0;
    }
    return 0.0;
}

} // namespace

void GaitFeedback::reset() noexcept {
    clear_progress();
    last_accepted_.reset();
    last_cue_at_.reset();
}

void GaitFeedback::clear_progress() noexcept {
    previous_.reset();
    motion_anchor_.reset();
    previous_velocity_.reset();
    stable_intervals_ = 0;
    distance_phase_ = 0.0;
}

void GaitFeedback::restart_at(const MotionSample& sample) noexcept {
    clear_progress();
    previous_ = sample;
    motion_anchor_ = sample;
}

bool GaitFeedback::Velocity::follows(const Velocity& previous) const noexcept {
    if (std::max(speed, previous.speed) > kMaximumSpeedRatio * std::min(speed, previous.speed)) {
        return false;
    }
    const double dot = (x * previous.x) + (y * previous.y);
    return dot >= kMinimumDirectionCosine * speed * previous.speed;
}

Gait GaitFeedback::observe(const EffectObservation& sample) noexcept {
    const auto& position = sample.horizontal_position;
    if (last_accepted_ && sample.frame == last_accepted_->frame) {
        if (!position || !usable(sample, *position) || !previous_ ||
            sample.epoch != previous_->epoch || sample.player_id != previous_->player ||
            sample.cell_id != previous_->cell || position->mover_id != previous_->mover ||
            sample.gait != previous_->gait || static_cast<double>(position->x) != previous_->x ||
            static_cast<double>(position->y) != previous_->y) {
            clear_progress();
        }
        return Gait::none;
    }
    if (last_accepted_ &&
        (sample.frame < last_accepted_->frame || sample.sampled_at <= last_accepted_->at)) {
        clear_progress();
        return Gait::none;
    }

    last_accepted_ = SampleStamp{.at = sample.sampled_at, .frame = sample.frame};
    if (!position || !usable(sample, *position)) {
        clear_progress();
        return Gait::none;
    }

    const MotionSample current{.at = sample.sampled_at,
        .epoch = sample.epoch,
        .player = sample.player_id,
        .cell = sample.cell_id,
        .mover = position->mover_id,
        .gait = sample.gait,
        .x = static_cast<double>(position->x),
        .y = static_cast<double>(position->y)};

    if (!previous_ || current.at - previous_->at > kMaximumSampleGap ||
        current.epoch != previous_->epoch || current.player != previous_->player ||
        current.cell != previous_->cell || current.mover != previous_->mover ||
        current.gait != previous_->gait || !motion_anchor_ ||
        current.at - motion_anchor_->at > kMaximumSampleGap) {
        restart_at(current);
        return Gait::none;
    }

    const auto elapsed = current.at - motion_anchor_->at;
    const double seconds = std::chrono::duration<double>{elapsed}.count();
    const double delta_x = current.x - motion_anchor_->x;
    const double delta_y = current.y - motion_anchor_->y;
    const double distance = std::hypot(delta_x, delta_y);
    previous_ = current;

    if (!std::isfinite(distance) || !std::isfinite(seconds) || seconds <= 0.0) {
        restart_at(current);
        return Gait::none;
    }
    if (distance < kMinimumDisplacement) {
        return Gait::none;
    }

    const double speed = distance / seconds;
    motion_anchor_ = current;
    if (!std::isfinite(speed) || speed < kMinimumSpeed || speed > kMaximumSpeed) {
        restart_at(current);
        return Gait::none;
    }

    const Velocity velocity{.x = delta_x / seconds, .y = delta_y / seconds, .speed = speed};
    if (!previous_velocity_ || !velocity.follows(*previous_velocity_)) {
        stable_intervals_ = 0;
        distance_phase_ = 0.0;
    }

    previous_velocity_ = velocity;
    stable_intervals_ = std::min(stable_intervals_ + 1U, kStableIntervals);
    distance_phase_ += distance;

    const double stride = stride_length(current.gait);
    if (stable_intervals_ < kStableIntervals || distance_phase_ < stride ||
        (last_cue_at_ && current.at - *last_cue_at_ < kMinimumCueInterval)) {
        return Gait::none;
    }

    // There can be several unobservable steps between widely spaced snapshots.
    // Emit one cue only and retain the fractional stride phase.
    distance_phase_ = std::fmod(distance_phase_, stride);
    last_cue_at_ = current.at;
    return current.gait;
}

} // namespace oblivion_adapter
