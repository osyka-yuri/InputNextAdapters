#include "inputnext_adapters/runtime_client.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

namespace inputnext::adapters {

input_next_effects_request_v1 make_request(const input_next_effects_context_v1& context,
    std::uint64_t mutation_sequence,
    std::uint32_t channel_mask,
    std::chrono::milliseconds ttl) noexcept {
    const auto count = ttl.count();
    const auto clamped = std::clamp<std::chrono::milliseconds::rep>(count,
        0,
        static_cast<std::chrono::milliseconds::rep>(std::numeric_limits<std::uint32_t>::max()));
    return {
        .struct_size = static_cast<std::uint32_t>(sizeof(input_next_effects_request_v1)),
        .channel_mask = channel_mask,
        .ttl_ms = static_cast<std::uint32_t>(clamped),
        .reserved = 0,
        .context = context,
        .mutation_sequence = mutation_sequence,
        .left_trigger = {},
        .right_trigger = {},
        .rumble = {},
        .leds = {},
    };
}

} // namespace inputnext::adapters
