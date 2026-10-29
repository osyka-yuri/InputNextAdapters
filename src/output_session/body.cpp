#include <inputnext_adapters/output_session.hpp>

#include "internal.hpp"

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <chrono>
#include <cstdint>
#include <limits>

namespace inputnext::adapters {
using namespace detail;

namespace {
[[nodiscard]] bool is_binding_failure_status(input_next_status status) noexcept {
    switch (status) {
    case INPUT_NEXT_STATUS_PENDING:
    case INPUT_NEXT_STATUS_TIMED_OUT_UNKNOWN:
    case INPUT_NEXT_STATUS_REVOKED:
    case INPUT_NEXT_STATUS_STALE_CONTEXT:
    case INPUT_NEXT_STATUS_STALE_TARGET:
        return true;
    default:
        return false;
    }
}
} // namespace

bool OutputSession::clear_body(TickClock& clock) noexcept {
    if (!body_owner_) {
        return true;
    }
    auto now = clock.now();
    const auto& cache = body_owner_->cache;
    if (now >= cache.retention_deadline) {
        body_owner_.reset();
        reset_accepted_fallback();
        return true;
    }
    if (cache.next_sequence == 0) {
        return false;
    }

    input_next_effects_body_clear_v1 request{};
    request.struct_size = sizeof(request);
    request.context = body_owner_->context;
    request.lease_id = cache.lease_id;
    request.audio_instance_id = cache.audio_instance_id;
    request.sequence = cache.next_sequence;
    const auto status = output_.clear_body(request);
    now = clock.now();
    diagnostics_.last_status = status;
    if (status != INPUT_NEXT_STATUS_OK && status != INPUT_NEXT_STATUS_STALE_LEASE) {
        body_attempted_ = true;
        body_last_attempt_failed_ = true;
        last_body_attempt_at_ = now;
        if (status != INPUT_NEXT_STATUS_BUSY) {
            handle_binding_failure(status);
        }
        return false;
    }
    body_last_attempt_failed_ = false;
    body_owner_.reset();
    reset_accepted_fallback();
    reset_attempt_history();
    return true;
}

OutputSession::BodyReadiness OutputSession::prepare_body_owner(TickClock& clock) noexcept {
    if (!binding_) {
        return BodyReadiness::failed;
    }

    auto& binding = *binding_;
    const auto current_audio_instance = advertised_audio_instance(binding.capabilities);
    if (body_owner_ && (!equal_context(body_owner_->context, binding.context) ||
                           body_owner_->cache.audio_instance_id != current_audio_instance)) {
        if (!clear_body(clock)) {
            return BodyReadiness::failed;
        }
    }
    if (!supports_body_lease(binding.capabilities)) {
        return BodyReadiness::skipped;
    }
    if (binding.body_baseline_required) {
        if (!observation_at_ || *observation_at_ <= binding.body_baseline_at) {
            return BodyReadiness::skipped;
        }
        binding.body_baseline_required = false;
    }

    if (body_owner_ && clock.now() >= body_owner_->cache.retention_deadline) {
        body_owner_.reset();
        reset_accepted_fallback();
    }

    auto& attempts = body_attempt_history_;
    if (!attempts.scoped || !equal_context(attempts.context, binding.context) ||
        attempts.audio_instance_id != current_audio_instance) {
        attempts = {};
        attempts.scoped = true;
        attempts.context = binding.context;
        attempts.audio_instance_id = current_audio_instance;
    }
    return BodyReadiness::ready;
}

void OutputSession::sample_body_fallback(const Binding& binding,
    const OutputFrame& frame,
    MonoTime now,
    BodySubmissionPlan& plan) noexcept {
    if (!frame.rumble || !valid(*frame.rumble)) {
        return;
    }

    plan.fallback = frame.rumble->payload;
    if (!(binding.capabilities.feature_flags & INPUT_NEXT_EFFECTS_CAP_LOW_RUMBLE)) {
        plan.fallback.low_frequency = 0.0F;
    }
    if (!(binding.capabilities.feature_flags & INPUT_NEXT_EFFECTS_CAP_HIGH_RUMBLE)) {
        plan.fallback.high_frequency = 0.0F;
    }
    plan.fallback_until = now + std::chrono::milliseconds{frame.rumble->ttl_ms};
    plan.fallback_valid = true;
}

bool OutputSession::accepted_fallback_changed(const Binding& binding,
    MonoTime now,
    const BodySubmissionPlan& plan) const noexcept {
    const bool in_scope =
        accepted_fallback_.scoped && accepted_fallback_.has_sample &&
        equal_context(accepted_fallback_.context, binding.context) &&
        accepted_fallback_.audio_instance_id == advertised_audio_instance(binding.capabilities);
    const bool expired =
        in_scope && accepted_fallback_.sample_valid && now >= accepted_fallback_.sampled_until;
    return !in_scope || accepted_fallback_.sample_valid != plan.fallback_valid ||
           !equal(accepted_fallback_.payload, plan.fallback) || expired;
}

bool OutputSession::attempted_fallback_changed(MonoTime now,
    const BodySubmissionPlan& plan) const noexcept {
    const auto& attempts = body_attempt_history_;
    return !attempts.has_fallback_sample || attempts.fallback_sample_valid != plan.fallback_valid ||
           !equal(attempts.fallback_payload, plan.fallback) ||
           (plan.fallback_valid && attempts.fallback_sample_valid &&
               now >= attempts.fallback_sampled_until);
}

bool OutputSession::configure_body_pcm_command(const Binding& binding,
    BodySubmissionPlan& plan,
    const BodyCache& cache,
    MonoTime now,
    bool active,
    bool held) noexcept {
    const auto& attempts = body_attempt_history_;
    const bool changed =
        active && (!attempts.has_plan || !same_body_program_identity(attempts.plan, plan.frame));
    const bool held_renewal =
        held && (!attempts.has_replace_at || now - attempts.last_replace_at >= refresh_interval);
    const bool failure_cooldown_elapsed =
        !body_last_attempt_failed_ || now - last_body_attempt_at_ >= refresh_interval;
    const bool window_renewal = cache.pcm_installed && cache.installed_pcm_plan_valid &&
                                same_body_program_identity(cache.installed_pcm_plan, plan.frame) &&
                                cache.installed_window_end <= now + refresh_interval &&
                                failure_cooldown_elapsed;

    auto& request = plan.request;
    if (active) {
        if (changed || held_renewal || window_renewal) {
            request.pcm_command = INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE;
        }
    } else if (!plan.acquiring) {
        const bool pcm_needs_stop = cache.pcm_plan_accepted && !cache.neutral_stop_accepted;
        const auto current_audio_instance = advertised_audio_instance(binding.capabilities);
        const bool hd_recovery_due =
            current_audio_instance != 0 &&
            cache.effective_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC &&
            now - cache.last_lease_submit_at >= refresh_interval && failure_cooldown_elapsed;
        if (pcm_needs_stop || hd_recovery_due) {
            request.pcm_command = INPUT_NEXT_EFFECTS_BODY_PCM_STOP;
        }
    }

    if (!plan.acquiring && request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
        cache.effective_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC) {
        request.route_preference = INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC;
    }
    return changed || held_renewal || window_renewal;
}

bool OutputSession::should_wait_for_body_submission(const BodySubmissionPlan& plan,
    const BodyCache& cache,
    MonoTime now,
    bool fallback_changed,
    bool attempted_fallback_changed,
    bool semantic_retry) const noexcept {
    const bool due =
        plan.acquiring || !body_owner_ || now - cache.last_lease_submit_at >= refresh_interval;
    const bool failed_request_cooldown = body_last_attempt_failed_ && body_attempted_ &&
                                         now - last_body_attempt_at_ < refresh_interval;
    const bool fallback_retry = body_owner_ && fallback_changed && attempted_fallback_changed;
    const bool waiting_for_retry = failed_request_cooldown && !semantic_retry && !fallback_retry;
    const bool fallback_update = body_owner_ && fallback_changed;
    const bool unchanged_lease =
        !due && plan.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP && !fallback_update;
    return waiting_for_retry || unchanged_lease;
}

bool OutputSession::plan_body_submission(OutputFrame frame,
    MonoTime sampled_at,
    TickClock& clock,
    BodySubmissionPlan& plan) noexcept {
    const auto now = clock.now();
    if (!observer_.admitted(now)) {
        return false;
    }
    frame = apply_frame_restriction(frame, now);
    frame = age_frame(frame, sampled_at, now);
    plan.frame = frame.body;
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return false;
    }
    const auto& binding = *binding_;
    const auto current_audio_instance = advertised_audio_instance(binding.capabilities);
    sample_body_fallback(binding, frame, now, plan);
    const bool fallback_changed = accepted_fallback_changed(binding, now, plan);
    const bool attempted_changed = attempted_fallback_changed(now, plan);

