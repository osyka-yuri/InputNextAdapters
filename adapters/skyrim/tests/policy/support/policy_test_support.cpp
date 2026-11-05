#include "support/policy_test_support.hpp"
#include "test_support.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <input_next_effects.h>
#include <optional>
#include <skyrim_adapter/model.hpp>

namespace skyrim_adapter::policy_test {

MonoTime at(std::int64_t milliseconds) {
    return MonoTime{} + std::chrono::milliseconds{milliseconds};
}

Observation observation(MonoTime sampled_at) {
    Observation result{};
    result.sampled_at = sampled_at;
    result.player_id = 1;
    result.available = true;
    result.alive = true;
    result.focused = true;
    result.menus_clear = true;
    result.weapon_drawn = true;
    result.in_combat = false;
    return result;
}

Observation world_observation(MonoTime sampled_at) {
    Observation result = observation(sampled_at);
    WorldObservation world{};
    world.stamina_ratio = 0.5F;
    world.location = WorldLocation{
        .cell_id = 10,
        .worldspace_id = 20,
        .x = 0.0F,
        .y = 0.0F,
        .z = 0.0F,
    };
    result.world = world;
    return result;
}

WorldObservation& world(Observation& obs) {
    return skyrim_adapter::test::require_world(obs);
}

Action action(EventKind kind, Hand hand, MonoTime observed_at, std::uint64_t sequence) {
    Action result{};
    result.event.kind = kind;
    result.event.hand = hand;
    result.event.actor_id = 1;
    result.event.source_id = 42;
    result.event.sequence = sequence;
    result.event.observed_at = observed_at;
    return result;
}

bool finite_legal(const EffectFrame& frame) {
    const auto legal_trigger = [](const std::optional<TriggerIntent>& intent) {
        if (!intent) {
            return true;
        }
        const auto& payload = intent->payload;
        return payload.mode <= INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION &&
               std::isfinite(payload.start) && std::isfinite(payload.end) &&
               std::isfinite(payload.strength) && std::isfinite(payload.frequency) &&
               payload.start >= 0.0F && payload.start <= 1.0F && payload.end >= 0.0F &&
               payload.end <= 1.0F && payload.strength >= 0.0F && payload.strength <= 1.0F &&
               payload.frequency >= 0.0F && payload.frequency <= 255.0F && intent->ttl_ms > 0 &&
               intent->ttl_ms <= INPUT_NEXT_EFFECTS_MAX_TTL_MS;
    };
    const bool legal_rumble =
        !frame.rumble ||
        (std::isfinite(frame.rumble->payload.low_frequency) &&
            std::isfinite(frame.rumble->payload.high_frequency) &&
            frame.rumble->payload.low_frequency >= 0.0F &&
            frame.rumble->payload.low_frequency <= 1.0F &&
            frame.rumble->payload.high_frequency >= 0.0F &&
            frame.rumble->payload.high_frequency <= 1.0F && frame.rumble->ttl_ms > 0 &&
            frame.rumble->ttl_ms <= INPUT_NEXT_EFFECTS_MAX_TTL_MS);
    const bool legal_led =
        !frame.leds ||
        (frame.leds->payload.lightbar_rgb <= 0x00FFFFFFU &&
            frame.leds->payload.player_index == 0U && frame.leds->payload.microphone_led == 0U &&
            frame.leds->payload.reserved == 0U && frame.leds->ttl_ms > 0U &&
            frame.leds->ttl_ms <= 200U);
    return legal_trigger(frame.left) && legal_trigger(frame.right) && legal_rumble && legal_led;
}

} // namespace skyrim_adapter::policy_test
