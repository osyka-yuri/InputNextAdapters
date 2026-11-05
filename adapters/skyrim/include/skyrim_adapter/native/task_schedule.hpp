#pragma once

#include <skyrim_adapter/model.hpp>

#include <chrono>
#include <optional>

namespace skyrim_adapter::native {
class TaskSchedule {
public:
    static constexpr auto observation_interval = std::chrono::milliseconds{50};

    struct Reservation {
        NativeTicket ticket{};
        bool urgent{};
    };

    explicit TaskSchedule(MonoTime started_at = MonoTime{}) noexcept
        : next_observation_(started_at + observation_interval) {}

    void notify_output() noexcept { output_pending_ = true; }

    void notify_urgent() noexcept {
        urgent_pending_ = true;
        output_pending_ = true;
    }

    [[nodiscard]] bool consume_output_wake() noexcept {
        const bool pending = output_pending_;
        output_pending_ = false;
        return pending;
    }

    [[nodiscard]] bool wake_pending(bool ticket_admitted) const noexcept {
        return output_pending_ || (ticket_admitted && !outstanding_ && urgent_pending_);
    }

    [[nodiscard]] std::optional<Reservation> reserve(MonoTime now,
        std::optional<NativeTicket> ticket) noexcept {
        if (outstanding_ || !ticket || (!urgent_pending_ && now < next_observation_)) {
            return std::nullopt;
        }

        const bool urgent = urgent_pending_;
        urgent_pending_ = false;
        outstanding_ = true;
        next_observation_ = now + observation_interval;
        return Reservation{.ticket = *ticket, .urgent = urgent};
    }

    void complete() noexcept {
        outstanding_ = false;
        output_pending_ = true;
    }

    [[nodiscard]] bool outstanding() const noexcept { return outstanding_; }

    [[nodiscard]] bool urgent_pending() const noexcept { return urgent_pending_; }

    [[nodiscard]] MonoTime next_observation() const noexcept { return next_observation_; }

private:
    MonoTime next_observation_;
    bool output_pending_{};
    bool urgent_pending_{};
    bool outstanding_{};
};
} // namespace skyrim_adapter::native
