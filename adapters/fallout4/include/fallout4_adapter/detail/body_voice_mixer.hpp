#pragma once

#include "fallout4_adapter/detail/body_cue_intent.hpp"
#include "fallout4_adapter/effect_settings.hpp"

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <array>
#include <cstdint>

namespace fallout4_adapter::detail {

struct BodyMixerOptions {
    float scale{};
    bool body_enabled{};
    bool audio_haptics_enabled{};
};

class BodyVoiceMixer {
public:
    // Clear active voices while keeping stable IDs monotonic across mode/weapon changes.
    void clear() noexcept;
    // Full effects reset also restarts the stable ID sequence.
    void reset() noexcept;
    void expire(MonoTime now) noexcept;
    void add(const BodyCueIntent& cue,
        MonoTime now,
        const BodyAdjustment& adjustment,
        BodyMixerOptions options) noexcept;
    void
    populate(OutputFrame& frame, MonoTime now, bool enabled, std::uint32_t maximum_ttl_ms) noexcept;

private:
    struct ActiveVoice {
        inputnext::adapters::BodyVoice voice{};
        float fallback_low{};
        float fallback_high{};
        float classic_scale{1.0F};
        bool occupied{};
    };

    [[nodiscard]] static std::uint64_t next_nonzero(std::uint64_t& sequence) noexcept;
    [[nodiscard]] static bool voice_is_active(const inputnext::adapters::BodyVoice& voice,
        MonoTime now) noexcept;

    std::array<ActiveVoice, 5> voices_{};
    std::uint64_t next_voice_sequence_{1};
};

} // namespace fallout4_adapter::detail
