#include <inputnext_adapters/output_session.hpp>

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <cstdint>

namespace inputnext::adapters {
OutputSession::OutputSession(OutputPort& output,
    DeliveryObserver& observer,
    std::uint32_t slot) noexcept
    : output_(output),
      observer_(observer),
      slot_(slot) {
}

OutputFrame OutputSession::apply_frame_restriction(OutputFrame frame, MonoTime now) noexcept {
    update_frame_restriction(now);
    return frame_neutral_ ? OutputFrame{} : frame;
}

void OutputSession::update_frame_restriction(MonoTime now) noexcept {
    if (!frame_neutral_ && observer_.frame_restriction(now) == FrameRestriction::neutral) {
        frame_neutral_ = true;
    }
}

bool OutputSession::refresh_send_frame(OutputFrame& frame, TickClock& clock) noexcept {
    const auto now = clock.now();
    if (!observer_.admitted(now)) {
        expire_admission(clock);
        return false;
    }
    frame = apply_frame_restriction(frame, now);
    return true;
}

bool OutputSession::neutralize_body_if_restricted(OutputFrame& frame,
    MonoTime sampled_at,
    TickClock& clock,
    bool& body_sent) noexcept {
    if (!frame_neutral_ || !body_owner_) {
        return true;
    }
    const bool neutralized =
        binding_ ? send_body(OutputFrame{}, sampled_at, clock) : clear_body(clock);
    if (!refresh_send_frame(frame, clock)) {
        return false;
    }
    body_sent = body_sent && neutralized;
    return true;
}

bool OutputSession::finish_trigger_delivery(OutputFrame& frame,
    const OutputFrame& original,
    MonoTime sampled_at,
    TickClock& clock,
    bool& body_sent) noexcept {
    if (!binding_) {
        return true;
    }

    const bool neutral_before_cleanup = frame_neutral_;
    bool cleanup_succeeded = sync_obsolete_triggers(*binding_, frame, original, sampled_at, clock);
    auto cleanup_status = cleanup_succeeded ? INPUT_NEXT_STATUS_OK : diagnostics_.last_status;
    if (!refresh_send_frame(frame, clock)) {
        return false;
    }
    if (!neutral_before_cleanup && frame_neutral_) {
        const bool body_neutralized =
            neutralize_body_if_restricted(frame, sampled_at, clock, body_sent);
        cleanup_succeeded = cleanup_succeeded && body_neutralized;
        if (!body_neutralized || !binding_) {
            return false;
        }
        const bool final_cleanup_succeeded =
            sync_obsolete_triggers(*binding_, frame, original, sampled_at, clock);
        if (!final_cleanup_succeeded && cleanup_status == INPUT_NEXT_STATUS_OK) {
            cleanup_status = diagnostics_.last_status;
        }
        cleanup_succeeded = final_cleanup_succeeded && cleanup_succeeded;
    }
    if (!cleanup_succeeded && cleanup_status != INPUT_NEXT_STATUS_OK &&
        diagnostics_.last_status == INPUT_NEXT_STATUS_OK) {
        diagnostics_.last_status = cleanup_status;
    }
    return cleanup_succeeded;
}

bool OutputSession::has_body_owner() const noexcept {
    return body_owner_.has_value();
}

void OutputSession::reset_attempt_history() noexcept {
    body_attempt_history_ = {};
    body_attempted_ = false;
    body_last_attempt_failed_ = false;
    last_body_attempt_at_ = MonoTime{};
}

void OutputSession::reset_accepted_fallback() noexcept {
    accepted_fallback_ = {};
}

void OutputSession::restore_trigger_failure(bool triggers_sent,
    input_next_status trigger_status) noexcept {
    if (!triggers_sent && trigger_status != INPUT_NEXT_STATUS_OK &&
        diagnostics_.last_status == INPUT_NEXT_STATUS_OK) {
        diagnostics_.last_status = trigger_status;
    }
}

void OutputSession::remember_first_failure(input_next_status& first_failure) const noexcept {
    if (first_failure == INPUT_NEXT_STATUS_OK && diagnostics_.last_status != INPUT_NEXT_STATUS_OK) {
        first_failure = diagnostics_.last_status;
    }
}

bool OutputSession::trigger_body_admission_open(bool triggers_sent, TickClock& clock) noexcept {
    if (triggers_sent || observer_.admitted(clock.now())) {
        return true;
    }
    if (!binding_ && body_owner_) {
        static_cast<void>(clear_body(clock));
    }
    return false;
}

bool OutputSession::deliver_body_frame(OutputFrame& frame,
    MonoTime sampled_at,
    TickClock& clock,
    bool& body_sent) noexcept {
    if (binding_) {
        body_sent = send_body(frame, sampled_at, clock);
        return refresh_send_frame(frame, clock) &&
               neutralize_body_if_restricted(frame, sampled_at, clock, body_sent);
    }
    if (!observer_.admitted(clock.now())) {
        expire_admission(clock);
        return false;
    }
    return frame_neutral_ ? neutralize_body_if_restricted(frame, sampled_at, clock, body_sent)
                          : !body_owner_ || clear_body(clock);
}

bool OutputSession::finish_body_frame(OutputFrame& frame,
    MonoTime sampled_at,
    TickClock& clock,
    bool& body_sent) noexcept {
    auto now = clock.now();
    const bool fallback_expired =
        accepted_fallback_.sample_valid && now >= accepted_fallback_.sampled_until;
    const bool pcm_expired = body_owner_ && body_owner_->cache.pcm_plan_accepted &&
                             !body_owner_->cache.neutral_stop_accepted &&
                             !body_program_active(frame.body, now);
    if (body_owner_ && (fallback_expired || pcm_expired)) {
        if (binding_) {
            body_sent = send_body(frame, sampled_at, clock) && body_sent;
        }
        if (!refresh_send_frame(frame, clock) ||
            !neutralize_body_if_restricted(frame, sampled_at, clock, body_sent)) {
            return false;
        }
        now = clock.now();
    }

    if (body_owner_ && now >= body_owner_->cache.retention_deadline) {
        static_cast<void>(clear_body(clock));
    }
    return refresh_send_frame(frame, clock) &&
           neutralize_body_if_restricted(frame, sampled_at, clock, body_sent);
}

bool OutputSession::deliver_send_body(OutputFrame& frame,
    MonoTime sampled_at,
    TickClock& clock,
    SendProgress& progress) noexcept {
    if (!deliver_body_frame(frame, sampled_at, clock, progress.body_sent)) {
        remember_first_failure(progress.first_failure);
        return false;
    }
    if (!finish_body_frame(frame, sampled_at, clock, progress.body_sent)) {
        remember_first_failure(progress.first_failure);
        return false;
    }
    return true;
}

bool OutputSession::finish_send_channels(OutputFrame& frame,
    const OutputFrame& original,
    MonoTime sampled_at,
    TickClock& clock,
    SendProgress& progress) noexcept {
    const bool cleanup_succeeded =
        finish_trigger_delivery(frame, original, sampled_at, clock, progress.body_sent);
    if (!cleanup_succeeded) {
        remember_first_failure(progress.first_failure);
    }

    const bool led_owner_before_cleanup =
        binding_ && (binding_->accepted_channels & INPUT_NEXT_EFFECTS_LEDS) != 0;
    const bool led_cleanup_succeeded = sync_obsolete_leds(original, clock);
    if (!led_cleanup_succeeded) {
        remember_first_failure(progress.first_failure);
    }

    // A synchronous LED clear may let a still-current trigger expire. One final trigger pass
    // handles that case only after the LED contribution was actually removed.
    bool final_trigger_cleanup = true;
    if (led_owner_before_cleanup && binding_ &&
        (binding_->accepted_channels & INPUT_NEXT_EFFECTS_LEDS) == 0) {
        final_trigger_cleanup =
            finish_trigger_delivery(frame, original, sampled_at, clock, progress.body_sent);
        if (!final_trigger_cleanup) {
            remember_first_failure(progress.first_failure);
        }
    }

    if (progress.first_failure != INPUT_NEXT_STATUS_OK) {
        diagnostics_.last_status = progress.first_failure;
    }
    return progress.triggers_sent && cleanup_succeeded && progress.leds_sent &&
           led_cleanup_succeeded && final_trigger_cleanup;
}

bool OutputSession::send(OutputFrame frame, MonoTime sampled_at, TickClock& clock) noexcept {
    advance_pending_cleanup();
    frame_neutral_ = false;
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return false;
    }
    const auto send_started_at = clock.now();
    if (!observer_.admitted(send_started_at)) {
        expire_admission(clock);
        return false;
    }
    if (!binding_->delivery_ready) {
        return false;
    }
    const auto original = frame;
    frame = apply_frame_restriction(frame, send_started_at);

