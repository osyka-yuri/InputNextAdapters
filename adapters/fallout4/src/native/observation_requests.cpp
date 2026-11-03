#include "native/observation_requests.hpp"

#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

namespace fallout4_adapter::native {
std::optional<PendingObservation> ObservationRequests::pending_request() const noexcept {
    return state_ == State::pending ? std::optional<PendingObservation>{request_} : std::nullopt;
}

ObservationRequestPublish ObservationRequests::try_publish_idle(std::uint64_t lifecycle_epoch,
    std::uint64_t ui_generation) noexcept {
    if (state_ != State::idle) {
        return ObservationRequestPublish::coalesced;
    }
    if (last_request_id_ == std::numeric_limits<std::uint64_t>::max()) {
        return ObservationRequestPublish::identity_exhausted;
    }
    const auto request_id = last_request_id_ + 1U;
    last_request_id_ = request_id;
    request_ = PendingObservation{
        .request_id = request_id,
        .lifecycle_epoch = lifecycle_epoch,
        .ui_generation = ui_generation,
    };
    state_ = State::pending;
    pending_hint_.store(true, std::memory_order_release);
    return ObservationRequestPublish::published;
}

bool ObservationRequests::is_pending(const PendingObservation& request) const noexcept {
    return state_ == State::pending && request_.request_id == request.request_id;
}

bool ObservationRequests::claim(const PendingObservation& request) noexcept {
    if (!is_pending(request)) {
        return false;
    }
    state_ = State::executing;
    pending_hint_.store(false, std::memory_order_release);
    return true;
}

bool ObservationRequests::is_executing(const PendingObservation& request) const noexcept {
    return state_ == State::executing && request_.request_id == request.request_id;
}

bool ObservationRequests::cancel_pending(std::uint64_t request_id) noexcept {
    if (state_ != State::pending || (request_id != 0 && request_.request_id != request_id)) {
        return false;
    }
    request_ = {};
    state_ = State::idle;
    pending_hint_.store(false, std::memory_order_release);
    return true;
}

bool ObservationRequests::complete(std::uint64_t request_id) noexcept {
    if (state_ != State::executing || request_.request_id != request_id) {
        return false;
    }
    request_ = {};
    state_ = State::idle;
    pending_hint_.store(false, std::memory_order_release);
    return true;
}
} // namespace fallout4_adapter::native
