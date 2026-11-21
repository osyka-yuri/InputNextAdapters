#pragma once

#include "effect_model.hpp"

#include <cstdint>
#include <optional>

namespace oblivion_adapter {

// Converts confirmed horizontal travel into an occasional footfall or hoofbeat.
class GaitFeedback {
public:
    [[nodiscard]] Gait observe(const EffectObservation& sample) noexcept;
    void reset() noexcept;

private:
    struct SampleStamp {
        MonoTime at;
        std::uint64_t frame;
    };

    struct MotionSample {
        MonoTime at;
        std::uint64_t epoch;
        std::uint32_t player;
        std::uint32_t cell;
        std::uint32_t mover;
        Gait gait;
        double x;
        double y;
    };

    struct Velocity {
        double x;
        double y;
        double speed;

        [[nodiscard]] bool follows(const Velocity& previous) const noexcept;
    };

    void clear_progress() noexcept;
    void restart_at(const MotionSample& sample) noexcept;

    std::optional<MotionSample> previous_;
    // Retain a net-motion origin while high-frequency coordinate deltas are
    // below the noise floor; expire it after one fresh-snapshot window.
    std::optional<MotionSample> motion_anchor_;
    std::optional<Velocity> previous_velocity_;
    std::optional<SampleStamp> last_accepted_;
    std::optional<MonoTime> last_cue_at_;
    unsigned stable_intervals_{};
    double distance_phase_{};
};

} // namespace oblivion_adapter
