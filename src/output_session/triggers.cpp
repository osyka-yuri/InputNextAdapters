#include <inputnext_adapters/output_session.hpp>

#include "internal.hpp"

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/runtime_client.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace inputnext::adapters {
using namespace detail;

namespace {
enum class TriggerSide : std::uint8_t {
    left,
    right,
};

bool retain_supported_trigger(std::optional<TriggerIntent>& intent,
    const input_next_effects_capabilities_v1& capabilities,
    TriggerSide side) noexcept {
    if (!intent) {
        return false;
    }

    const auto channel = side == TriggerSide::left ? INPUT_NEXT_EFFECTS_LEFT_TRIGGER
                                                   : INPUT_NEXT_EFFECTS_RIGHT_TRIGGER;
    const auto feature = side == TriggerSide::left ? INPUT_NEXT_EFFECTS_CAP_LEFT_TRIGGER
                                                   : INPUT_NEXT_EFFECTS_CAP_RIGHT_TRIGGER;

    const bool channel_supported = (capabilities.supported_channels & channel) != 0;
    const bool feature_supported = (capabilities.feature_flags & feature) != 0;
    if (!valid(*intent) || !channel_supported || !feature_supported) {
        intent.reset();
        return false;
    }
    return true;
}

std::uint32_t supported_trigger_channels(OutputFrame& frame,
    const OutputFrame& original,
    MonoTime sampled_at,
    MonoTime now,
    const input_next_effects_capabilities_v1& capabilities) noexcept {
    frame = age_frame(original, sampled_at, now);
    std::uint32_t channels = 0;
    if (retain_supported_trigger(frame.left, capabilities, TriggerSide::left)) {
        channels |= INPUT_NEXT_EFFECTS_LEFT_TRIGGER;
    }
    if (retain_supported_trigger(frame.right, capabilities, TriggerSide::right)) {
        channels |= INPUT_NEXT_EFFECTS_RIGHT_TRIGGER;
    }
    return channels;
}
} // namespace

std::uint64_t OutputSession::consume_mutation_sequence(Binding& binding) noexcept {
    const auto sequence = binding.next_mutation_sequence;
    if (sequence == 0) {
        return 0;
    }
    binding.next_mutation_sequence =
        sequence == std::numeric_limits<std::uint64_t>::max() ? 0 : sequence + 1;
    return sequence;
}

bool OutputSession::withdraw(std::uint32_t channels) noexcept {
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return true;
    }
    auto& binding = *binding_;
    const auto accepted = channels & binding.accepted_channels & independent_channel_mask;
    if (accepted == 0) {
        return true;
    }

    const auto sequence = consume_mutation_sequence(binding);
    if (sequence == 0) {
        handle_binding_failure(INPUT_NEXT_STATUS_STALE_SEQUENCE);
        return false;
    }
    input_next_effects_clear_v1 request{};
    request.struct_size = static_cast<std::uint32_t>(sizeof(request));
    request.channel_mask = accepted;
    request.context = binding.context;
    request.mutation_sequence = sequence;
    for (std::size_t index = 0; index < binding.channels.size(); ++index) {
        const auto channel =
            index == 0 ? INPUT_NEXT_EFFECTS_LEFT_TRIGGER : INPUT_NEXT_EFFECTS_RIGHT_TRIGGER;
        if ((accepted & channel) != 0) {
            request.channel_revisions[index] = binding.channels[index].revision;
        }
    }
    if ((accepted & led_mask) != 0) {
        request.channel_revisions[3] = binding.led.revision;
    }
    const auto status = output_.clear(request);
    if (status != INPUT_NEXT_STATUS_OK) {
        handle_binding_failure(status);
        return false;
    }

    diagnostics_.last_status = status;
    binding.accepted_channels &= ~accepted;
    if ((accepted & led_mask) != 0) {
        binding.led = {};
    }
    return true;
}