    const bool hd_ready = current_audio_instance != 0;
    const bool active = hd_ready && body_program_active(plan.frame, now);
    const bool held = hd_ready && body_program_has_held(plan.frame, now);
    BodyCache empty_cache{};
    const BodyCache* cache = body_owner_ ? &body_owner_->cache : &empty_cache;
    plan.acquiring = !body_owner_;

    auto& request = plan.request;
    request.struct_size = sizeof(request);
    request.ttl_ms = static_cast<std::uint32_t>(body_lease_ttl.count());
    request.route_preference = INPUT_NEXT_EFFECTS_BODY_ROUTE_PREFER_HD;
    request.pcm_command = INPUT_NEXT_EFFECTS_BODY_PCM_KEEP;
    request.context = binding.context;
    request.lease_id = plan.acquiring ? 0 : cache->lease_id;
    request.audio_instance_id = current_audio_instance;
    request.sequence = cache->next_sequence;
    request.fallback_rumble = plan.fallback;

    const bool semantic_retry =
        configure_body_pcm_command(binding, plan, *cache, now, active, held);
    if (should_wait_for_body_submission(plan,
            *cache,
            now,
            fallback_changed,
            attempted_changed,
            semantic_retry)) {
        return true;
    }
    if (cache->next_sequence == 0) {
        return false;
    }

