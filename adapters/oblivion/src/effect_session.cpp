#include "effect_session.hpp"

#include "action_results.hpp"
#include "effect_model.hpp"
#include "effect_settings.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <span>

#include <input_next_core.h>
#include <inputnext_adapters/output.hpp>
#include <inputnext_adapters/output_session.hpp>

namespace oblivion_adapter {
namespace {

bool fresh(MonoTime sampled_at, MonoTime now) noexcept {
    return sampled_at <= now && now - sampled_at <= effect_max_age;
}

bool usable(const EffectObservation& observation) noexcept {
    return observation.mode != EffectMode::unavailable && observation.mode != EffectMode::loading &&
           observation.alive && observation.player_id != 0 && observation.health.valid;
}

bool older_than(const EffectObservation& candidate, const EffectObservation& previous) noexcept {
    return candidate.epoch < previous.epoch || candidate.sampled_at < previous.sampled_at ||
           (candidate.epoch == previous.epoch && candidate.frame < previous.frame);
}

bool same_target(const input_next_device_target_v1& left,
    const input_next_device_target_v1& right) noexcept {
    return std::ranges::equal(left.runtime_id, right.runtime_id) &&
           left.device_key == right.device_key && left.connection_id == right.connection_id;
}

bool player_hit(const EffectEvent& event, const EffectObservation& observation) noexcept {
    return event.kind == EffectEventKind::hit && event.actor_id == observation.player_id &&
           event.target_id != 0 && event.target_id != observation.player_id;
}

void preserve_unarmed_contact(std::array<EffectEvent, effect_inbox_capacity>& inbox,
    std::size_t size,
    const EffectObservation& observation) noexcept {
    if (observation.mode != EffectMode::gameplay || observation.frame == 0 ||
        observation.weapon != Weapon::unarmed || observation.weapon_id != 0 ||
        !observation.melee_attack) {
        return;
    }

    for (std::size_t index = 0; index < size; ++index) {
        auto& event = inbox[index];
        if (event.epoch != observation.epoch || event.mode != EffectMode::gameplay ||
            event.frame != observation.frame || !fresh(event.at, observation.sampled_at)) {
            continue;
        }
        if (player_hit(event, observation) && event.weapon == Weapon::unarmed &&
            event.source_id == 0) {
            // Preserve the contact frame's animation before a later sample replaces it.
            event.melee_confirmed = true;
        }
    }
}

} // namespace

bool EffectSession::holds_current(const EffectObservation& previous,
    const EffectObservation& next) const noexcept {
    if (previous.mode != EffectMode::gameplay) {
        return true;
    }
    const bool same_weapon = previous.weapon == next.weapon && previous.weapon_id == next.weapon_id;
    const auto previous_bow = gameplay_bow_phase(previous);
    if ((body_enabled_ || triggers_enabled_) && previous_bow != BowPhase::none &&
        (previous_bow != gameplay_bow_phase(next) || !same_weapon)) {
        return false;
    }
    if (triggers_enabled_ && previous.blocking &&
        (!next.blocking || previous.shield != next.shield)) {
        return false;
    }
    if (body_enabled_ && previous.casting && (!next.casting || !same_weapon)) {
        return false;
    }
    const bool heartbeat_cancelled = previous.in_combat &&
                                     previous.health.ratio < critical_health_exit &&
                                     (!next.in_combat || next.health.ratio >= critical_health_exit);
    return !body_enabled_ || !heartbeat_cancelled;
}

EffectSession::EffectSession(inputnext::adapters::OutputPort& output,
    const EffectSettings& settings) noexcept
    : body_enabled_(settings.body_enabled && settings.rumble_scale > 0.0F),
      triggers_enabled_(settings.triggers_enabled && settings.trigger_scale > 0.0F),
      enabled_(
          body_enabled_ || triggers_enabled_ ||
          (settings.led.enabled && (settings.led.health_enabled || settings.led.stealth_enabled))),
      menu_effects_enabled_(settings.menu_effects_enabled),
      gate_result_enabled_(
          body_enabled_ && settings.profiles[profile_index(EffectId::gate_closed)].enabled),
      policy_(settings),
      output_(output, *this, settings.controller_slot, body_enabled_) {
}

void EffectSession::invalidate_locked(MonoTime now, GateBaseline gate) noexcept {
    observation_.reset();
    size_ = 0;
    if (gate == GateBaseline::discard) {
        gate_counter_.reset();
    }
    earliest_sample_ = std::max(earliest_sample_, now);
    ++revision_;
    ++generation_;
}

void EffectSession::invalidate(MonoTime now) noexcept {
    const std::scoped_lock lock(ingress_);
    invalidate_locked(now);
}

void EffectSession::set_focus(bool focused, MonoTime now) noexcept {
    const std::scoped_lock lock(ingress_);
    if (focused_ != focused) {
        focused_ = focused;
        invalidate_locked(now);
    }
}

void EffectSession::stop(MonoTime now) noexcept {
    const std::scoped_lock lock(ingress_);
    stopped_ = true;
    invalidate_locked(now);
}

void EffectSession::change_mode_locked(const EffectObservation& previous,
    const EffectObservation& next) noexcept {
    ++mode_revision_;
    const auto end = inbox_.begin() + static_cast<std::ptrdiff_t>(size_);
    const auto keep = std::remove_if(inbox_.begin(), end, [&](const EffectEvent& event) {
        const bool next_mode = event.mode == next.mode && event.frame > previous.frame;
        const bool just_closed = next.mode == EffectMode::gameplay && event.mode == previous.mode &&
                                 menu_effect(event.kind, previous.mode) &&
                                 fresh(event.at, next.sampled_at);
        return !next_mode && !just_closed;
    });
    size_ = static_cast<std::size_t>(keep - inbox_.begin());
}

bool EffectSession::enqueue_menu_results_locked(const EffectObservation& previous,
    const EffectObservation& next) noexcept {
    if (!body_enabled_ || !menu_effects_enabled_ || previous.sampled_at <= event_floor_) {
        return true;
    }
    for (const auto kind : menu_results(previous, next)) {
        if (kind && !enqueue_locked({.kind = *kind,
                        .at = next.sampled_at,
                        .epoch = next.epoch,
                        .frame = next.frame,
                        .mode = previous.mode,
                        .actor_id = next.player_id})) {
            return false;
        }
    }
    return true;
}

bool EffectSession::enqueue_block_impact_locked(const EffectObservation& observation) noexcept {
    const bool was_block_impact =
        observation_ && observation_->mode == EffectMode::gameplay && observation_->block_hit;
    if (!body_enabled_ || observation.mode != EffectMode::gameplay || observation.frame == 0 ||
        observation.sampled_at <= event_floor_ || !observation.block_hit || was_block_impact) {
        return true;
    }
    return enqueue_locked({.kind = EffectEventKind::block_impact,
        .at = observation.sampled_at,
        .epoch = observation.epoch,
        .frame = observation.frame,
        .mode = observation.mode,
        .actor_id = observation.player_id,
        .target_id = observation.player_id});
}

void EffectSession::publish(EffectObservation observation) noexcept {
    const std::scoped_lock lock(ingress_);
    if (stopped_ || !focused_ || observation.sampled_at < earliest_sample_) {
        return;
    }
    if (observation_ && older_than(observation, *observation_)) {
        return;
    }
    if (observation.mode == EffectMode::loading) {
        gate_counter_.note_loading();
        if (observation_) {
            invalidate_locked(observation.sampled_at, GateBaseline::preserve);
        }
        return;
    }
    if (!usable(observation)) {
        if (observation_) {
            invalidate_locked(observation.sampled_at);
        } else {
            gate_counter_.reset();
        }
        return;
    }
    if (observation.mode != EffectMode::gameplay) {
        gate_counter_.reset();
    }
    if (observation_) {
        if (observation.epoch != observation_->epoch ||
            observation.player_id != observation_->player_id) {
            invalidate_locked(observation.sampled_at);
        } else if (!fresh(observation_->sampled_at, observation.sampled_at)) {
            invalidate_locked(observation.sampled_at, GateBaseline::preserve);
        } else {
            const auto previous = *observation_;
            if (observation.mode != previous.mode) {
                change_mode_locked(previous, observation);
            }
            preserve_unarmed_contact(inbox_, size_, previous);
            if (!enqueue_menu_results_locked(previous, observation)) {
                return;
            }
        }
    }
    if (!enqueue_block_impact_locked(observation)) {
        return;
    }
    preserve_unarmed_contact(inbox_, size_, observation);
    observation_ = observation;
}

bool EffectSession::push_event(EffectEvent event) noexcept {
    const std::scoped_lock lock(ingress_);
    if (!enabled_ || stopped_ || !focused_ || !observation_ || event.epoch != observation_->epoch ||
        event.at < earliest_sample_ || event.at <= event_floor_ ||
        !fresh(observation_->sampled_at, event.at)) {
        return false;
    }
    if (event.mode != EffectMode::gameplay &&
        (!menu_effects_enabled_ || !menu_effect(event.kind, event.mode))) {
        return false;
    }
    return enqueue_locked(event);
}

bool EffectSession::enqueue_locked(EffectEvent event) noexcept {
    if (size_ == inbox_.size()) {
        invalidate_locked(event.at);
        return false;
    }
    event.sequence = next_sequence_++;
    inbox_[size_++] = event;
    return true;
}

void EffectSession::discard_before_locked(MonoTime time) noexcept {
    if (time <= event_floor_) {
        return;
    }
    event_floor_ = time;
    ++generation_;
    const auto end = inbox_.begin() + static_cast<std::ptrdiff_t>(size_);
    const auto keep = std::remove_if(inbox_.begin(), end, [&](const EffectEvent& event) {
        return event.at <= event_floor_;
    });
    size_ = static_cast<std::size_t>(keep - inbox_.begin());
}

EffectSession::Attempt EffectSession::capture(MonoTime now) noexcept {
    const std::scoped_lock lock(ingress_);
    if (observation_ && observation_->sampled_at > now) {
        return {.observation = std::nullopt,
            .revision = revision_,
            .mode_revision = mode_revision_};
    }
    if (observation_ && now - observation_->sampled_at > effect_max_age) {
        invalidate_locked(now, GateBaseline::preserve);
    }
    return {.observation = observation_, .revision = revision_, .mode_revision = mode_revision_};
}

bool EffectSession::current_locked(const Attempt& attempt, MonoTime now) const noexcept {
    return focused_ && !stopped_ && attempt.observation && observation_ &&
           revision_ == attempt.revision && fresh(attempt.observation->sampled_at, now) &&
           fresh(observation_->sampled_at, now);
}

std::optional<std::size_t> EffectSession::prepare_frame(MonoTime now,
    FrameEvents& events) noexcept {
    const std::scoped_lock lock(ingress_);
    if (!current_locked(active_, now) || active_.mode_revision != mode_revision_ || !observation_) {
        return std::nullopt;
    }
    // xOBSE can publish during a runtime call. Re-read the snapshot and events together.
    active_.observation = observation_;
    const auto& observation = *observation_;
    std::size_t ready = 0;
    std::size_t pending = 0;
    for (std::size_t index = 0; index < size_; ++index) {
        const auto& event = inbox_[index];
        if (event.epoch != observation.epoch) {
            continue;
        }
        if (event.at > now || event.frame > observation.frame) {
            inbox_[pending++] = event;
        } else if (fresh(event.at, now) &&
                   (event.mode == observation.mode || (observation.mode == EffectMode::gameplay &&
                                                          menu_effect(event.kind, event.mode)))) {
            events[ready++] = event;
        }
    }
    size_ = pending;
    // Reconcile requires a sample taken after binding. A gate delta across loading
    // can play here only after the same controller connection has been confirmed.
    if (gate_result_enabled_ && observation.sampled_at > event_floor_ && ready < events.size() &&
        gate_counter_.observe(observation)) {
        events[ready++] = {.kind = EffectEventKind::gate_closed,
            .at = observation.sampled_at,
            .epoch = observation.epoch,
            .frame = observation.frame,
            .sequence = next_sequence_++,
            .mode = EffectMode::gameplay,
            .actor_id = observation.player_id};
    }
    return ready;
}

bool EffectSession::withdraw(inputnext::adapters::TickClock& clock) noexcept {
    const bool triggers = output_.withdraw_triggers();
    const bool leds = output_.withdraw_leds();
    const bool body = output_.clear_body(clock);
    return triggers && leds && body;
}

void EffectSession::tick(inputnext::adapters::TickClock clock) noexcept {
    if (!enabled_) {
        return;
    }
    const auto attempt = capture(clock.now());
    if (attempt.revision != processed_revision_) {
        processed_revision_ = attempt.revision;
        policy_.reset();
        close_pending_ = true;
    }
    if (close_pending_) {
        close_pending_ = !output_.close_and_drain(clock);
        if (close_pending_) {
            return;
        }
    }
    if (!attempt.observation) {
        output_.maintain_cleanup();
        return;
    }
    if (attempt.mode_revision != processed_mode_revision_) {
        policy_.reset_body();
        if (!withdraw(clock)) {
            output_.maintain_cleanup();
            return;
        }
        processed_mode_revision_ = attempt.mode_revision;
    }

    active_ = attempt;
    if (admitted(clock.now()) && output_.reconcile(attempt.observation->sampled_at, clock)) {
        const auto now = clock.now();
        FrameEvents events{};
        if (const auto count = prepare_frame(now, events); count && active_.observation) {
            const auto& observation = *active_.observation;
            const auto frame = policy_.update(observation,
                std::span<const EffectEvent>{events.data(), *count},
                now);
            // Trigger and rumble durations start at render time. PCM and LED intents
            // retain their source timestamps.
            static_cast<void>(output_.send(frame, now, clock));
        }
    }
    active_ = {};
}

bool EffectSession::drain(inputnext::adapters::TickClock clock) noexcept {
    return output_.close_and_drain(clock);
}

inputnext::adapters::DeliveryDiagnostics EffectSession::diagnostics() const noexcept {
    return output_.diagnostics();
}

bool EffectSession::admitted(MonoTime now) const noexcept {
    const std::scoped_lock lock(ingress_);
    return current_locked(active_, now);
}

inputnext::adapters::FrameRestriction EffectSession::frame_restriction(
    MonoTime /*now*/) const noexcept {
    const std::scoped_lock lock(ingress_);
    using inputnext::adapters::FrameRestriction;
    if (active_.mode_revision != mode_revision_ || !active_.observation || !observation_) {
        return FrameRestriction::neutral;
    }
    return holds_current(*active_.observation, *observation_) ? FrameRestriction::none
                                                              : FrameRestriction::neutral;
}

void EffectSession::binding_lost(input_next_status status) noexcept {
    policy_.reset();
    const std::scoped_lock lock(ingress_);
    if (status != INPUT_NEXT_STATUS_BUSY && status != INPUT_NEXT_STATUS_PENDING) {
        bound_target_.reset();
        gate_counter_.reset();
    }
    if (active_.observation) {
        discard_before_locked(active_.observation->sampled_at);
    }
}

void EffectSession::binding_changed(MonoTime started_at) noexcept {
    policy_.reset();
    const auto target = output_.bound_target();
    const std::scoped_lock lock(ingress_);
    if (!target || !bound_target_ || !same_target(*target, *bound_target_)) {
        gate_counter_.reset();
    }
    bound_target_ = target;
    discard_before_locked(started_at);
}

void EffectSession::body_rebound(MonoTime baseline_at) noexcept {
    policy_.reset_body();
    const std::scoped_lock lock(ingress_);
    gate_counter_.reset();
    discard_before_locked(baseline_at);
}

void EffectSession::admission_expired() noexcept {
    policy_.reset();
}

} // namespace oblivion_adapter