bool OutputSession::withdraw_triggers() noexcept {
    return withdraw(trigger_mask);
}

bool OutputSession::sync_obsolete_triggers(Binding& binding,
    OutputFrame& frame,
    const OutputFrame& original,
    MonoTime sampled_at,
    TickClock& clock) noexcept {
    for (;;) {
        const auto now = clock.now();
        if (!observer_.admitted(now)) {
            expire_admission(clock);
            return false;
        }
        const auto effective_original = apply_frame_restriction(original, now);
        const auto desired = supported_trigger_channels(frame,
            effective_original,
            sampled_at,
            now,
            binding.capabilities);
        const auto obsolete = binding.accepted_channels & trigger_mask & ~desired;
        if (obsolete == 0) {
            return true;
        }
        if (!withdraw(obsolete)) {
            return false;
        }
    }
}

OutputSession::TriggerSubmissionPlan OutputSession::plan_trigger_submissions(Binding& binding,
    OutputFrame& frame,
    const OutputFrame& original,
    MonoTime sampled_at,
    MonoTime now,
    std::uint32_t processed_channels) noexcept {
    TriggerSubmissionPlan plan{};
    const auto effective_original = apply_frame_restriction(original, now);
    const auto desired = supported_trigger_channels(frame,
        effective_original,
        sampled_at,
        now,
        binding.capabilities);

    for (std::size_t index = 0; index < trigger_channels.size(); ++index) {
        const auto channel = trigger_channels[index];
        if ((processed_channels & channel) != 0 || (desired & channel) == 0) {
            continue;
        }

        const auto& intent_opt = index == 0 ? frame.left : frame.right;
        if (!intent_opt) {
            continue;
        }
        const auto& intent = *intent_opt;
        auto& request = plan.channel_requests[index];
        request =
            make_request(binding.context, 0, channel, std::chrono::milliseconds{intent.ttl_ms});
        if (index == 0) {
            request.left_trigger = intent.payload;
        } else {
            request.right_trigger = intent.payload;
        }

        const auto& cache = binding.channels[index];
        const bool already_accepted = (binding.accepted_channels & channel) != 0;
        const bool cache_current = already_accepted && equal_payload(cache.request, request) &&
                                   cache.expires_at > now + tick_interval &&
                                   now - cache.sent_at < refresh_interval;
        if (!cache_current) {
            plan.pending_channels |= channel;
        }
    }
    return plan;
}

bool OutputSession::submit_trigger_channels(Binding& binding,
    const std::array<input_next_effects_request_v1, 2>& channel_requests,
    std::uint32_t channels,
    MonoTime submitted_at,
    TickClock& clock) noexcept {
    const auto left = (channels & INPUT_NEXT_EFFECTS_LEFT_TRIGGER) != 0;
    const auto right = (channels & INPUT_NEXT_EFFECTS_RIGHT_TRIGGER) != 0;
    const auto ttl_ms = left ? channel_requests[0].ttl_ms : channel_requests[1].ttl_ms;
    const auto sequence = consume_mutation_sequence(binding);
    if (sequence == 0) {
        handle_binding_failure(INPUT_NEXT_STATUS_STALE_SEQUENCE);
        return false;
    }
    auto request =
        make_request(binding.context, sequence, channels, std::chrono::milliseconds{ttl_ms});
    if (left) {
        request.left_trigger = channel_requests[0].left_trigger;
    }
    if (right) {
        request.right_trigger = channel_requests[1].right_trigger;
    }
    input_next_effects_receipt_v1 receipt{};
    receipt.struct_size = static_cast<std::uint32_t>(sizeof(receipt));
    const auto status = output_.submit(request, receipt);
    if (status != INPUT_NEXT_STATUS_OK) {
        handle_binding_failure(status);
        return false;
    }
    if (!valid_effect_receipt(request, receipt)) {
        handle_binding_failure(INPUT_NEXT_STATUS_INVALID_ARGUMENT);
        return false;
    }
    diagnostics_.last_status = status;
    for (std::size_t index = 0; index < trigger_channels.size(); ++index) {
        if ((channels & trigger_channels[index]) == 0) {
            continue;
        }
        binding.channels[index] = ChannelCache{
            .request = channel_requests[index],
            .revision = receipt.channel_revisions[index],
            .sent_at = submitted_at,
            .expires_at = submitted_at + std::chrono::milliseconds{channel_requests[index].ttl_ms},
        };
    }
    binding.accepted_channels |= channels;
    const auto completed_at = clock.now();
    if (!observer_.admitted(completed_at)) {
        expire_admission(clock);
        return false;
    }
    update_frame_restriction(completed_at);
    return true;
}

