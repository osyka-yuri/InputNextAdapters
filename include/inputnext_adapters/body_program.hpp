#pragma once

#include <inputnext_adapters/audio_haptic_profile.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

namespace inputnext::adapters {
using MonoTime = std::chrono::steady_clock::time_point;

inline constexpr std::uint32_t body_pcm_sample_rate = 48'000;
inline constexpr std::uint32_t body_pcm_max_frames = 9'600;

struct WaveRecipe {
    float frequency{};
    float second_frequency{};
    float second_mix{};
    float level{};
    float noise{};
    float attack_ms{};
    float release_ms{};
    float decay_per_second{};
    float modulation_hz{};
    std::uint16_t first_pulse_ms{};
    std::uint16_t pulse_gap_ms{};
    std::uint16_t second_pulse_ms{};
    AudioHapticEnvelope audio_envelope{};

    bool operator==(const WaveRecipe&) const noexcept = default;
};

struct BodyVoice {
    WaveRecipe wave{};
    MonoTime started_at;
    std::uint32_t duration_ms{};
    std::uint64_t stable_id{};
    std::uint64_t sequence{};
    std::uint32_t source_id{};
    int priority{};
    float scale{};
    float hd_scale{1.0F};
    float duration_scale{1.0F};
    float position_gain{};
    std::uint8_t channel_mask{};
    bool held{};
};

struct BodyProgram {
    // Each voice carries its own routing and lifetime; slots have no shared game semantics.
    std::array<std::optional<BodyVoice>, 5> voices{};
};

struct BodyPcmWindow {
    std::uint32_t frame_count{};
    bool contains_held{};
};

[[nodiscard]] bool same_body_program_identity(const BodyProgram& left,
    const BodyProgram& right) noexcept;
[[nodiscard]] bool body_program_active(const BodyProgram& program, MonoTime now) noexcept;
[[nodiscard]] bool body_program_has_held(const BodyProgram& program, MonoTime now) noexcept;
[[nodiscard]] BodyPcmWindow build_body_pcm(const BodyProgram& program,
    MonoTime begins_at,
    std::span<float> stereo_destination) noexcept;
} // namespace inputnext::adapters
