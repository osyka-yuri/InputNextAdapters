#pragma once

#include <inputnext_adapters/body_program.hpp>

#include <input_next_effects.h>

#include <cstdint>
#include <optional>

namespace inputnext::adapters {
struct TriggerIntent {
    input_next_effects_trigger_v1 payload{};
    std::uint32_t ttl_ms{};
};

struct RumbleIntent {
    input_next_effects_rumble_v1 payload{};
    std::uint32_t ttl_ms{};
};

struct LedIntent {
    input_next_effects_led_v1 payload{};
    MonoTime source_at;
    std::uint32_t ttl_ms{};
};

struct OutputFrame {
    std::optional<TriggerIntent> left;
    std::optional<TriggerIntent> right;
    std::optional<RumbleIntent> rumble;
    BodyProgram body{};
    std::optional<LedIntent> leds;
};
} // namespace inputnext::adapters
