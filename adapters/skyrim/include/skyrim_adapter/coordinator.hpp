#pragma once

#include <inputnext_adapters/output_session.hpp>

#include <skyrim_adapter/body_waveform.hpp>
#include <skyrim_adapter/policy.hpp>

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>

namespace skyrim_adapter {
struct Diagnostics {
    input_next_status last_status{INPUT_NEXT_STATUS_UNAVAILABLE};
    std::uint64_t inbox_overflows{};
    bool faulted{};
    bool body_owner_present{};
    std::uint32_t accepted_body_route{INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC};
    std::uint32_t body_fallback_reason{INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE};
};

// Ingress APIs use a short gate; ticks serialize OutputPort delivery separately.
class Coordinator final : private inputnext::adapters::DeliveryObserver {
public:
    explicit Coordinator(inputnext::adapters::OutputPort& output,
        CoordinatorConfig config = CoordinatorConfig()) noexcept;
    Coordinator(inputnext::adapters::OutputPort& output,
        CoordinatorConfig config,
        const PolicyConfig& policy) noexcept;
    void control(Control value, MonoTime now) noexcept;
    [[nodiscard]] std::optional<NativeTicket> native_ticket() const noexcept;
    // True requests native reconciliation for an admitted player event, including an inbox overflow.
    [[nodiscard]] bool enqueue(NativeTicket ticket, RawEvent event) noexcept;
    [[nodiscard]] std::optional<NativeCapture> begin_read(NativeTicket ticket,
        MonoTime now) noexcept;
    void publish(const NativeCapture& capture,
        Observation observation,
        std::span<const Action> actions,
        MonoTime now) noexcept;
    void tick() noexcept;
    void tick(MonoTime now) noexcept;
    [[nodiscard]] bool close_and_drain() noexcept;
    void fault() noexcept;
    [[nodiscard]] Diagnostics diagnostics() const noexcept;

private:
    enum class DeliveryResetKind : std::uint8_t { hard, menu };

    struct DeliveryContext {
        Epoch epoch{};
        std::uint64_t burst_revision{};
        std::uint64_t hard_fence_revision{};
        std::uint64_t observation_revision{};
        Observation observation{};
        bool playable{};
    };

    void tick_locked(inputnext::adapters::TickClock& clock) noexcept;
    [[nodiscard]] bool capture_delivery_batch(MonoTime now,
        DeliveryContext& context,
        std::array<Action, inbox_capacity>& actions,
        std::size_t& action_count) noexcept;
    void run_tick(MonoTime supplied, bool use_steady_clock) noexcept;
    [[nodiscard]] bool process_withdrawal_phase(inputnext::adapters::TickClock& clock) noexcept;
    [[nodiscard]] bool maintain_live_admission(MonoTime now) noexcept;
    void publish_delivery_diagnostics() noexcept;
    void reset_delivery_history(std::uint64_t burst_revision) noexcept;
    void stage_delivery_reset_locked(DeliveryResetKind kind) noexcept;
    void stage_menu_pause_locked(MonoTime now) noexcept;
    void invalidate_locked(bool preserve_observation = false) noexcept;
    void request_withdrawal_locked(bool withdraw_body, bool force_revision = false) noexcept;
    void discard_actions() noexcept;
    void overflow() noexcept;
    [[nodiscard]] bool live_admitted_locked(MonoTime now) const noexcept;

    [[nodiscard]] bool admitted(MonoTime now) const noexcept override;
    [[nodiscard]] inputnext::adapters::FrameRestriction frame_restriction(
        MonoTime now) const noexcept override;
    void binding_lost(input_next_status status) noexcept override;
    void binding_changed(MonoTime started_at) noexcept override;
    void body_rebound(MonoTime baseline_at) noexcept override;
    void admission_expired() noexcept override;

    Policy policy_;
    mutable std::mutex ingress_gate_;
    std::mutex delivery_gate_;
    inputnext::adapters::OutputSession output_session_;
    Epoch epoch_{1};
    std::uint64_t burst_revision_{1}, next_sequence_{1};
    std::uint64_t hard_fence_revision_{1}, observation_revision_{1}, withdrawal_revision_{};
    std::uint64_t processed_burst_revision_{1};
    std::optional<std::uint64_t> pending_hard_reset_revision_;
    std::optional<std::uint64_t> pending_menu_reset_revision_;
    std::optional<std::uint64_t> pending_menu_pause_revision_;
    std::optional<MonoTime> pending_menu_pause_at_;
    std::array<RawEvent, inbox_capacity> raw_{};
    std::array<Action, inbox_capacity> pending_{};
    std::size_t raw_count_{}, pending_count_{};
    bool data_loaded_{}, load_ready_{}, menus_clear_{true}, focused_{true};
    bool suspended_{true}, withdrawal_pending_{}, body_withdrawal_pending_{}, closing_{};
    MonoTime gameplay_resumed_at_;
    bool gameplay_resume_pending_{};
    std::optional<MonoTime> binding_started_at_;
    std::optional<Observation> observation_;
    Diagnostics diagnostics_;

    // Delivery-owned while delivery_gate_ is held. It is absent during maintenance.
    std::optional<DeliveryContext> active_context_;
    mutable MonoTime observer_check_time_;
};
} // namespace skyrim_adapter
