#pragma once

#include <array>
#include <cstddef>

namespace inputnext::adapters {
inline constexpr std::size_t audio_haptic_knot_count = 32;

struct AudioHapticEnvelope {
    std::array<float, audio_haptic_knot_count> primary{};
    std::array<float, audio_haptic_knot_count> secondary{};
    bool enabled{};

    bool operator==(const AudioHapticEnvelope&) const noexcept = default;
};

struct AudioHapticProfile {
    float frequency{};
    float second_frequency{};
    AudioHapticEnvelope envelope{};

    bool operator==(const AudioHapticProfile&) const noexcept = default;
};

struct WaveRecipe;

[[nodiscard]] bool valid_audio_haptic_envelope(const AudioHapticEnvelope& envelope) noexcept;
[[nodiscard]] bool valid_audio_haptic_profile(const AudioHapticProfile& profile) noexcept;
[[nodiscard]] bool apply_audio_haptic_profile(WaveRecipe& recipe,
    const AudioHapticProfile& profile) noexcept;
} // namespace inputnext::adapters