    const bool triggers_sent = send_triggers(original, sampled_at, clock);
    const auto trigger_status = diagnostics_.last_status;
    SendProgress progress{};
    progress.triggers_sent = triggers_sent;
    if (!triggers_sent && trigger_status != INPUT_NEXT_STATUS_OK) {
        progress.first_failure = trigger_status;
    }
    if (!trigger_body_admission_open(triggers_sent, clock)) {
        restore_trigger_failure(triggers_sent, trigger_status);
        return false;
    }
    if (!refresh_send_frame(frame, clock)) {
        restore_trigger_failure(triggers_sent, trigger_status);
        return false;
    }

    const bool leds_sent = send_leds(original, clock);
    progress.leds_sent = leds_sent;
    if (!leds_sent) {
        remember_first_failure(progress.first_failure);
    }

    if (!deliver_send_body(frame, sampled_at, clock, progress)) {
        if (progress.first_failure != INPUT_NEXT_STATUS_OK) {
            diagnostics_.last_status = progress.first_failure;
        }
        return false;
    }
    return finish_send_channels(frame, original, sampled_at, clock, progress) && progress.body_sent;
}

DeliveryDiagnostics OutputSession::diagnostics() const noexcept {
    auto result = diagnostics_;
    result.body_owner_present = body_owner_.has_value();
    if (body_owner_) {
        result.accepted_body_route = body_owner_->cache.effective_route;
        result.body_fallback_reason = body_owner_->cache.fallback_reason;
    }
    return result;
}
} // namespace inputnext::adapters
