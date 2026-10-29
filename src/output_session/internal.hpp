#pragma once

#include <inputnext_adapters/output_frame.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace inputnext::adapters::detail {
inline constexpr auto refresh_interval = std::chrono::milliseconds{100};
inline constexpr auto tick_interval = std::chrono::milliseconds{20};
inline constexpr auto body_lease_ttl = std::chrono::milliseconds{200};
inline constexpr std::uint32_t hold_ttl_ms = 200;
inline constexpr std::array<std::uint32_t, 2> trigger_channels{
    INPUT_NEXT_EFFECTS_LEFT_TRIGGER,
    INPUT_NEXT_EFFECTS_RIGHT_TRIGGER,
};
inline constexpr std::uint32_t trigger_mask =
    INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER;
inline constexpr std::uint32_t led_mask = INPUT_NEXT_EFFECTS_LEDS;
inline constexpr std::uint32_t independent_channel_mask = trigger_mask | led_mask;

inline bool equal_target(const input_next_device_target_v1& left,
    const input_next_device_target_v1& right) noexcept {
    return left.struct_size == right.struct_size && left.reserved == right.reserved &&
           left.runtime_id[0] == right.runtime_id[0] && left.runtime_id[1] == right.runtime_id[1] &&
           left.device_key == right.device_key && left.connection_id == right.connection_id;
}

inline bool equal_context(const input_next_effects_context_v1& left,
    const input_next_effects_context_v1& right) noexcept {
    return left.struct_size == right.struct_size && left.reserved == right.reserved &&
           left.runtime_id[0] == right.runtime_id[0] && left.runtime_id[1] == right.runtime_id[1] &&
           left.id == right.id;
}

inline bool valid_device_target(const input_next_device_target_v1& target) noexcept {
    return target.struct_size == sizeof(target) && target.reserved == 0 &&
           (target.runtime_id[0] != 0 || target.runtime_id[1] != 0) && target.device_key != 0 &&
           target.connection_id != 0;
}

inline bool valid_context(const input_next_effects_context_v1& context,
    const input_next_device_target_v1& target) noexcept {
    return context.struct_size == sizeof(context) && context.reserved == 0 && context.id != 0 &&
           context.runtime_id[0] == target.runtime_id[0] &&
           context.runtime_id[1] == target.runtime_id[1];
}

inline bool valid_effect_receipt(const input_next_effects_request_v1& request,
    const input_next_effects_receipt_v1& receipt) noexcept {
    if (receipt.struct_size != sizeof(receipt) || receipt.accepted_mask != request.channel_mask ||
        !equal_context(receipt.context, request.context) ||
        receipt.mutation_sequence != request.mutation_sequence ||
        receipt.publication_revision == 0) {
        return false;
    }
    constexpr std::array<std::uint32_t, 4> channels{
        INPUT_NEXT_EFFECTS_LEFT_TRIGGER,
        INPUT_NEXT_EFFECTS_RIGHT_TRIGGER,
        INPUT_NEXT_EFFECTS_RUMBLE,
        INPUT_NEXT_EFFECTS_LEDS,
    };
    for (std::size_t index = 0; index < channels.size(); ++index) {
        if ((request.channel_mask & channels[index]) != 0 &&
            receipt.channel_revisions[index] == 0) {
            return false;
        }
    }
    return true;
}

inline bool valid_close_receipt(const input_next_effects_context_v1& context,
    const input_next_device_target_v1& target,
    const input_next_effects_close_receipt_v1& receipt) noexcept {
    if (!valid_device_target(target) || !valid_context(context, target) ||
        receipt.struct_size != sizeof(receipt) || !equal_context(receipt.context, context) ||
        !equal_target(receipt.target, target) || receipt.close_request_id != context.id) {
        return false;
    }
    switch (receipt.disposition) {
    case INPUT_NEXT_EFFECTS_CLOSE_NORMAL:
    case INPUT_NEXT_EFFECTS_CLOSE_CONNECTION_RETIRED:
        return receipt.publication_revision != 0;
    case INPUT_NEXT_EFFECTS_CLOSE_CANCELLED_BEFORE_CREATION:
        return receipt.publication_revision == 0;
    default:
        return false;
    }
}

inline std::uint64_t advertised_audio_instance(
    const input_next_effects_capabilities_v1& capabilities) noexcept {
    if ((capabilities.feature_flags & INPUT_NEXT_EFFECTS_CAP_HD_READY) == 0) {
        return 0;
    }
    return capabilities.audio_instance_id;
}