    plan.submit = true;
    return true;
}

bool OutputSession::prepare_body_pcm(BodySubmissionPlan& plan, MonoTime now) noexcept {
    auto& request = plan.request;
    request.pcm = {};
    request.pcm.struct_size = static_cast<std::uint32_t>(sizeof(request.pcm));
    if (request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
        !body_program_active(plan.frame, now)) {
        if (body_owner_ && body_owner_->cache.pcm_plan_accepted &&
            !body_owner_->cache.neutral_stop_accepted) {
            request.pcm_command = INPUT_NEXT_EFFECTS_BODY_PCM_STOP;
        } else {
            request.pcm_command = INPUT_NEXT_EFFECTS_BODY_PCM_KEEP;
            if (body_owner_ &&
                body_owner_->cache.effective_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC) {
                request.route_preference = INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC;
            }
        }
    }
    if (request.pcm_command != INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE) {
        return true;
    }

    const auto window = build_body_pcm(plan.frame, now, body_pcm_scratch_);
    if (window.frame_count == 0) {
        return false;
    }
    request.pcm.samples = body_pcm_scratch_.data();
    request.pcm.frame_count = window.frame_count;
    request.pcm.channels = 2;
    request.pcm.sample_rate = body_pcm_sample_rate;
    request.pcm.gain = 1.0F;
    return true;
}

void OutputSession::record_body_attempt(const BodySubmissionPlan& plan, MonoTime now) noexcept {
    auto& attempts = body_attempt_history_;
    attempts.fallback_payload = plan.fallback;
    attempts.fallback_sampled_until = plan.fallback_until;
    attempts.has_fallback_sample = true;
    attempts.fallback_sample_valid = plan.fallback_valid;
    if (plan.request.pcm_command != INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE) {
        return;
    }

    attempts.plan = plan.frame;
    attempts.last_replace_at = now;
    attempts.has_plan = true;
    attempts.has_replace_at = true;
}

void OutputSession::record_accepted_body_submission(const BodySubmissionPlan& plan,
    const input_next_effects_body_receipt_v1& receipt,
    BodyCache& accepted,
    const BodySubmissionTimes& times) noexcept {
    const auto& request = plan.request;
    accepted.lease_id = receipt.lease_id;
    accepted.audio_instance_id = receipt.audio_instance_id;
    accepted.next_sequence =
        request.sequence == std::numeric_limits<std::uint64_t>::max() ? 0 : request.sequence + 1;
    accepted.last_lease_submit_at = times.started_at;
    // Retain the cleanup identity through the latest possible native expiry.
    accepted.retention_deadline = times.returned_at + std::chrono::milliseconds{request.ttl_ms};
    accepted.effective_route = receipt.effective_route;
    accepted.fallback_reason = receipt.fallback_reason;
    accepted_fallback_ = {
        .context = request.context,
        .audio_instance_id = receipt.audio_instance_id,
        .payload = plan.fallback,
        .sampled_until = plan.fallback_until,
        .scoped = true,
        .has_sample = true,
        .sample_valid = plan.fallback_valid,
    };

    if (request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE) {
        accepted.pcm_plan_accepted = true;
        accepted.pcm_installed = receipt.effective_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
        accepted.neutral_stop_accepted = false;
        accepted.installed_window_end =
            accepted.pcm_installed ? times.started_at + body_lease_ttl : MonoTime{};
        accepted.installed_pcm_plan = plan.frame;
        accepted.installed_pcm_plan_valid = true;
    } else if (request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP) {
        accepted.pcm_plan_accepted = false;
        accepted.pcm_installed = false;
        accepted.neutral_stop_accepted = true;
        accepted.installed_window_end = MonoTime{};
        accepted.installed_pcm_plan = {};
        accepted.installed_pcm_plan_valid = false;
    } else if (receipt.effective_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC) {
        accepted.pcm_installed = false;
        accepted.installed_window_end = MonoTime{};
        accepted.installed_pcm_plan = {};
        accepted.installed_pcm_plan_valid = false;
    }
}

