#include "skyrim_adapter/detail/effect_mixer.hpp"

#include "skyrim_adapter/model.hpp"

#include "effect_recipes.hpp"

#include <input_next_effects.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace skyrim_adapter::detail {
namespace {
constexpr int held_priority = 10;

float unit_value(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    return std::clamp(value, 0.0F, 1.0F);
}

std::uint32_t remaining_ms(MonoTime now, MonoTime started_at, std::uint32_t duration_ms) noexcept {
    if (now < started_at) {
        return duration_ms;
    }
    const std::uint32_t elapsed = elapsed_ms(now, started_at);
    return elapsed >= duration_ms ? 0U : duration_ms - elapsed;
}

RumbleRecipe to_recipe(const RumbleEnvelope& envelope) noexcept {
    return {
        .shape = static_cast<RumbleShape>(envelope.shape),
        .low = envelope.low,
        .high = envelope.high,
        .second_low = envelope.second_low,
        .second_high = envelope.second_high,
        .started_at = envelope.started_at,
        .duration_ms = envelope.duration_ms,
        .peak_ms = envelope.peak_ms,
        .gap_ms = envelope.gap_ms,
        .second_duration_ms = envelope.second_duration_ms,
        .priority = envelope.priority,
        .present = envelope.valid,
    };
}

RumbleFallback sample_rumble_recipe(const RumbleRecipe& recipe, MonoTime now) noexcept {
    if (!recipe.present || recipe.started_at > now) {
        return {};
    }

    const std::uint32_t age = elapsed_ms(now, recipe.started_at);
    if (age >= recipe.duration_ms) {
        return {};
    }

    float low = recipe.low;
    float high = recipe.high;
    std::uint32_t boundary = recipe.duration_ms;
    switch (recipe.shape) {
    case RumbleShape::steady:
        break;
    case RumbleShape::fade:
        if (age < recipe.peak_ms) {
            boundary = recipe.peak_ms;
        } else if (recipe.duration_ms > recipe.peak_ms) {
            const float remaining = static_cast<float>(recipe.duration_ms - age) /
                                    static_cast<float>(recipe.duration_ms - recipe.peak_ms);
            const float tail_scale = 0.60F * std::clamp(remaining, 0.0F, 1.0F);
            low *= tail_scale;
            high *= tail_scale;
        }
        break;
    case RumbleShape::double_pulse:
        if (age < recipe.peak_ms) {
            boundary = recipe.peak_ms;
        } else if (age < recipe.peak_ms + recipe.gap_ms) {
            low = 0.0F;
            high = 0.0F;
            boundary = recipe.peak_ms + recipe.gap_ms;
        } else {
            low = recipe.second_low;
            high = recipe.second_high;
            boundary = recipe.peak_ms + recipe.gap_ms + recipe.second_duration_ms;
        }
        break;
    case RumbleShape::double_tap:
        if (age < recipe.peak_ms) {
            boundary = recipe.peak_ms;
        } else if (age < recipe.peak_ms + recipe.gap_ms) {
            low = 0.0F;
            high = 0.0F;
            boundary = recipe.peak_ms + recipe.gap_ms;
        } else {
            boundary = recipe.peak_ms + recipe.gap_ms + recipe.second_duration_ms;
        }
        break;
    }

    if (age >= boundary || (low <= 0.0F && high <= 0.0F)) {
        return {};
    }
    return {
        .low = unit_value(low),
        .high = unit_value(high),
        .ttl_ms = boundary - age,
        .present = true,
    };
}

} // namespace

void EffectMixer::reset() noexcept {
    left_trigger_overlay_ = {};
    right_trigger_overlay_ = {};
    rumble_envelope_ = {};
}

void EffectMixer::reset_rumble() noexcept {
    rumble_envelope_ = {};
}

