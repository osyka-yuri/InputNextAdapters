#pragma once

#include "skyrim_adapter/model.hpp"

#include <cstdint>

namespace skyrim_adapter::detail {
struct VoiceProfile {
    ShoutLevel level{ShoutLevel::unknown};
    std::uint32_t duration_ms{};
    float classic_low{};
    float classic_high{};
    std::uint32_t classic_peak_ms{};
    float pcm_frequency_hz{};
    float pcm_second_frequency_hz{};
    float pcm_second_mix{};
    float pcm_level{};
    float pcm_noise{};
    float pcm_attack_ms{};
    float pcm_release_ms{};
    float pcm_decay_per_second{};
};

[[nodiscard]] constexpr VoiceProfile voice_profile(ShoutLevel level) noexcept {
    switch (level) {
    case ShoutLevel::one_word:
        return {
            .level = ShoutLevel::one_word,
            .duration_ms = 110,
            .classic_low = 0.28F,
            .classic_high = 0.14F,
            .classic_peak_ms = 60,
            .pcm_frequency_hz = 104.0F,
            .pcm_second_frequency_hz = 0.0F,
            .pcm_second_mix = 0.0F,
            .pcm_level = 0.32F,
            .pcm_noise = 0.035F,
            .pcm_attack_ms = 2.0F,
            .pcm_release_ms = 18.0F,
            .pcm_decay_per_second = 9.0F,
        };
    case ShoutLevel::two_words:
        return {
            .level = ShoutLevel::two_words,
            .duration_ms = 170,
            .classic_low = 0.42F,
            .classic_high = 0.22F,
            .classic_peak_ms = 100,
            .pcm_frequency_hz = 88.0F,
            .pcm_second_frequency_hz = 132.0F,
            .pcm_second_mix = 0.08F,
            .pcm_level = 0.44F,
            .pcm_noise = 0.04F,
            .pcm_attack_ms = 2.5F,
            .pcm_release_ms = 24.0F,
            .pcm_decay_per_second = 6.0F,
        };
    case ShoutLevel::three_words:
        return {
            .level = ShoutLevel::three_words,
            .duration_ms = 240,
            .classic_low = 0.56F,
            .classic_high = 0.30F,
            .classic_peak_ms = 140,
            .pcm_frequency_hz = 72.0F,
            .pcm_second_frequency_hz = 108.0F,
            .pcm_second_mix = 0.14F,
            .pcm_level = 0.56F,
            .pcm_noise = 0.045F,
            .pcm_attack_ms = 3.0F,
            .pcm_release_ms = 32.0F,
            .pcm_decay_per_second = 4.0F,
        };
    case ShoutLevel::unknown:
    default:
        // Keep the established voice cue unchanged for unavailable or invalid capture.
        return {
            .level = ShoutLevel::unknown,
            .duration_ms = 160,
            .classic_low = 0.42F,
            .classic_high = 0.22F,
            .classic_peak_ms = 100,
            .pcm_frequency_hz = 80.0F,
            .pcm_second_frequency_hz = 0.0F,
            .pcm_second_mix = 0.0F,
            .pcm_level = 0.42F,
            .pcm_noise = 0.04F,
            .pcm_attack_ms = 3.0F,
            .pcm_release_ms = 26.0F,
            .pcm_decay_per_second = 5.0F,
        };
    }
}
} // namespace skyrim_adapter::detail
