#pragma once

#include "body_effects.hpp"
#include "gait_feedback.hpp"

#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace oblivion_adapter {

// Derives output intents from copied game state and events.
class EffectPolicy final {
public:
    explicit EffectPolicy(const EffectSettings& settings) noexcept;

    void reset() noexcept;
    // Clear body cues and event history while preserving LED state.
    // Triggers are derived from the next observation.
    void reset_body() noexcept;

    [[nodiscard]] inputnext::adapters::OutputFrame update(const EffectObservation& sample,
        std::span<const EffectEvent> events,
        MonoTime now) noexcept;

private:
    struct GroundSample {
        MonoTime at;
        std::uint32_t cell_id;
        std::optional<float> airborne_peak;
    };

    [[nodiscard]] bool seen_or_remember(const EffectEvent& event) noexcept;
    void collect_combat(const EffectObservation& sample,
        std::span<const EffectEvent> events,
        MonoTime now) noexcept;
    void collect_contacts(const EffectObservation& sample,
        std::span<const EffectEvent> events,
        MonoTime now) noexcept;
    void collect_block_impacts(const EffectObservation& sample,
        std::span<const EffectEvent> events,
        MonoTime now) noexcept;
    void collect_magic(const EffectObservation& sample,
        std::span<const EffectEvent> events,
        MonoTime now) noexcept;
    void collect_actions(const EffectObservation& sample,
        std::span<const EffectEvent> events,
        MonoTime now) noexcept;
    void collect_movement(const EffectObservation& sample,
        std::span<const EffectEvent> events,
        MonoTime now) noexcept;
    void collect_footfall(const EffectObservation& sample) noexcept;
    void collect_incoming_contact(const EffectEvent& event,
        const EffectObservation& sample,
        std::span<const EffectEvent> events,
        MonoTime now,
        bool blocked) noexcept;
    void collect_player_hit(const EffectEvent& event,
        const EffectObservation& sample,
        std::span<const EffectEvent> events,
        MonoTime now) noexcept;
    void collect_fall_impact(const EffectEvent& event,
        const EffectObservation& sample,
        std::span<const EffectEvent> events,
        MonoTime now) noexcept;
    void collect_condition(const EffectObservation& sample, MonoTime now) noexcept;
    void update_held(const EffectObservation& sample, MonoTime now) noexcept;
    void apply_triggers(inputnext::adapters::OutputFrame& frame,
        const EffectObservation& sample) const noexcept;
    [[nodiscard]] std::optional<inputnext::adapters::LedIntent> update_leds(
        const EffectObservation& sample,
        std::span<const EffectEvent> events,
        MonoTime now) noexcept;

    BodyEffects body_;
    const float trigger_scale_;
    const bool menu_effects_enabled_;
    const bool leds_enabled_;
    inputnext::adapters::LedPolicy led_policy_;
    std::array<HeldBodyEffect, 2> held_{};
    GaitFeedback gait_;
    std::array<std::optional<EffectEvent>, effect_inbox_capacity> event_history_{};
    std::size_t next_event_stamp_{};
    std::optional<std::uint64_t> epoch_;
    std::optional<MonoTime> last_now_;
    std::optional<GroundSample> ground_;
    std::optional<bool> previous_mounted_;
    bool critical_latched_{};
    std::optional<MonoTime> next_heartbeat_at_;
    std::optional<float> previous_magicka_;
    std::optional<float> previous_fatigue_;
    EffectMode previous_mode_{EffectMode::unavailable};
    bool led_paused_{};
};

} // namespace oblivion_adapter