void EffectMixer::offer_trigger(TriggerEnvelope& choice,
    const input_next_effects_trigger_v1& payload,
    MonoTime started_at,
    std::uint32_t duration_ms,
    int priority) noexcept {
    if (duration_ms != 0 && (!choice.valid || priority > choice.priority)) {
        choice = {
            .payload = payload,
            .started_at = started_at,
            .duration_ms = duration_ms,
            .priority = priority,
            .valid = true,
        };
    }
}

void EffectMixer::offer_rumble(RumbleEnvelope& choice,
    std::uint8_t shape,
    float low,
    float high,
    float second_low,
    float second_high,
    MonoTime started_at,
    std::uint32_t duration_ms,
    std::uint32_t peak_ms,
    std::uint32_t gap_ms,
    std::uint32_t second_duration_ms,
    int priority) noexcept {
    if (duration_ms == 0) {
        return;
    }
    if (!choice.valid || priority > choice.priority ||
        (priority == choice.priority && started_at > choice.started_at)) {
        choice = {
            .shape = shape,
            .low = unit_value(low),
            .high = unit_value(high),
            .second_low = unit_value(second_low),
            .second_high = unit_value(second_high),
            .started_at = started_at,
            .duration_ms = duration_ms,
            .peak_ms = peak_ms,
            .gap_ms = gap_ms,
            .second_duration_ms = second_duration_ms,
            .priority = priority,
            .valid = true,
        };
    } else if (priority == choice.priority && started_at == choice.started_at) {
        choice.low = std::max(choice.low, unit_value(low));
        choice.high = std::max(choice.high, unit_value(high));
        choice.second_low = std::max(choice.second_low, unit_value(second_low));
        choice.second_high = std::max(choice.second_high, unit_value(second_high));
        choice.duration_ms = std::max(choice.duration_ms, duration_ms);
    }
}

EffectFrame EffectMixer::finish(MonoTime now,
    const EffectRequests& requests,
    const BodyPlan& body,
    RumbleFallback bow_fallback,
    float trigger_scale,
    float rumble_scale) noexcept {
    retain_step_envelopes(now, requests);
    EffectFrame frame{};
    frame.body = body;
    sample_trigger_intents(now, requests, frame);
    const RumbleFallback rumble = sample_rumble_intent(now, requests, bow_fallback);
    scale_step_outputs(frame, rumble, {.trigger = trigger_scale, .rumble = rumble_scale});
    return frame;
}

void EffectMixer::retain_step_envelopes(MonoTime now, const EffectRequests& requests) noexcept {
    const auto retain_trigger = [now](TriggerEnvelope& current,
                                    const TriggerEnvelope& request) noexcept {
        const bool active = current.valid && current.started_at <= now &&
                            remaining_ms(now, current.started_at, current.duration_ms) > 0;
        if (request.valid && (!active || request.priority >= current.priority)) {
            current = request;
        } else if (!active) {
            current = {};
        }
    };
    retain_trigger(left_trigger_overlay_, requests.left_request);
    retain_trigger(right_trigger_overlay_, requests.right_request);

    const bool rumble_active =
        rumble_envelope_.valid && rumble_envelope_.started_at <= now &&
        remaining_ms(now, rumble_envelope_.started_at, rumble_envelope_.duration_ms) > 0;
    const RumbleEnvelope& rumble_request = requests.rumble_request;
    if (rumble_request.valid &&
        (!rumble_active || rumble_request.priority >= rumble_envelope_.priority)) {
        RumbleEnvelope retained = rumble_request;
        if (rumble_active && rumble_request.priority == rumble_envelope_.priority &&
            rumble_envelope_.started_at == rumble_request.started_at) {
            retained.low = std::max(retained.low, rumble_envelope_.low);
            retained.high = std::max(retained.high, rumble_envelope_.high);
        }
        rumble_envelope_ = retained;
    } else if (!rumble_active) {
        rumble_envelope_ = {};
    }
}

