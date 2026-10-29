#include <inputnext_adapters/output_session.hpp>

#include "internal.hpp"

#include <inputnext_adapters/body_program.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <cstdint>
#include <optional>

namespace inputnext::adapters {
using namespace detail;

std::optional<MonoTime> OutputSession::binding_started_at() const noexcept {
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return std::nullopt;
    }
    return binding_->started_at;
}

bool OutputSession::bound() const noexcept {
    return binding_ && binding_->context_state == INPUT_NEXT_EFFECTS_CONTEXT_OPEN;
}

bool OutputSession::reconcile(std::optional<MonoTime> observation_at, TickClock& clock) noexcept {
    if (binding_) {
        binding_->delivery_ready = false;
    }
    advance_pending_cleanup();
    if (binding_ && binding_->context_state == INPUT_NEXT_EFFECTS_CONTEXT_REVOKED) {
        if (!close_binding(INPUT_NEXT_STATUS_REVOKED, true)) {
            return false;
        }
    }
    if (slot_ > 3U) {
        observation_at_ = observation_at;
        diagnostics_.last_status = INPUT_NEXT_STATUS_INVALID_ARGUMENT;
        return false;
    }

    input_next_device_selector_v1 selector{};
    selector.struct_size = static_cast<std::uint32_t>(sizeof(selector));
    selector.target.struct_size = static_cast<std::uint32_t>(sizeof(selector.target));
    selector.kind = INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT;
    selector.xinput_slot = slot_;
    input_next_device_target_v1 target{};
    input_next_effects_capabilities_v1 capabilities{};
    target.struct_size = sizeof(target);
    capabilities.struct_size = sizeof(capabilities);
    const auto status = output_.resolve_target(selector, target, capabilities);
    if (status != INPUT_NEXT_STATUS_OK) {
        if (status != INPUT_NEXT_STATUS_BUSY) {
            observation_at_ = observation_at;
        }
        handle_binding_failure(status);
        return false;
    }
    observation_at_ = observation_at;
    if (!valid_device_target(target) || capabilities.struct_size != sizeof(capabilities)) {
        handle_binding_failure(INPUT_NEXT_STATUS_INVALID_ARGUMENT);
        return false;
    }
    diagnostics_.last_status = INPUT_NEXT_STATUS_OK;

    const auto current_audio_instance = advertised_audio_instance(capabilities);
    const bool current_body_support = supports_body_lease(capabilities);
    if (!binding_ || !equal_target(binding_->target, target)) {
        if (binding_ && !close_binding(INPUT_NEXT_STATUS_STALE_TARGET, true)) {
            return false;
        }
        bind_new_target(target, capabilities, current_audio_instance, current_body_support, clock);
        return false;
    }

    if (!refresh_context()) {
        return false;
    }
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return false;
    }

    reconcile_audio_instance(*binding_, current_audio_instance, clock);
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return false;
    }
    reconcile_body_support(*binding_, current_body_support, clock);
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return false;
    }
    binding_->capabilities = capabilities;
    binding_->delivery_ready = observation_at_ && *observation_at_ > binding_->started_at;
    return binding_->delivery_ready;
}

void OutputSession::bind_new_target(const input_next_device_target_v1& target,
    const input_next_effects_capabilities_v1& capabilities,
    std::uint64_t audio_instance,
    bool body_supported,
    TickClock& clock) noexcept {
    const auto now = clock.now();
    input_next_effects_open_v1 request{};
    request.struct_size = static_cast<std::uint32_t>(sizeof(request));
    request.target = target;
    input_next_effects_context_v1 context{};
    context.struct_size = static_cast<std::uint32_t>(sizeof(context));
    const auto status = output_.open_context(request, context);
    if (status != INPUT_NEXT_STATUS_OK) {
        diagnostics_.last_status = status;
        observer_.binding_lost(status);
        return;
    }
    if (!valid_context(context, target)) {
        diagnostics_.last_status = INPUT_NEXT_STATUS_INVALID_ARGUMENT;
        observer_.binding_lost(INPUT_NEXT_STATUS_INVALID_ARGUMENT);
        return;
    }

    binding_.emplace();
    binding_->target = target;
    binding_->context = context;
    binding_->capabilities = capabilities;
    binding_->started_at = now;
    binding_->body_baseline_at = now;
    binding_->observed_audio_instance_id = audio_instance;
    binding_->body_lease_supported = body_supported;
    binding_->context_state = INPUT_NEXT_EFFECTS_CONTEXT_OPEN;
    observer_.binding_changed(now);
    diagnostics_.last_status = INPUT_NEXT_STATUS_OK;
    observation_at_.reset();
    reset_attempt_history();
}

