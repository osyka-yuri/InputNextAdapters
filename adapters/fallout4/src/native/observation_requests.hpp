#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace fallout4_adapter::native {
struct PendingObservation {
    std::uint64_t request_id{};
    std::uint64_t lifecycle_epoch{};
    std::uint64_t ui_generation{};
};

enum class ObservationRequestPublish : std::uint8_t { published, coalesced, identity_exhausted };

// NativeOwnerPipeline holds inbox_mutex_ for request state and identity operations.
// pending_hint() permits early rejection; it never authorizes access to request state.
class ObservationRequests final {
public:
    enum class State : std::uint8_t { idle, pending, executing };

    constexpr explicit ObservationRequests(std::uint64_t initial_request_id = 0) noexcept
        : last_request_id_(initial_request_id) {}

    [[nodiscard]] bool pending_hint() const noexcept {
        return pending_hint_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool idle() const noexcept { return state_ == State::idle; }

    [[nodiscard]] bool pending() const noexcept { return state_ == State::pending; }

    [[nodiscard]] bool executing() const noexcept { return state_ == State::executing; }

    [[nodiscard]] std::uint64_t active_id() const noexcept {
        return state_ == State::idle ? 0 : request_.request_id;
    }

    [[nodiscard]] std::optional<PendingObservation> pending_request() const noexcept;

    [[nodiscard]] ObservationRequestPublish try_publish_idle(std::uint64_t lifecycle_epoch,
        std::uint64_t ui_generation) noexcept;
    [[nodiscard]] bool is_pending(const PendingObservation& request) const noexcept;
    [[nodiscard]] bool claim(const PendingObservation& request) noexcept;
    [[nodiscard]] bool is_executing(const PendingObservation& request) const noexcept;
    [[nodiscard]] bool cancel_pending(std::uint64_t request_id = 0) noexcept;
    [[nodiscard]] bool complete(std::uint64_t request_id) noexcept;

private:
    PendingObservation request_{};
    std::uint64_t last_request_id_{};
    State state_{State::idle};
    std::atomic<bool> pending_hint_;
};
} // namespace fallout4_adapter::native