void EffectMixer::sample_trigger_intents(MonoTime now,
    const EffectRequests& requests,
    EffectFrame& frame) const noexcept {
    const auto sample_trigger =
        [now](const TriggerEnvelope& envelope) noexcept -> std::optional<TriggerIntent> {
        if (!envelope.valid || envelope.started_at > now) {
            return std::nullopt;
        }
        const std::uint32_t ttl_ms = remaining_ms(now, envelope.started_at, envelope.duration_ms);
        if (ttl_ms == 0) {
            return std::nullopt;
        }
        return TriggerIntent{.payload = envelope.payload, .ttl_ms = ttl_ms};
    };
    frame.left = sample_trigger(left_trigger_overlay_);
    frame.right = sample_trigger(right_trigger_overlay_);

    struct TriggerLayers {
        const TriggerEnvelope& held;
        const TriggerEnvelope& overlay;
    };

    const auto apply_held_trigger = [&sample_trigger](TriggerLayers layers,
                                        std::optional<TriggerIntent>& output) noexcept {
        const bool overlay_active = layers.overlay.valid && layers.overlay.priority > held_priority;
        if (output && overlay_active) {
            return;
        }
        output = sample_trigger(layers.held);
    };
    apply_held_trigger({.held = requests.left_held, .overlay = left_trigger_overlay_}, frame.left);
    apply_held_trigger({.held = requests.right_held, .overlay = right_trigger_overlay_},
        frame.right);
}

RumbleFallback EffectMixer::sample_rumble_intent(MonoTime now,
    const EffectRequests& requests,
    RumbleFallback bow_fallback) const noexcept {
    const RumbleFallback retained = sample_rumble_recipe(to_recipe(rumble_envelope_), now);
    const RumbleFallback held = sample_rumble_recipe(to_recipe(requests.held_rumble_request), now);
    RumbleFallback rumble{
        .low = retained.low,
        .high = retained.high,
        .ttl_ms = retained.ttl_ms,
        .present = retained.present,
    };
    if (held.present && (!rumble_envelope_.valid || rumble_envelope_.priority <= held_priority)) {
        rumble.low = std::max(rumble.low, held.low);
        rumble.high = std::max(rumble.high, held.high);
        rumble.ttl_ms = held.ttl_ms;
        rumble.present = true;
    }

    if (bow_fallback.present &&
        (!rumble_envelope_.valid || rumble_envelope_.priority <= held_priority)) {
        rumble.low = std::max(rumble.low, bow_fallback.low);
        rumble.high = std::max(rumble.high, bow_fallback.high);
        rumble.ttl_ms = bow_fallback.ttl_ms;
        rumble.present = true;
    }
    return rumble;
}

void EffectMixer::scale_step_outputs(EffectFrame& frame,
    RumbleFallback rumble,
    OutputScales scales) noexcept {
    if (scales.trigger > 0.0F) {
        const auto scale_trigger = [scales](std::optional<TriggerIntent>& intent) noexcept {
            if (!intent) {
                return;
            }
            intent->payload.strength = unit_value(intent->payload.strength * scales.trigger);
            if (intent->payload.mode != INPUT_NEXT_EFFECTS_TRIGGER_OFF &&
                intent->payload.strength <= 0.0F) {
                intent.reset();
            }
        };
        scale_trigger(frame.left);
        scale_trigger(frame.right);
    } else {
        frame.left.reset();
        frame.right.reset();
    }

    if (scales.rumble > 0.0F && rumble.present) {
        const float low = unit_value(rumble.low * scales.rumble);
        const float high = unit_value(rumble.high * scales.rumble);
        if (low > 0.0F || high > 0.0F) {
            frame.rumble = RumbleIntent{
                .payload = {.low_frequency = low, .high_frequency = high},
                .ttl_ms = std::min(rumble.ttl_ms, hold_ttl_ms),
            };
        }
    }
}

} // namespace skyrim_adapter::detail