void OutputSession::reconcile_audio_instance(Binding& binding,
    std::uint64_t audio_instance,
    TickClock& clock) noexcept {
    if (binding.observed_audio_instance_id == audio_instance) {
        return;
    }

    const auto old_context = binding.context;
    binding.observed_audio_instance_id = audio_instance;
    binding.body_baseline_at = clock.now();
    binding.body_baseline_required = true;
    observer_.body_rebound(binding.body_baseline_at);
    reset_attempt_history();
    if (body_owner_ && equal_context(body_owner_->context, old_context)) {
        static_cast<void>(clear_body(clock));
    }
}

void OutputSession::reconcile_body_support(Binding& binding,
    bool body_supported,
    TickClock& clock) noexcept {
    if (binding.body_lease_supported == body_supported) {
        return;
    }

    const auto old_context = binding.context;
    if (body_supported) {
        binding.body_baseline_at = clock.now();
        binding.body_baseline_required = true;
        observer_.body_rebound(binding.body_baseline_at);
    }
    reset_attempt_history();
    binding.body_lease_supported = body_supported;
    if (!body_supported && body_owner_ && equal_context(body_owner_->context, old_context)) {
        static_cast<void>(clear_body(clock));
    }
}

bool OutputSession::refresh_context() noexcept {
    if (!binding_) {
        return false;
    }
    input_next_effects_context_info_v1 info{};
    info.struct_size = static_cast<std::uint32_t>(sizeof(info));
    info.context.struct_size = static_cast<std::uint32_t>(sizeof(info.context));
    const auto status = output_.query_context(binding_->context, info);
    diagnostics_.last_status = status;
    if (status == INPUT_NEXT_STATUS_PENDING || status == INPUT_NEXT_STATUS_BUSY) {
        return false;
    }
    if (status != INPUT_NEXT_STATUS_OK && status != INPUT_NEXT_STATUS_REVOKED) {
        handle_binding_failure(status);
        return false;
    }
    if (info.struct_size != sizeof(info) || !equal_context(info.context, binding_->context) ||
        info.state < INPUT_NEXT_EFFECTS_CONTEXT_ATTACHING ||
        info.state > INPUT_NEXT_EFFECTS_CONTEXT_REVOKED) {
        handle_binding_failure(INPUT_NEXT_STATUS_INVALID_ARGUMENT);
        return false;
    }
    if (status == INPUT_NEXT_STATUS_REVOKED || info.state == INPUT_NEXT_EFFECTS_CONTEXT_REVOKED) {
        handle_binding_failure(INPUT_NEXT_STATUS_REVOKED);
        return false;
    }

    binding_->context_state = info.state;
    if (info.state == INPUT_NEXT_EFFECTS_CONTEXT_ATTACHING) {
        diagnostics_.last_status = INPUT_NEXT_STATUS_PENDING;
        return false;
    }
    diagnostics_.last_status = INPUT_NEXT_STATUS_OK;
    return true;
}

void OutputSession::handle_binding_failure(input_next_status status) noexcept {
    diagnostics_.last_status = status;
    if (status == INPUT_NEXT_STATUS_BUSY) {
        return;
    }
    static_cast<void>(close_binding(status, false));
    observer_.binding_lost(status);
    reset_attempt_history();
}

} // namespace inputnext::adapters