void OutputSession::record_trigger_failure(TriggerDeliveryProgress& progress) const noexcept {
    progress.all_submissions_succeeded = false;
    if (progress.first_failure == INPUT_NEXT_STATUS_OK &&
        diagnostics_.last_status != INPUT_NEXT_STATUS_OK) {
        progress.first_failure = diagnostics_.last_status;
    }
}

void OutputSession::restore_trigger_failure(const TriggerDeliveryProgress& progress) noexcept {
    if (progress.first_failure != INPUT_NEXT_STATUS_OK &&
        diagnostics_.last_status == INPUT_NEXT_STATUS_OK) {
        diagnostics_.last_status = progress.first_failure;
    }
}

bool OutputSession::process_trigger_channel(Binding& binding,
    OutputFrame& frame,
    const OutputFrame& original,
    MonoTime sampled_at,
    TickClock& clock,
    std::size_t index,
    TriggerDeliveryProgress& progress) noexcept {
    const auto channel = trigger_channels[index];
    if ((progress.processed_channels & channel) != 0) {
        return true;
    }
    if (!sync_obsolete_triggers(binding, frame, original, sampled_at, clock)) {
        record_trigger_failure(progress);
        if (!binding_ || !observer_.admitted(clock.now())) {
            return false;
        }
    }

    const auto now = clock.now();
    if (!observer_.admitted(now)) {
        expire_admission(clock);
        return false;
    }
    const auto plan = plan_trigger_submissions(binding,
        frame,
        original,
        sampled_at,
        now,
        progress.processed_channels);
    progress.processed_channels |= channel;
    if ((plan.pending_channels & channel) == 0) {
        return true;
    }

    std::uint32_t channels = channel;
    const auto other_index = 1U - index;
    const auto other_channel = trigger_channels[other_index];
    if ((plan.pending_channels & other_channel) != 0 &&
        plan.channel_requests[other_index].ttl_ms == plan.channel_requests[index].ttl_ms) {
        channels |= other_channel;
    }
    progress.processed_channels |= channels;
    if (!submit_trigger_channels(binding, plan.channel_requests, channels, now, clock)) {
        record_trigger_failure(progress);
        if (!binding_ || !observer_.admitted(clock.now())) {
            return false;
        }
    }
    return true;
}

bool OutputSession::send_triggers(OutputFrame frame,
    MonoTime sampled_at,
    TickClock& clock) noexcept {
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return false;
    }
    const OutputFrame original = frame;
    auto& binding = *binding_;
    TriggerDeliveryProgress progress{};
    for (std::size_t index = 0; index < trigger_channels.size(); ++index) {
        if (!process_trigger_channel(binding,
                frame,
                original,
                sampled_at,
                clock,
                index,
                progress)) {
            return false;
        }
    }
    const bool cleanup_succeeded =
        sync_obsolete_triggers(binding, frame, original, sampled_at, clock);
    if (!cleanup_succeeded) {
        record_trigger_failure(progress);
    }
    restore_trigger_failure(progress);
    return progress.all_submissions_succeeded && cleanup_succeeded;
}
} // namespace inputnext::adapters