OutputSession::NeutralResolution OutputSession::resolve_neutral_submission(BodySubmissionPlan& plan,
    MonoTime sampled_at,
    TickClock& clock) noexcept {
    bool neutral_plan = false;
    for (;;) {
        const auto now = clock.now();
        if (!observer_.admitted(now)) {
            return NeutralResolution::failed;
        }
        update_frame_restriction(now);
        if (!frame_neutral_ || neutral_plan) {
            break;
        }

        BodySubmissionPlan restricted{};
        if (!plan_body_submission(OutputFrame{}, sampled_at, clock, restricted)) {
            return NeutralResolution::failed;
        }
        plan = restricted;
        neutral_plan = true;
        if (!plan.submit) {
            return NeutralResolution::complete;
        }
        const auto prepare_now = clock.now();
        if (!observer_.admitted(prepare_now) || !prepare_body_pcm(plan, prepare_now)) {
            return NeutralResolution::failed;
        }
    }
    return NeutralResolution::proceed;
}

bool OutputSession::submit_body(BodySubmissionPlan& plan,
    MonoTime sampled_at,
    TickClock& clock) noexcept {
    auto now = clock.now();
    if (!observer_.admitted(now)) {
        return false;
    }
    if (!plan.acquiring && !body_owner_) {
        return false;
    }

    if (!prepare_body_pcm(plan, now)) {
        return false;
    }

    const auto resolution = resolve_neutral_submission(plan, sampled_at, clock);
    if (resolution == NeutralResolution::failed) {
        return false;
    }
    if (resolution == NeutralResolution::complete) {
        return true;
    }

    now = clock.now();
    if (!observer_.admitted(now)) {
        return false;
    }
    if (plan.fallback_valid && now >= plan.fallback_until) {
        plan.fallback = {};
        plan.fallback_until = MonoTime{};
        plan.fallback_valid = false;
        plan.request.fallback_rumble = {};
    }

    record_body_attempt(plan, now);
    body_attempted_ = true;
    last_body_attempt_at_ = now;
    input_next_effects_body_receipt_v1 receipt{};
    receipt.struct_size = sizeof(receipt);
    const auto status = output_.submit_body(plan.request, receipt);
    diagnostics_.last_status = status;
    if (status != INPUT_NEXT_STATUS_OK) {
        update_frame_restriction(clock.now());
        body_last_attempt_failed_ = true;
        if (is_binding_failure_status(status)) {
            handle_binding_failure(status);
        } else if (status == INPUT_NEXT_STATUS_STALE_LEASE) {
            body_owner_.reset();
            reset_accepted_fallback();
        }
        return false;
    }
    if (!binding_ || !valid_body_receipt(plan.request, binding_->target, receipt)) {
        update_frame_restriction(clock.now());
        diagnostics_.last_status = INPUT_NEXT_STATUS_INVALID_ARGUMENT;
        body_last_attempt_failed_ = true;
        // Do not create an owner from an untrusted receipt or issue a compensating clear.
        // Revoke the exact context through reserved cleanup; its receipt, not a guessed body
        // lease identity, settles the possibly accepted operation.
        handle_binding_failure(INPUT_NEXT_STATUS_INVALID_ARGUMENT);
        return false;
    }
    const auto target = binding_->target;
    body_last_attempt_failed_ = false;
    const BodySubmissionTimes times{.started_at = now, .returned_at = clock.now()};
    if (plan.acquiring) {
        body_owner_.emplace().target = target;
        body_owner_->context = plan.request.context;
    }
    if (!body_owner_) {
        return false;
    }
    record_accepted_body_submission(plan, receipt, body_owner_->cache, times);
    update_frame_restriction(times.returned_at);
    return true;
}

bool OutputSession::send_body(OutputFrame frame, MonoTime sampled_at, TickClock& clock) noexcept {
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return false;
    }

    const auto readiness = prepare_body_owner(clock);
    if (readiness == BodyReadiness::failed) {
        return false;
    }
    if (readiness == BodyReadiness::skipped) {
        return true;
    }

    BodySubmissionPlan plan{};
    if (!plan_body_submission(frame, sampled_at, clock, plan)) {
        return false;
    }
    if (!plan.submit) {
        return true;
    }
    return submit_body(plan, sampled_at, clock);
}
} // namespace inputnext::adapters
