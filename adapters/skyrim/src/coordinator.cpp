#include <skyrim_adapter/coordinator.hpp>

#include <skyrim_adapter/detail/ingress.hpp>
#include <skyrim_adapter/model.hpp>

#include "coordinator/observation.hpp"

#include <inputnext_adapters/output.hpp>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>
#include <span>

namespace skyrim_adapter {
using coordinator_detail::fresh;
using coordinator_detail::hard_observation_valid;

Coordinator::Coordinator(inputnext::adapters::OutputPort& output, CoordinatorConfig config) noexcept
    : output_session_(output, *this, config.slot) {
    diagnostics_.faulted = config.slot > 3;
}

Coordinator::Coordinator(inputnext::adapters::OutputPort& output,
    CoordinatorConfig config,
    const PolicyConfig& policy) noexcept
    : policy_(policy),
      output_session_(output, *this, config.slot) {
    diagnostics_.faulted = config.slot > 3;
}

void Coordinator::discard_actions() noexcept {
    raw_count_ = pending_count_ = 0;
    ++burst_revision_;
}

void Coordinator::stage_delivery_reset_locked(DeliveryResetKind kind) noexcept {
    if (kind == DeliveryResetKind::hard) {
        pending_hard_reset_revision_ = burst_revision_;
    } else {
        pending_menu_reset_revision_ = burst_revision_;
    }
}

void Coordinator::stage_menu_pause_locked(MonoTime now) noexcept {
    stage_delivery_reset_locked(DeliveryResetKind::menu);
    pending_menu_pause_revision_ = burst_revision_;
    pending_menu_pause_at_ = now;
}

void Coordinator::request_withdrawal_locked(bool withdraw_body, bool force_revision) noexcept {
    if (force_revision || !withdrawal_pending_ || (withdraw_body && !body_withdrawal_pending_)) {
        ++withdrawal_revision_;
    }
    withdrawal_pending_ = true;
    body_withdrawal_pending_ = body_withdrawal_pending_ || withdraw_body;
}

void Coordinator::invalidate_locked(bool preserve_observation) noexcept {
    ++epoch_;
    discard_actions();
    if (!preserve_observation) {
        observation_.reset();
        ++observation_revision_;
        ++hard_fence_revision_;
    }
    suspended_ = true;
    request_withdrawal_locked(!preserve_observation, true);
}

void Coordinator::control(Control value, MonoTime now) noexcept {
    std::scoped_lock lock(ingress_gate_);
    if (closing_) {
        return;
    }
    bool changed = true;
    switch (value) {
    case Control::data_loaded:
        changed = !data_loaded_;
        data_loaded_ = true;
        break;
    case Control::pre_load:
    case Control::load_failed:
        load_ready_ = false;
        break;
    case Control::load_succeeded:
    case Control::new_game:
        load_ready_ = true;
        break;
    case Control::menu_suppressed:
        changed = menus_clear_;
        menus_clear_ = false;
        break;
    case Control::menu_resumed:
        changed = !menus_clear_;
        menus_clear_ = true;
        if (changed) {
            gameplay_resumed_at_ = now;
            gameplay_resume_pending_ = true;
        }
        break;
    case Control::focus_lost:
        changed = focused_;
        focused_ = false;
        break;
    case Control::focus_gained:
        changed = !focused_;
        focused_ = true;
        break;
    case Control::fault:
        diagnostics_.faulted = true;
        break;
    }
    if (changed) {
        invalidate_locked(value == Control::menu_suppressed || value == Control::menu_resumed);
        if (value == Control::menu_suppressed) {
            stage_menu_pause_locked(now);
        } else if (value == Control::menu_resumed) {
            stage_delivery_reset_locked(DeliveryResetKind::menu);
        } else {
            stage_delivery_reset_locked(DeliveryResetKind::hard);
        }
    }
}

std::optional<NativeTicket> Coordinator::native_ticket() const noexcept {
    std::scoped_lock lock(ingress_gate_);
    if (closing_ || !data_loaded_ || diagnostics_.faulted) {
        return std::nullopt;
    }
    return NativeTicket{epoch_};
}

void Coordinator::overflow() noexcept {
    ++diagnostics_.inbox_overflows;
    discard_actions();
    stage_delivery_reset_locked(DeliveryResetKind::hard);
}

bool Coordinator::enqueue(NativeTicket ticket, RawEvent event) noexcept {
    std::scoped_lock lock(ingress_gate_);
    if (closing_ || ticket.epoch != epoch_ || diagnostics_.faulted || !load_ready_ || !focused_ ||
        !menus_clear_) {
        return false;
    }
    if (!observation_ || !observation_->available || observation_->player_id == 0) {
        return false;
    }
    bool player_event = event.actor_id == observation_->player_id;
    if (event.kind == EventKind::footstep) {
        player_event =
            detail::footstep_enqueue_attributable(*observation_, event.actor_id, event.observed_at);
    } else if (event.kind == EventKind::hit) {
        player_event = player_event || event.target_id == observation_->player_id;
    }
    if (!player_event) {
        return false;
    }
    if (binding_started_at_ && event.observed_at <= *binding_started_at_) {
        return false;
    }
    if (raw_count_ == raw_.size()) {
        overflow();
        return true;
    }
    event.sequence = next_sequence_++;
    raw_[raw_count_++] = event;
    return true;
}

std::optional<NativeCapture> Coordinator::begin_read(NativeTicket ticket, MonoTime now) noexcept {
    std::scoped_lock lock(ingress_gate_);
    if (closing_ || ticket.epoch != epoch_ || diagnostics_.faulted || !data_loaded_ ||
        !load_ready_) {
        return std::nullopt;
    }
    NativeCapture capture{};
    capture.ticket = ticket;
    capture.burst_revision = burst_revision_;
    capture.read_started_at = now;
    capture.count = raw_count_;
    std::copy_n(raw_.begin(), raw_count_, capture.events.begin());
    raw_count_ = 0;
    return capture;
}

void Coordinator::publish(const NativeCapture& capture,
    Observation observation,
    std::span<const Action> actions,
    MonoTime now) noexcept {
    std::scoped_lock lock(ingress_gate_);
    if (closing_ || capture.ticket.epoch != epoch_ || diagnostics_.faulted || !load_ready_ ||
        !fresh(capture.read_started_at, now)) {
        return;
    }

    observation.sampled_at = capture.read_started_at;
    observation_ = observation;
    ++observation_revision_;
    if (!hard_observation_valid(observation)) {
        ++hard_fence_revision_;
        stage_delivery_reset_locked(DeliveryResetKind::hard);
    }

    // Preserve observation recovery ordering: actions from an obsolete capture are rejected
    // only after its fresh current-epoch observation has been published.
    if (capture.burst_revision != burst_revision_ || !focused_ || !menus_clear_) {
        return;
    }
    const auto binding_started_at = binding_started_at_;
    const auto action_admitted = [&](const Action& action) noexcept {
        return fresh(action.event.observed_at, now) &&
               (!binding_started_at || action.event.observed_at > *binding_started_at) &&
               detail::action_attribution_matches(action, observation, now);
    };
    std::size_t admitted_count{};
    for (const auto& action : actions) {
        if (action_admitted(action)) {
            ++admitted_count;
        }
    }
    if (admitted_count > pending_.size() - pending_count_) {
        overflow();
        return;
    }
    for (const auto& action : actions) {
        if (action_admitted(action)) {
            pending_[pending_count_++] = action;
        }
    }
}

void Coordinator::fault() noexcept {
    control(Control::fault, MonoTime{});
}

Diagnostics Coordinator::diagnostics() const noexcept {
    std::scoped_lock lock(ingress_gate_);
    return diagnostics_;
}

} // namespace skyrim_adapter
