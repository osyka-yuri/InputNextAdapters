#include "fallout4_adapter/detail/body_voice_mixer.hpp"

#include "effect_recipes.hpp"
#include "fallout4_adapter/detail/body_cue_intent.hpp"
#include "fallout4_adapter/effect_settings.hpp"
#include "fallout4_adapter/model.hpp"

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ratio>

namespace fallout4_adapter::detail {
namespace {

[[nodiscard]] std::uint32_t scaled_duration_ms(std::uint16_t duration_ms,
    float duration_scale) noexcept {
    const auto scaled =
        std::lround(static_cast<double>(duration_ms) * static_cast<double>(duration_scale));
    return static_cast<std::uint32_t>(std::max(1L, scaled));
}

[[nodiscard]] float bounded_duration_multiplier(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 1.0F, 1.875F) : 1.0F;
}

} // namespace

std::uint64_t BodyVoiceMixer::next_nonzero(std::uint64_t& sequence) noexcept {
    const std::uint64_t result = sequence;
    sequence = sequence == std::numeric_limits<std::uint64_t>::max() ? 1 : sequence + 1;
    return result == 0 ? 1 : result;
}

bool BodyVoiceMixer::voice_is_active(const inputnext::adapters::BodyVoice& voice,
    MonoTime now) noexcept {
    return voice.started_at <= now && voice.duration_ms != 0 &&
           now - voice.started_at < std::chrono::milliseconds{voice.duration_ms};
}

void BodyVoiceMixer::clear() noexcept {
    voices_ = {};
}

void BodyVoiceMixer::reset() noexcept {
    clear();
    next_voice_sequence_ = 1;
}

void BodyVoiceMixer::expire(MonoTime now) noexcept {
    for (auto& voice : voices_) {
        if (voice.occupied && !voice_is_active(voice.voice, now)) {
            voice = {};
        }
    }
}

void BodyVoiceMixer::add(const BodyCueIntent& cue,
    MonoTime now,
    const BodyAdjustment& adjustment,
    BodyMixerOptions options) noexcept {
    if (!options.body_enabled || options.scale <= 0.0F || !std::isfinite(cue.intensity)) {
        return;
    }
    if (!adjustment.enabled || (adjustment.hd_scale <= 0.0F && adjustment.classic_scale <= 0.0F)) {
        return;
    }

    const auto recipe = recipe_for(cue.effect);
    const auto duration_ms = scaled_duration_ms(recipe.duration_ms,
        adjustment.duration_scale * bounded_duration_multiplier(cue.duration_multiplier));
    const float bounded_intensity = std::clamp(cue.intensity, 0.0F, 1.0F);
    const int priority = cue.priority_override.value_or(recipe.priority);
    if (bounded_intensity <= 0.0F || duration_ms == 0 || cue.started_at > now ||
        now - cue.started_at >= std::chrono::milliseconds{duration_ms}) {
        return;
    }

    auto available =
        std::ranges::find_if(voices_, [](const ActiveVoice& voice) { return !voice.occupied; });
    if (available == voices_.end()) {
        const auto lowest = std::ranges::min_element(voices_,
            [](const ActiveVoice& left, const ActiveVoice& right) {
                if (left.voice.priority != right.voice.priority) {
                    return left.voice.priority < right.voice.priority;
                }
                return left.voice.started_at < right.voice.started_at;
            });
        if (lowest == voices_.end() || lowest->voice.priority > priority) {
            return;
        }
        available = lowest;
    }

    const auto sequence = next_nonzero(next_voice_sequence_);
    auto voice = inputnext::adapters::BodyVoice{};
    voice.wave = recipe.wave;
    if (options.audio_haptics_enabled && adjustment.audio_haptic) {
        static_cast<void>(
            inputnext::adapters::apply_audio_haptic_profile(voice.wave, *adjustment.audio_haptic));
    }
    if (adjustment.frequency_hz) {
        const float scale = *adjustment.frequency_hz / voice.wave.frequency;
        voice.wave.frequency = *adjustment.frequency_hz;
        if (voice.wave.second_frequency > 0.0F) {
            voice.wave.second_frequency =
                std::clamp(voice.wave.second_frequency * scale, 20.0F, 1000.0F);
        }
    }
    voice.started_at = cue.started_at;
    voice.duration_ms = duration_ms;
    voice.stable_id = sequence;
    voice.sequence = sequence;
    voice.source_id = static_cast<std::uint32_t>(cue.effect) + 1U;
    voice.priority = priority;
    voice.scale = options.scale * bounded_intensity;
    voice.hd_scale = adjustment.hd_scale;
    voice.duration_scale = adjustment.duration_scale;
    voice.position_gain = 1.0F;
    voice.channel_mask = 0x3U;
    voice.held = false;
    *available = ActiveVoice{
        .voice = voice,
        .fallback_low = recipe.fallback_low,
        .fallback_high = recipe.fallback_high,
        .classic_scale = adjustment.classic_scale,
        .occupied = true,
    };
}

void BodyVoiceMixer::populate(OutputFrame& frame,
    MonoTime now,
    bool enabled,
    std::uint32_t maximum_ttl_ms) noexcept {
    if (!enabled) {
        return;
    }

    std::array<const ActiveVoice*, 5> active{};
    std::size_t count{};
    for (auto& candidate : voices_) {
        if (!candidate.occupied) {
            continue;
        }
        if (!voice_is_active(candidate.voice, now)) {
            candidate = {};
            continue;
        }
        active[count++] = &candidate;
    }
    std::sort(active.begin(),
        active.begin() + static_cast<std::ptrdiff_t>(count),
        [](const ActiveVoice* left, const ActiveVoice* right) {
            if (left->voice.priority != right->voice.priority) {
                return left->voice.priority > right->voice.priority;
            }
            return left->voice.started_at > right->voice.started_at;
        });

    inputnext::adapters::BodyProgram body{};
    double low_squared{};
    double high_squared{};
    std::uint32_t fallback_ttl{};
    for (std::size_t index = 0; index < count; ++index) {
        const ActiveVoice& voice = *active[index];
        body.voices[index] = voice.voice;
        const auto elapsed =
            std::chrono::duration<float, std::milli>(now - voice.voice.started_at).count();
        const float envelope = envelope_gain(voice.voice.wave,
            voice.voice.duration_ms,
            elapsed,
            voice.voice.duration_scale);
        const float gain = envelope * voice.voice.scale;
        const float classic_gain = gain * voice.classic_scale;
        low_squared += static_cast<double>(voice.fallback_low * classic_gain) *
                       (voice.fallback_low * classic_gain);
        high_squared += static_cast<double>(voice.fallback_high * classic_gain) *
                        (voice.fallback_high * classic_gain);
        const auto age =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - voice.voice.started_at);
        const auto remaining = voice.voice.duration_ms - static_cast<std::uint32_t>(age.count());
        fallback_ttl = std::max(fallback_ttl, remaining);
    }
    frame.body = body;

    const auto low = static_cast<float>(std::sqrt(low_squared));
    const auto high = static_cast<float>(std::sqrt(high_squared));
    if (fallback_ttl != 0 && (low > 0.0F || high > 0.0F)) {
        inputnext::adapters::RumbleIntent rumble{};
        rumble.payload.low_frequency = std::clamp(low, 0.0F, 0.78F);
        rumble.payload.high_frequency = std::clamp(high, 0.0F, 0.78F);
        rumble.ttl_ms = std::clamp(fallback_ttl, 1U, maximum_ttl_ms);
        frame.rumble = rumble;
    }
}

} // namespace fallout4_adapter::detail
