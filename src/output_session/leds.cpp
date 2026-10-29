#include <inputnext_adapters/output_session.hpp>

#include "internal.hpp"

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/runtime_client.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

namespace inputnext::adapters {
using namespace detail;

namespace {
constexpr std::uint32_t maximum_led_ttl_ms = 200;

struct CurrentLed {
    input_next_effects_led_v1 payload{};
    MonoTime source_at;
    MonoTime expires_at;
    std::uint32_t remaining_ttl_ms{};
};

bool valid_led_payload(const input_next_effects_led_v1& payload) noexcept {
    return (payload.lightbar_rgb & UINT32_C(0xff000000)) == 0 && payload.player_index <= 4 &&
           payload.microphone_led <= 1 && payload.reserved == 0;
}

bool equal_led_payload(const input_next_effects_led_v1& left,
    const input_next_effects_led_v1& right) noexcept {
    return left.lightbar_rgb == right.lightbar_rgb && left.player_index == right.player_index &&
           left.microphone_led == right.microphone_led && left.reserved == right.reserved;
}

MonoTime led_deadline(MonoTime source_at, std::uint32_t ttl_ms) noexcept {
    const auto lifetime = std::chrono::milliseconds{ttl_ms};
    const auto latest_start = MonoTime::max() - lifetime;
    return source_at > latest_start ? MonoTime::max() : source_at + lifetime;
}

bool current_led(const std::optional<LedIntent>& intent,
    const input_next_effects_capabilities_v1& capabilities,
    MonoTime binding_started_at,
    MonoTime now,
    CurrentLed& current) noexcept {
    if (!intent || !valid_led_payload(intent->payload) || intent->ttl_ms == 0 ||
        intent->ttl_ms > maximum_led_ttl_ms || intent->source_at <= binding_started_at ||
        intent->source_at > now || (capabilities.supported_channels & led_mask) == 0 ||
        (capabilities.feature_flags & INPUT_NEXT_EFFECTS_CAP_LIGHTBAR) == 0) {
        return false;
    }

    const auto expires_at = led_deadline(intent->source_at, intent->ttl_ms);
    if (expires_at <= now) {
        return false;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(expires_at - now);
    if (remaining.count() <= 0) {
        return false;
    }
    current = CurrentLed{
        .payload = intent->payload,
        .source_at = intent->source_at,
        .expires_at = expires_at,
        .remaining_ttl_ms =
            static_cast<std::uint32_t>(std::min<std::chrono::milliseconds::rep>(remaining.count(),
                static_cast<std::chrono::milliseconds::rep>(maximum_led_ttl_ms))),
    };
    return true;
}

bool cache_refresh_window_open(MonoTime now, MonoTime sent_at) noexcept {
    return now >= sent_at && now - sent_at < refresh_interval;
}

bool cache_lives_past_tick(MonoTime now, MonoTime expires_at) noexcept {
    return expires_at > now && expires_at - now > tick_interval;
}
} // namespace

bool OutputSession::withdraw_leds() noexcept {
    return withdraw(led_mask);
}

bool OutputSession::sync_obsolete_leds(const OutputFrame& frame,
    TickClock& clock,
    bool allow_expired_replacement) noexcept {
    for (;;) {
        if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
            return true;
        }

        const auto now = clock.now();
        if (!observer_.admitted(now)) {
            expire_admission(clock);
            return false;
        }
        update_frame_restriction(now);
        if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
            return true;
        }

        CurrentLed desired{};
        const bool desired_active =
            !frame_neutral_ &&
            current_led(frame.leds, binding_->capabilities, binding_->started_at, now, desired);
        const bool accepted = (binding_->accepted_channels & led_mask) != 0;
        const bool expired_owner = accepted && now >= binding_->led.expires_at;
        if (!accepted || (desired_active && (!expired_owner || allow_expired_replacement))) {
            return true;
        }

