#pragma once

#include <inputnext_adapters/output_frame.hpp>

#include <cstdint>
#include <optional>

namespace inputnext::adapters {
struct LedHealthSample {
    float current{};
    float maximum{};
};

struct LedStealthSample {
    bool sneaking{};
    std::int32_t detection_score{};
};

struct LedObservation {
    MonoTime sampled_at;
    std::uint64_t subject_id{};
    std::optional<LedHealthSample> health;
    std::optional<LedStealthSample> stealth;
    std::optional<std::uint32_t> radiation_sources;
    bool in_combat{};
    // Independently attributed facts, collapsed by the producer using the maximum timestamp.
    std::optional<MonoTime> incoming_hit_at;
    std::optional<MonoTime> confirmed_damage_at;
};

struct LedSettings {
    bool enabled{true};
    bool health_enabled{true};
    bool stealth_enabled{true};
    bool radiation_enabled{true};
    std::uint32_t health_hold_ms{4000};
    std::uint32_t healing_transition_ms{200};
    float critical_enter{0.20F};
    float critical_exit{0.25F};
    std::int32_t detection_hidden_score{0};
    std::int32_t detection_alert_score{100};
};

enum class LedMode : std::uint8_t {
    none,
    critical_health,
    recent_damage,
    radiation,
    combat_health,
    stealth,
    health,
};

[[nodiscard]] bool valid_led_settings(const LedSettings& settings) noexcept;

class LedPolicy final {
public:
    explicit LedPolicy(LedSettings settings = {}) noexcept;
    void reset() noexcept;
    // Intentional menu gaps suppress output while keeping only absolute, still-live
    // indication deadlines. The next update must provide a fresh same-subject sample.
    void pause(MonoTime now) noexcept;
    [[nodiscard]] std::optional<LedIntent> resume(const LedObservation& observation,
        MonoTime now) noexcept;
    [[nodiscard]] std::optional<LedIntent> update(const LedObservation& observation,
        MonoTime now) noexcept;
    [[nodiscard]] LedMode mode() const noexcept;

private:
    // The policy owns only copied values and monotonic deadlines. A source boundary,
    // subject change, or clock discontinuity can discard the complete state at once.
    [[nodiscard]] bool begin_update(const LedObservation& observation, MonoTime now) noexcept;
    [[nodiscard]] bool accepts_observation(const LedObservation& observation,
        MonoTime now,
        bool& reset_for_baseline) const noexcept;
    [[nodiscard]] bool event_carrier_is_current(const LedObservation& observation,
        MonoTime now) const noexcept;
    void reset_at(MonoTime now) noexcept;
    void clear_health_state() noexcept;
    void clear_hit_correlation() noexcept;
    void apply_observation(const LedObservation& observation) noexcept;
    void apply_health_sample(const LedHealthSample& health, MonoTime sampled_at) noexcept;
    void update_critical_latch() noexcept;
    void apply_event_evidence(const LedObservation& observation,
        MonoTime now,
        bool health_baseline_before_sample) noexcept;
    void match_incoming_hit(MonoTime now) noexcept;
    [[nodiscard]] float displayed_health_ratio(MonoTime now) const noexcept;
    [[nodiscard]] std::optional<LedIntent> arbitrate(MonoTime now) noexcept;

    LedSettings settings_{};
    LedMode mode_{LedMode::none};
    bool settings_valid_{};
    bool paused_{};
    std::optional<std::uint64_t> subject_id_;
    std::optional<MonoTime> last_observation_at_;
    std::optional<MonoTime> last_now_;
    std::optional<MonoTime> paused_at_;

    std::optional<float> health_current_;
    float health_maximum_{};
    float health_ratio_{};
    float ramp_from_{};
    float ramp_to_{};
    MonoTime ramp_started_;
    MonoTime ramp_until_;
    bool ramp_active_{};
    std::optional<MonoTime> health_deadline_;
    std::optional<MonoTime> damage_deadline_;
    std::optional<MonoTime> comparable_loss_at_;
    float comparable_loss_maximum_{};
    bool critical_latched_{};

    std::optional<LedStealthSample> stealth_;
    std::optional<std::uint32_t> radiation_sources_;
    bool in_combat_{};

    std::optional<MonoTime> incoming_hit_at_;
    std::optional<MonoTime> last_incoming_hit_at_;
    std::optional<MonoTime> matched_incoming_hit_at_;
    std::optional<MonoTime> last_confirmed_damage_at_;
};
} // namespace inputnext::adapters
