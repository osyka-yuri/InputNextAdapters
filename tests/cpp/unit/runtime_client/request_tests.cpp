#include "inputnext_adapters/runtime_client.hpp"
#include <catch2/catch_test_macros.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <tuple>
#include <type_traits>

namespace {
bool same_context(const input_next_effects_context_v1& left,
    const input_next_effects_context_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.reserved,
            value.runtime_id[0],
            value.runtime_id[1],
            value.id);
    };
    return fields(left) == fields(right);
}
} // namespace

TEST_CASE("runtime_client.make_request", "[unit][runtime_client]") {
    using namespace std::chrono_literals;

    static_assert(!std::is_invocable_v<decltype(inputnext::adapters::make_request),
                      input_next_effects_context_v1,
                      std::uint64_t,
                      std::chrono::milliseconds,
                      std::uint32_t>,
        "make_request must reject swapped duration and channel_mask parameters");
    static_assert(!std::is_invocable_v<decltype(inputnext::adapters::make_request),
                      input_next_effects_context_v1,
                      std::uint64_t,
                      std::uint32_t,
                      std::uint32_t>,
        "make_request must reject raw integer TTL to prevent accidental parameter swaps");

    const input_next_effects_context_v1 context{
        .struct_size = static_cast<std::uint32_t>(sizeof(input_next_effects_context_v1)),
        .reserved = 0,
        .runtime_id = {11, 12},
        .id = 42,
    };

    // Channel mask, layout defaults, and positive TTL conversion.
    {
        const auto req =
            inputnext::adapters::make_request(context, 9, INPUT_NEXT_EFFECTS_RIGHT_TRIGGER, 1000ms);
        REQUIRE(req.struct_size == sizeof(req));
        REQUIRE(req.channel_mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER);
        REQUIRE(req.ttl_ms == 1000);
        REQUIRE(req.reserved == 0);
        REQUIRE(same_context(req.context, context));
        REQUIRE(req.mutation_sequence == 9);
        REQUIRE(req.left_trigger.mode == 0);
        REQUIRE(req.left_trigger.start == 0.0F);
        REQUIRE(req.right_trigger.mode == 0);
        REQUIRE(req.right_trigger.start == 0.0F);
        REQUIRE(req.rumble.low_frequency == 0.0F);
        REQUIRE(req.rumble.high_frequency == 0.0F);
        REQUIRE(req.leds.lightbar_rgb == 0);
        REQUIRE(req.leds.player_index == 0);
    }

    // Zero TTL.
    {
        const auto req =
            inputnext::adapters::make_request(context, 10, INPUT_NEXT_EFFECTS_LEFT_TRIGGER, 0ms);
        REQUIRE(req.channel_mask == INPUT_NEXT_EFFECTS_LEFT_TRIGGER);
        REQUIRE(req.ttl_ms == 0);
        REQUIRE(req.struct_size == sizeof(req));
        REQUIRE(same_context(req.context, context));
    }

    // 1 ms TTL.
    {
        const auto req =
            inputnext::adapters::make_request(context, 11, INPUT_NEXT_EFFECTS_RUMBLE, 1ms);
        REQUIRE(req.channel_mask == INPUT_NEXT_EFFECTS_RUMBLE);
        REQUIRE(req.ttl_ms == 1);
        REQUIRE(req.struct_size == sizeof(req));
        REQUIRE(same_context(req.context, context));
    }

    // Maximum 32-bit millisecond boundary.
    {
        constexpr auto kMaxUint32 = std::numeric_limits<std::uint32_t>::max();
        const auto req = inputnext::adapters::make_request(context,
            12,
            0x1FU,
            std::chrono::milliseconds{kMaxUint32});
        REQUIRE(req.channel_mask == 0x1FU);
        REQUIRE(req.ttl_ms == kMaxUint32);
        REQUIRE(req.struct_size == sizeof(req));
        REQUIRE(same_context(req.context, context));
    }

    // Negative durations saturate to zero.
    {
        const auto req = inputnext::adapters::make_request(context, 13, 0x1FU, -10ms);
        REQUIRE(req.channel_mask == 0x1FU);
        REQUIRE(req.ttl_ms == 0);
    }
    {
        const auto req = inputnext::adapters::make_request(context,
            14,
            0x1FU,
            std::chrono::milliseconds{std::numeric_limits<std::int64_t>::min()});
        REQUIRE(req.ttl_ms == 0);
    }

    // Durations exceeding UINT32_MAX saturate to max limit.
    {
        constexpr auto kBeyondMax =
            static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) + 1000LL;
        const auto req = inputnext::adapters::make_request(context,
            15,
            0x1FU,
            std::chrono::milliseconds{kBeyondMax});
        REQUIRE(req.ttl_ms == std::numeric_limits<std::uint32_t>::max());
    }
    {
        const auto req = inputnext::adapters::make_request(context,
            16,
            0x1FU,
            std::chrono::milliseconds{std::numeric_limits<std::int64_t>::max()});
        REQUIRE(req.ttl_ms == std::numeric_limits<std::uint32_t>::max());
    }
}
