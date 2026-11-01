#pragma once

#include "fallout4_adapter/detail/body_voice_mixer.hpp"
#include "fallout4_adapter/detail/landing_detector.hpp"
#include "fallout4_adapter/detail/resource_cue_tracker.hpp"
#include "model.hpp"

#include <optional>
#include <span>

namespace fallout4_adapter {
inline constexpr auto observation_max_age = std::chrono::milliseconds{150};
inline constexpr auto event_max_age = std::chrono::milliseconds{150};
inline constexpr std::uint32_t effect_ttl_max_ms = 200;

class Policy {
public:
    Policy() noexcept;
    explicit Policy(const PolicyConfig& config) noexcept;

    void reset() noexcept;
    // Withdraw voices and resource baselines while retaining a known mode for optional UI cues.
    void reset_effects() noexcept;
    // Clear body/resource state while retaining the independent LED history.
    void reset_effects_preserving_led() noexcept;
    void reset_preserving_led() noexcept;
    void pause_led_for_context(MonoTime now) noexcept;
    [[nodiscard]] OutputFrame update(const Observation& observation,
        std::span<const PolicyEvent> events,
        MonoTime now) noexcept;

private:
    [[nodiscard]] bool prepare_observation(const Observation& observation, MonoTime now) noexcept;

    struct MechanicBaseline {
        std::uint64_t weapon_identity{};
        bool reloading{};
        bool power_armor{};
    };

    void clear_voices() noexcept;
    void reset_resources() noexcept;
    void reset_mechanic_baseline() noexcept;
    void update_resources(const Observation& observation, MonoTime now) noexcept;
    void update_mechanic_cues(const Observation& observation, MonoTime now) noexcept;
    void update_landing(const Observation& observation, MonoTime now) noexcept;
    void add_event(const PolicyEvent& event, const Observation& observation, MonoTime now) noexcept;
    void add_cue(BodyEffect cue,
        MonoTime now,
        MonoTime started_at,
        float intensity = 1.0F,
        std::optional<int> priority_override = std::nullopt,
        float duration_multiplier = 1.0F) noexcept;
    void populate_triggers(OutputFrame& frame,
        const Observation& observation,
        MonoTime now) const noexcept;
    [[nodiscard]] OutputFrame make_frame(const Observation& observation, MonoTime now) noexcept;

    PolicyConfig config_{};
    inputnext::adapters::LedPolicy led_policy_;
    detail::BodyVoiceMixer body_voice_mixer_{};
    detail::ResourceCueTracker resource_cue_tracker_{};
    detail::LandingDetector landing_detector_{};
    std::optional<MechanicBaseline> previous_mechanics_;
    std::optional<Mode> previous_mode_;
    std::optional<std::uint64_t> previous_weapon_identity_;
    std::optional<MonoTime> last_shot_at_;
};
} // namespace fallout4_adapter
