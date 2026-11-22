#pragma once

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/policy.hpp>
#include <fallout_gamebryo/settings.hpp>
#include <input_next_core.h>
#include <inputnext_adapters/output.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>

namespace fallout_gamebryo {
// Ingress can run during runtime calls. Only the output thread calls tick and drain.
class Session final : private inputnext::adapters::DeliveryObserver {
public:
    Session(inputnext::adapters::OutputPort& output, const Settings& settings) noexcept;

    void publish(const Observation& observation, std::span<const Event> events) noexcept;
    void invalidate(MonoTime now) noexcept;
    void set_foreground(bool focused, MonoTime now) noexcept;
    void stop(MonoTime now) noexcept;

    void tick(inputnext::adapters::TickClock clock) noexcept;
    [[nodiscard]] bool drain(inputnext::adapters::TickClock clock) noexcept;
    [[nodiscard]] inputnext::adapters::DeliveryDiagnostics diagnostics() const noexcept;

private:
    struct Snapshot {
        std::optional<Observation> observation;
        std::uint64_t revision{};
        std::uint64_t presentation{};
    };

    void invalidate_locked(MonoTime now) noexcept;
    void discard_events_through(MonoTime cutoff) noexcept;
    [[nodiscard]] Snapshot capture(MonoTime now) noexcept;
    [[nodiscard]] bool current_locked(MonoTime now) const noexcept;
    [[nodiscard]] std::optional<std::size_t> take_frame(MonoTime now,
        std::array<Event, event_capacity>& events) noexcept;
    [[nodiscard]] bool withdraw(inputnext::adapters::TickClock& clock) noexcept;

    [[nodiscard]] bool admitted(MonoTime now) const noexcept override;
    [[nodiscard]] inputnext::adapters::FrameRestriction frame_restriction(
        MonoTime now) const noexcept override;
    void binding_lost(input_next_status status) noexcept override;
    void binding_changed(MonoTime started_at) noexcept override;
    void body_rebound(MonoTime baseline_at) noexcept override;
    void admission_expired() noexcept override;

    mutable std::mutex ingress_;
    Snapshot latest_;
    std::array<Event, event_capacity> events_{};
    std::size_t event_count_{};
    MonoTime earliest_sample_{MonoTime::min()};
    MonoTime event_floor_{MonoTime::min()};
    bool foreground_{true};
    bool stopped_{};

    const bool enabled_;
    const bool menu_enabled_;
    Policy policy_;
    inputnext::adapters::OutputSession output_;
    Snapshot active_;
    std::uint64_t applied_revision_{};
    std::uint64_t applied_presentation_{};
    bool close_pending_{};
};
} // namespace fallout_gamebryo