inline bool supports_body_lease(const input_next_effects_capabilities_v1& capabilities) noexcept {
    return (capabilities.supported_channels & INPUT_NEXT_EFFECTS_RUMBLE) != 0 &&
           (capabilities.feature_flags & INPUT_NEXT_EFFECTS_CAP_BODY_LEASE) != 0;
}

inline bool unit(float value) noexcept {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

inline bool valid(const TriggerIntent& intent) noexcept {
    const auto& payload = intent.payload;
    const auto valid_shape = [&payload] noexcept {
        return payload.mode <= INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION && unit(payload.start) &&
               unit(payload.end) && payload.end >= payload.start && unit(payload.strength);
    };
    const auto valid_frequency = [&payload] noexcept {
        return std::isfinite(payload.frequency) && payload.frequency >= 0.0F &&
               payload.frequency <= 255.0F;
    };
    return intent.ttl_ms > 0 && intent.ttl_ms <= hold_ttl_ms && valid_shape() && valid_frequency();
}

inline bool valid(const RumbleIntent& intent) noexcept {
    return intent.ttl_ms > 0 && intent.ttl_ms <= hold_ttl_ms &&
           unit(intent.payload.low_frequency) && unit(intent.payload.high_frequency);
}

inline bool equal(const input_next_effects_trigger_v1& left,
    const input_next_effects_trigger_v1& right) noexcept {
    return left.mode == right.mode && left.start == right.start && left.end == right.end &&
           left.strength == right.strength && left.frequency == right.frequency;
}

inline bool equal_payload(const input_next_effects_request_v1& left,
    const input_next_effects_request_v1& right) noexcept {
    return equal(left.left_trigger, right.left_trigger) &&
           equal(left.right_trigger, right.right_trigger);
}

inline bool equal(const input_next_effects_rumble_v1& left,
    const input_next_effects_rumble_v1& right) noexcept {
    return left.low_frequency == right.low_frequency && left.high_frequency == right.high_frequency;
}

inline bool valid_body_receipt(const input_next_effects_body_request_v1& request,
    const input_next_device_target_v1& target,
    const input_next_effects_body_receipt_v1& receipt) noexcept {
    if (receipt.struct_size != sizeof(receipt) || receipt.reserved != 0) {
        return false;
    }
    if (!equal_context(receipt.context, request.context) || !equal_target(receipt.target, target) ||
        receipt.audio_instance_id != request.audio_instance_id ||
        receipt.sequence != request.sequence || receipt.publication_revision == 0) {
        return false;
    }
    if (receipt.lease_id == 0 || (request.lease_id != 0 && receipt.lease_id != request.lease_id)) {
        return false;
    }
    if (receipt.effective_route > INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD ||
        receipt.fallback_reason > INPUT_NEXT_EFFECTS_BODY_FALLBACK_PREEMPTED) {
        return false;
    }
    if (receipt.effective_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD) {
        return request.audio_instance_id != 0 &&
               receipt.fallback_reason == INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE;
    }
    if (request.route_preference == INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC) {
        return receipt.fallback_reason == INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE;
    }
    return receipt.fallback_reason != INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE;
}

inline bool age_ttl(std::uint32_t& ttl_ms, MonoTime sampled_at, MonoTime now) noexcept {
    if (now <= sampled_at) {
        return ttl_ms != 0;
    }

    const auto lifetime = std::chrono::milliseconds{ttl_ms};
    const auto elapsed = now - sampled_at;
    if (elapsed >= lifetime) {
        ttl_ms = 0;
        return false;
    }

    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(lifetime - elapsed);
    if (remaining.count() <= 0) {
        ttl_ms = 0;
        return false;
    }
    ttl_ms = static_cast<std::uint32_t>(remaining.count());
    return true;
}

inline OutputFrame age_frame(OutputFrame frame, MonoTime sampled_at, MonoTime now) noexcept {
    if (frame.left && !age_ttl(frame.left->ttl_ms, sampled_at, now)) {
        frame.left.reset();
    }
    if (frame.right && !age_ttl(frame.right->ttl_ms, sampled_at, now)) {
        frame.right.reset();
    }
    if (frame.rumble && !age_ttl(frame.rumble->ttl_ms, sampled_at, now)) {
        frame.rumble.reset();
    }
    return frame;
}
} // namespace inputnext::adapters::detail
