#include "native/owner_mailbox.hpp"

#include "fallout4_adapter/model.hpp"
#include "fallout4_adapter/session.hpp"

#include "native/observation.hpp"
#include "native/observation_requests.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace fallout4_adapter::native {
void OwnerMailbox::enqueue_control(Control control, MonoTime now) noexcept {
    if (control_count_ == controls_.size()) {
        control_overflow_ = true;
        return;
    }
    const auto index = (control_head_ + control_count_) % controls_.size();
    controls_[index] = ControlNotice{.control = control, .arrived_at = now};
    ++control_count_;
}

void OwnerMailbox::take_pending_batch(PendingBatch& output) noexcept {
    output.control_count = control_count_;
    for (std::size_t index = 0; index < output.control_count; ++index) {
        output.controls[index] = controls_[(control_head_ + index) % controls_.size()];
    }
    control_head_ = (control_head_ + control_count_) % controls_.size();
    control_count_ = 0;

    output.event_count = event_count_;
    for (std::size_t index = 0; index < output.event_count; ++index) {
        output.events[index] = events_[(event_head_ + index) % events_.size()];
    }
    event_head_ = (event_head_ + event_count_) % events_.size();
    event_count_ = 0;

    output.sample = std::exchange(sample_, std::nullopt);
    output.overflow = std::exchange(control_overflow_, false);
}

std::optional<OwnerMailbox::CachedPlayer> OwnerMailbox::callback_player(
    std::uint64_t current_generation) const noexcept {
    if (!ui_provenance_matches(event_epoch_,
            cached_player_.epoch,
            event_ui_generation_,
            cached_player_.ui_generation,
            current_generation) ||
        cached_player_.address == 0) {
        return std::nullopt;
    }
    return cached_player_;
}

void OwnerMailbox::publish_task_provenance(const std::optional<Ticket>& ticket,
    std::uint64_t ui_generation) noexcept {
    task_epoch_ = control_count_ == 0 && !control_overflow_ && ticket ? ticket->epoch : 0;
    task_ui_generation_ = ui_generation;
}

void OwnerMailbox::clear_task_provenance() noexcept {
    task_epoch_ = 0;
    task_ui_generation_ = 0;
}

void OwnerMailbox::clear_event_admission() noexcept {
    event_epoch_ = 0;
    event_ui_generation_ = 0;
}

void OwnerMailbox::discard_generation_data() noexcept {
    cached_player_ = {};
    sample_.reset();
    event_head_ = 0;
    event_count_ = 0;
}

void OwnerMailbox::discard_inert_publication() noexcept {
    clear_event_admission();
    cached_player_ = {};
    sample_.reset();
    event_count_ = 0;
}

void OwnerMailbox::reject_sample(const PendingSample& sample) noexcept {
    if (event_epoch_ == sample.ticket.epoch &&
        event_ui_generation_ == sample.observation.ui_generation) {
        clear_event_admission();
    }
}

void OwnerMailbox::publish_observation(const PendingObservation& request,
    const NativeObservation& observation) noexcept {
    cached_player_ = CachedPlayer{
        .epoch = request.lifecycle_epoch,
        .ui_generation = request.ui_generation,
        .handle = observation.player_handle,
        .address = observation.player_address,
        .form_id = observation.player_form_id,
        .weapon_identity = observation.value.weapon.identity,
        .power_armor = observation.value.power_armor,
        .sprinting = observation.value.sprinting,
    };
    sample_ = PendingSample{.ticket = Ticket{request.lifecycle_epoch}, .observation = observation};
    event_epoch_ = request.lifecycle_epoch;
    event_ui_generation_ = request.ui_generation;
}

void OwnerMailbox::retract_observation(const PendingObservation& request) noexcept {
    if (cached_player_.ui_generation == request.ui_generation &&
        cached_player_.epoch == request.lifecycle_epoch) {
        cached_player_ = {};
    }
    if (sample_ && sample_->ticket.epoch == request.lifecycle_epoch &&
        sample_->observation.ui_generation == request.ui_generation) {
        sample_.reset();
    }
    if (event_epoch_ == request.lifecycle_epoch && event_ui_generation_ == request.ui_generation) {
        clear_event_admission();
    }
}
} // namespace fallout4_adapter::native
