#pragma once

#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/policy.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

namespace fallout4_adapter {
enum class Control : std::uint8_t {
    data_ready,
    pre_load,
    load_succeeded,
    load_failed,
    new_game,
    focus_lost,
    focus_gained,
    context_changed,
    fault,
};

struct Ticket {
    std::uint64_t epoch{};
};

struct SessionDiagnostics {
    inputnext::adapters::DeliveryDiagnostics delivery{};
    std::uint64_t accepted_events{}, rejected_events{}, overflows{};
    bool faulted{};
};

// The worker transfers copied mailbox values and drives output through this session.
// gate_ serializes delivery and its observer callbacks. No game pointer is retained.
class Session final : private inputnext::adapters::DeliveryObserver {
public:
    explicit Session(inputnext::adapters::OutputPort& output) noexcept;
    Session(inputnext::adapters::OutputPort& output, const Config& config) noexcept;
    void control(Control control_value, MonoTime now) noexcept;
    [[nodiscard]] std::optional<Ticket> ticket() const noexcept;
    [[nodiscard]] bool publish(Ticket ticket, Observation observation, MonoTime now) noexcept;
    [[nodiscard]] bool enqueue(Ticket ticket, PolicyEvent event, MonoTime now) noexcept;
    void tick() noexcept;
    void tick(MonoTime now) noexcept;
    [[nodiscard]] bool close_and_drain() noexcept;
    [[nodiscard]] SessionDiagnostics diagnostics() const noexcept;

private:
    [[nodiscard]] bool admitted(MonoTime now) const noexcept override;
    [[nodiscard]] inputnext::adapters::FrameRestriction frame_restriction(
        MonoTime now) const noexcept override;
    void binding_lost(input_next_status status) noexcept override;
    void binding_changed(MonoTime started_at) noexcept override;
    void body_rebound(MonoTime baseline_at) noexcept override;
    void admission_expired() noexcept override;
    void discard(bool preserve_mode = false, bool preserve_led = false) noexcept;
    void discard_preserving_led() noexcept;
    void invalidate(bool preserve_mode = false) noexcept;
    void tick_locked(inputnext::adapters::TickClock& clock) noexcept;

    mutable std::mutex gate_;
    Policy policy_;
    inputnext::adapters::OutputSession output_;
    std::optional<Observation> observation_;
    std::optional<MonoTime> event_context_started_at_;
    std::array<PolicyEvent, 64> inbox_{};
    std::size_t count_{};
    std::uint64_t epoch_{1}, next_sequence_{1};
    SessionDiagnostics diagnostics_{};
    std::optional<MonoTime> preserve_led_deadline_;
    std::optional<MonoTime> last_context_pause_at_;
    bool enabled_{}, data_ready_{}, load_ready_{}, focused_{}, withdrawn_{}, context_pending_{},
        closing_{}, body_rebound_during_tick_{}, preserve_led_for_next_observation_{},
        ui_sample_pending_{};
};
} // namespace fallout4_adapter
