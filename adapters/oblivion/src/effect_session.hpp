#pragma once

#include "action_results.hpp"
#include "effect_policy.hpp"

#include <input_next_core.h>
#include <inputnext_adapters/output_session.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace oblivion_adapter {

// Ingress is shared by xOBSE and the output worker. The worker alone drives OutputSession.
class EffectSession final : private inputnext::adapters::DeliveryObserver {
public:
    EffectSession(inputnext::adapters::OutputPort& output, const EffectSettings& settings) noexcept;

    void publish(EffectObservation observation) noexcept;
    bool push_event(EffectEvent event) noexcept;
    void set_focus(bool focused, MonoTime now) noexcept;
    void invalidate(MonoTime now) noexcept;
    void stop(MonoTime now) noexcept;

    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_.load(); }

    void tick(inputnext::adapters::TickClock clock) noexcept;
    [[nodiscard]] bool drain(inputnext::adapters::TickClock clock) noexcept;
    [[nodiscard]] inputnext::adapters::DeliveryDiagnostics diagnostics() const noexcept;

private:
    enum class GateBaseline : std::uint8_t { discard, preserve };

    // The worker can also derive a gate closure from the current observation.
    using FrameEvents = std::array<EffectEvent, effect_inbox_capacity + 1U>;

    struct Attempt {
        std::optional<EffectObservation> observation;
        std::uint64_t revision{};
        std::uint64_t mode_revision{};
    };

    void invalidate_locked(MonoTime now, GateBaseline gate = GateBaseline::discard) noexcept;
    void change_mode_locked(const EffectObservation& previous,
        const EffectObservation& next) noexcept;
    [[nodiscard]] bool enqueue_menu_results_locked(const EffectObservation& previous,
        const EffectObservation& next) noexcept;
    [[nodiscard]] bool enqueue_block_impact_locked(const EffectObservation& observation) noexcept;
    [[nodiscard]] bool enqueue_locked(EffectEvent event) noexcept;
    void discard_before_locked(MonoTime time) noexcept;
    [[nodiscard]] Attempt capture(MonoTime now) noexcept;
    [[nodiscard]] bool current_locked(const Attempt& attempt, MonoTime now) const noexcept;
    [[nodiscard]] bool holds_current(const EffectObservation& previous,
        const EffectObservation& next) const noexcept;
    [[nodiscard]] std::optional<std::size_t> prepare_frame(MonoTime now,
        FrameEvents& events) noexcept;
    [[nodiscard]] bool withdraw(inputnext::adapters::TickClock& clock) noexcept;

    [[nodiscard]] bool admitted(MonoTime now) const noexcept override;
    [[nodiscard]] inputnext::adapters::FrameRestriction frame_restriction(
        MonoTime now) const noexcept override;
    void binding_lost(input_next_status status) noexcept override;
    void binding_changed(MonoTime started_at) noexcept override;
    void body_rebound(MonoTime baseline_at) noexcept override;
    void admission_expired() noexcept override;

    mutable std::mutex ingress_;
    std::optional<EffectObservation> observation_;
    GateCounter gate_counter_;
    std::optional<input_next_device_target_v1> bound_target_;
    std::array<EffectEvent, effect_inbox_capacity> inbox_{};
    std::size_t size_{};
    std::uint64_t next_sequence_{1};
    std::uint64_t revision_{};
    std::uint64_t mode_revision_{};
    std::atomic<std::uint64_t> generation_;
    MonoTime earliest_sample_{MonoTime::min()};
    MonoTime event_floor_{MonoTime::min()};
    bool focused_{};
    bool stopped_{};

    const bool body_enabled_;
    const bool triggers_enabled_;
    const bool enabled_;
    const bool menu_effects_enabled_;
    const bool gate_result_enabled_;
    EffectPolicy policy_;
    inputnext::adapters::OutputSession output_;
    Attempt active_;
    std::uint64_t processed_revision_{};
    std::uint64_t processed_mode_revision_{};
    bool close_pending_{};
};

} // namespace oblivion_adapter
