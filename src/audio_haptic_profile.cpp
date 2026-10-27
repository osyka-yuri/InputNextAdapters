#include <inputnext_adapters/audio_haptic_profile.hpp>

#include <inputnext_adapters/body_program.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace inputnext::adapters {
bool valid_audio_haptic_envelope(const AudioHapticEnvelope& envelope) noexcept {
    if (!envelope.enabled || envelope.primary.front() != 0.0F || envelope.primary.back() != 0.0F ||
        envelope.secondary.front() != 0.0F || envelope.secondary.back() != 0.0F) {
        return false;
    }

    bool has_positive_gain = false;
    for (std::size_t index = 0; index < audio_haptic_knot_count; ++index) {
        const float primary = envelope.primary[index];
        const float secondary = envelope.secondary[index];
        if (!std::isfinite(primary) || !std::isfinite(secondary) || primary < 0.0F ||
            secondary < 0.0F || primary > 1.0F || secondary > 1.0F ||
            (primary + secondary) > (1.0F + 1.0e-6F)) {
            return false;
        }
        has_positive_gain = has_positive_gain || primary > 0.0F || secondary > 0.0F;
    }
    return has_positive_gain;
}

bool valid_audio_haptic_profile(const AudioHapticProfile& profile) noexcept {
    if (!std::isfinite(profile.frequency) || profile.frequency < 20.0F ||
        profile.frequency > 1'000.0F || !std::isfinite(profile.second_frequency) ||
        (profile.second_frequency != 0.0F &&
            (profile.second_frequency < 20.0F || profile.second_frequency > 1'000.0F)) ||
        !valid_audio_haptic_envelope(profile.envelope)) {
        return false;
    }
    if (profile.second_frequency == 0.0F && std::ranges::any_of(profile.envelope.secondary,
                                                [](float value) { return value != 0.0F; })) {
        return false;
    }
    return true;
}

bool apply_audio_haptic_profile(WaveRecipe& recipe, const AudioHapticProfile& profile) noexcept {
    if (!valid_audio_haptic_profile(profile) || !std::isfinite(recipe.frequency) ||
        recipe.frequency <= 0.0F || !std::isfinite(recipe.level) || recipe.level <= 0.0F) {
        return false;
    }
    recipe.frequency = profile.frequency;
    recipe.second_frequency = profile.second_frequency;
    recipe.audio_envelope = profile.envelope;
    return true;
}
} // namespace inputnext::adapters
