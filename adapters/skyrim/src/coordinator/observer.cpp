#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>

#include "observation.hpp"

#include <input_next_core.h>
#include <inputnext_adapters/output_session.hpp>

#include <cstdint>
#include <mutex>

namespace skyrim_adapter {
using coordinator_detail::fresh;
using coordinator_detail::hard_observation_valid;

bool Coordinator::admitted(MonoTime now) const noexcept {
    observer_check_time_ = now;
    if (!active_context_) {
        return false;
    }
    const auto& frozen = active_context_->observation;
    if (!fresh(frozen.sampled_at, now) || !hard_observation_valid(frozen)) {
        return false;
    }

    std::scoped_lock lock(ingress_gate_);
    return live_admitted_locked(now) &&
           hard_fence_revision_ == active_context_->hard_fence_revision && observation_ &&
           observation_->player_id == frozen.player_id;
}

inputnext::adapters::FrameRestriction Coordinator::frame_restriction(MonoTime now) const noexcept {
    static_cast<void>(now);
    if (!active_context_ || !active_context_->playable) {
        return inputnext::adapters::FrameRestriction::neutral;
    }
    const auto& frozen = active_context_->observation;
    std::scoped_lock lock(ingress_gate_);
    const bool resume_is_fresh =
        !gameplay_resume_pending_ || frozen.sampled_at > gameplay_resumed_at_;
    if (closing_ || epoch_ != active_context_->epoch ||
        burst_revision_ != active_context_->burst_revision || !menus_clear_ ||
        !frozen.menus_clear || !observation_ || !observation_->menus_clear || !resume_is_fresh) {
        return inputnext::adapters::FrameRestriction::neutral;
    }
    return inputnext::adapters::FrameRestriction::none;
}

void Coordinator::binding_lost(input_next_status status) noexcept {
    std::uint64_t burst_snapshot{};
    {
        std::scoped_lock lock(ingress_gate_);
        diagnostics_.last_status = status;
        binding_started_at_.reset();
        discard_actions();
        stage_delivery_reset_locked(DeliveryResetKind::hard);
        observation_.reset();
        ++observation_revision_;
        ++hard_fence_revision_;
        suspended_ = true;
        request_withdrawal_locked(true, true);
        burst_snapshot = burst_revision_;
    }
    reset_delivery_history(burst_snapshot);
}

void Coordinator::binding_changed(MonoTime started_at) noexcept {
    std::uint64_t burst_snapshot{};
    {
        std::scoped_lock lock(ingress_gate_);
        binding_started_at_ = started_at;
        discard_actions();
        stage_delivery_reset_locked(DeliveryResetKind::hard);
        observation_.reset();
        ++observation_revision_;
        ++hard_fence_revision_;
        suspended_ = true;
        burst_snapshot = burst_revision_;
    }
    reset_delivery_history(burst_snapshot);
}

void Coordinator::body_rebound(MonoTime baseline_at) noexcept {
    policy_.reset_body_history(baseline_at);
}

void Coordinator::admission_expired() noexcept {
    std::uint64_t burst_snapshot{};
    {
        std::scoped_lock lock(ingress_gate_);
        if (closing_) {
            return;
        }
        const bool active_matches = active_context_ && active_context_->epoch == epoch_ &&
                                    active_context_->burst_revision == burst_revision_ &&
                                    active_context_->hard_fence_revision == hard_fence_revision_ &&
                                    active_context_->observation_revision == observation_revision_;
        if (active_matches && !live_admitted_locked(observer_check_time_) && !suspended_) {
            invalidate_locked(false);
        } else {
            request_withdrawal_locked(true, true);
        }
        stage_delivery_reset_locked(DeliveryResetKind::hard);
        burst_snapshot = burst_revision_;
    }
    reset_delivery_history(burst_snapshot);
}

} // namespace skyrim_adapter
