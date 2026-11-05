#pragma once

#include "skyrim_adapter/model.hpp"

#include <cstdint>
#include <optional>

namespace skyrim_adapter::detail {

struct TriggerEnvelope {
    input_next_effects_trigger_v1 payload{};
    MonoTime started_at;
    std::uint32_t duration_ms{};
    int priority{};
    bool valid{};
};

struct RumbleEnvelope {
    std::uint8_t shape{};
    float low{};
    float high{};
    float second_low{};
    float second_high{};
    MonoTime started_at;
    std::uint32_t duration_ms{};
    std::uint32_t peak_ms{};
    std::uint32_t gap_ms{};
    std::uint32_t second_duration_ms{};
    int priority{};
    bool valid{};
};

struct EffectRequests {
    TriggerEnvelope left_request{};
    TriggerEnvelope right_request{};
    TriggerEnvelope left_held{};
    TriggerEnvelope right_held{};
    RumbleEnvelope rumble_request{};
    RumbleEnvelope held_rumble_request{};
};

struct RumbleFallback {
    float low{};
    float high{};
    std::uint32_t ttl_ms{};
    bool present{};
};

class EffectMixer {
public:
    void reset() noexcept;
    // Body-history reset clears rumble but retains trigger overlays.
    void reset_rumble() noexcept;

    static void offer_trigger(TriggerEnvelope& choice,
        const input_next_effects_trigger_v1& payload,
        MonoTime started_at,
        std::uint32_t duration_ms,
        int priority) noexcept;
    static void offer_rumble(RumbleEnvelope& choice,
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
        int priority) noexcept;

    [[nodiscard]] EffectFrame finish(MonoTime now,
        const EffectRequests& requests,
        const BodyPlan& body,
        RumbleFallback bow_fallback,
        float trigger_scale,
        float rumble_scale) noexcept;

private:
    struct OutputScales {
        float trigger{};
        float rumble{};
    };

    void retain_step_envelopes(MonoTime now, const EffectRequests& requests) noexcept;
    void sample_trigger_intents(MonoTime now,
        const EffectRequests& requests,
        EffectFrame& frame) const noexcept;
    [[nodiscard]] RumbleFallback sample_rumble_intent(MonoTime now,
        const EffectRequests& requests,
        RumbleFallback bow_fallback) const noexcept;
    static void
    scale_step_outputs(EffectFrame& frame, RumbleFallback rumble, OutputScales scales) noexcept;

    TriggerEnvelope left_trigger_overlay_{};
    TriggerEnvelope right_trigger_overlay_{};
    RumbleEnvelope rumble_envelope_{};
};

} // namespace skyrim_adapter::detail