        const bool cleared = withdraw(led_mask);
        const auto returned_at = clock.now();
        if (!observer_.admitted(returned_at)) {
            expire_admission(clock);
            return false;
        }
        update_frame_restriction(returned_at);
        if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
            return true;
        }
        if (!cleared) {
            return false;
        }
    }
}

bool OutputSession::send_leds(const OutputFrame& frame, TickClock& clock) noexcept {
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return true;
    }
    if (!sync_obsolete_leds(frame, clock, true)) {
        return false;
    }
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return true;
    }

    const auto now = clock.now();
    if (!observer_.admitted(now)) {
        expire_admission(clock);
        return false;
    }
    update_frame_restriction(now);
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return true;
    }

    CurrentLed current{};
    if (frame_neutral_ ||
        !current_led(frame.leds, binding_->capabilities, binding_->started_at, now, current)) {
        return true;
    }

    const auto& binding = *binding_;
    const bool accepted = (binding.accepted_channels & led_mask) != 0;
    const bool payload_unchanged =
        accepted && equal_led_payload(binding.led.request.leds, current.payload);
    const bool owner_deadline_not_shortened = current.expires_at >= binding.led.expires_at;
    const bool cache_current = payload_unchanged && owner_deadline_not_shortened &&
                               cache_lives_past_tick(now, binding.led.expires_at) &&
                               cache_refresh_window_open(now, binding.led.sent_at);
    if (cache_current) {
        return true;
    }

    return submit_led(frame,
        current.payload,
        current.source_at,
        now,
        current.expires_at,
        current.remaining_ttl_ms,
        clock);
}

bool OutputSession::submit_led(const OutputFrame& frame,
    const input_next_effects_led_v1& payload,
    MonoTime source_at,
    MonoTime submitted_at,
    MonoTime expires_at,
    std::uint32_t ttl_ms,
    TickClock& clock) noexcept {
    if (!binding_ || binding_->context_state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
        return true;
    }
    auto& binding = *binding_;
    const auto sequence = consume_mutation_sequence(binding);
    if (sequence == 0) {
        handle_binding_failure(INPUT_NEXT_STATUS_STALE_SEQUENCE);
        return false;
    }
    auto request =
        make_request(binding.context, sequence, led_mask, std::chrono::milliseconds{ttl_ms});
    request.leds = payload;
    input_next_effects_receipt_v1 receipt{};
    receipt.struct_size = static_cast<std::uint32_t>(sizeof(receipt));
    const auto status = output_.submit(request, receipt);
    if (status != INPUT_NEXT_STATUS_OK) {
        handle_binding_failure(status);
        const auto returned_at = clock.now();
        if (!observer_.admitted(returned_at)) {
            expire_admission(clock);
            return false;
        }
        update_frame_restriction(returned_at);
        const auto failure = diagnostics_.last_status;
        if (binding_ && binding_->context_state == INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
            static_cast<void>(sync_obsolete_leds(frame, clock));
        }
        if (diagnostics_.last_status == INPUT_NEXT_STATUS_OK) {
            diagnostics_.last_status = failure;
        }
        return false;
    }
    if (!valid_effect_receipt(request, receipt)) {
        handle_binding_failure(INPUT_NEXT_STATUS_INVALID_ARGUMENT);
        const auto returned_at = clock.now();
        if (!observer_.admitted(returned_at)) {
            expire_admission(clock);
        } else {
            update_frame_restriction(returned_at);
        }
        return false;
    }

    binding.led = LedCache{
        .request = request,
        .revision = receipt.channel_revisions[3],
        .source_at = source_at,
        .sent_at = submitted_at,
        .expires_at = expires_at,
    };
    binding.accepted_channels |= led_mask;
    diagnostics_.last_status = status;

    const auto returned_at = clock.now();
    if (!observer_.admitted(returned_at)) {
        expire_admission(clock);
        return false;
    }
    update_frame_restriction(returned_at);
    return sync_obsolete_leds(frame, clock);
}
} // namespace inputnext::adapters
