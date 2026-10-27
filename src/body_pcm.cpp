#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/body_program.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>

namespace inputnext::adapters {
namespace {
constexpr float maximum_peak = 0.75F;
constexpr float soft_knee_fraction = 0.60F;

float safe_scale(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
}

float safe_hd_scale(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 2.0F) : 1.0F;
}

double safe_duration_scale(float value) noexcept {
    return std::isfinite(value) ? std::clamp(static_cast<double>(value), 0.25, 2.0) : 1.0;
}

std::uint64_t mix64(std::uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31U;
    return value;
}

class SeededNoise {
public:
    explicit SeededNoise(std::uint64_t seed) noexcept : seed_(seed) {}

    [[nodiscard]] float unit(std::int64_t sample) const noexcept {
        const auto index = static_cast<std::uint64_t>(sample);
        const std::uint64_t value = mix64(seed_ ^ (index * 0x9e3779b97f4a7c15ULL));
        constexpr double inverse = 1.0 / 9007199254740992.0;
        return static_cast<float>((static_cast<double>(value >> 11U) * inverse * 2.0) - 1.0);
    }

private:
    std::uint64_t seed_;
};

class RollingNoise {
public:
    RollingNoise(SeededNoise generator, std::int64_t sample) noexcept
        : generator_(generator),
          sample_(sample),
          previous_(generator_.unit(sample - 1)),
          current_(generator_.unit(sample)),
          next_(generator_.unit(sample + 1)) {}

    [[nodiscard]] float at(std::int64_t sample) noexcept {
        if (sample != sample_) {
            if (sample == sample_ + 1) {
                advance();
            } else {
                reseed(sample);
            }
        }
        return (0.25F * previous_) + (0.50F * current_) + (0.25F * next_);
    }

private:
    void advance() noexcept {
        previous_ = current_;
        current_ = next_;
        ++sample_;
        next_ = generator_.unit(sample_ + 1);
    }

    void reseed(std::int64_t sample) noexcept {
        sample_ = sample;
        previous_ = generator_.unit(sample - 1);
        current_ = generator_.unit(sample);
        next_ = generator_.unit(sample + 1);
    }

    SeededNoise generator_;
    std::int64_t sample_;
    float previous_;
    float current_;
    float next_;
};

double smooth_gate(double position, double width) noexcept {
    if (position <= 0.0) {
        return 0.0;
    }
    if (position >= width || width <= 0.0) {
        return 1.0;
    }
    const double normalized_position = position / width;
    return normalized_position * normalized_position * (3.0 - (2.0 * normalized_position));
}

struct EnvelopeResult {
    double gain{0.0};
    bool second_pulse{false};
};

struct EnvelopeParameters {
    double duration_scale;
    double duration_ms;
    double attack_width;
    double release_width;
    double first_pulse_end;
    double second_pulse_start;
    double second_pulse_end;
};

EnvelopeParameters envelope_parameters(const WaveRecipe& recipe,
    const BodyVoice& voice,
    double duration_scale) noexcept {
    const double duration_ms = static_cast<double>(voice.duration_ms) / duration_scale;
    const double edge = std::min(3.0, std::max(0.5, duration_ms * 0.20));
    const double attack = std::max(0.0, static_cast<double>(recipe.attack_ms));
    const double release = std::max(0.0, static_cast<double>(recipe.release_ms));
    const auto first_pulse_end = static_cast<double>(recipe.first_pulse_ms);
    const double second_pulse_start = first_pulse_end + static_cast<double>(recipe.pulse_gap_ms);
    return {
        .duration_scale = duration_scale,
        .duration_ms = duration_ms,
        .attack_width = attack > 0.0 ? attack : edge,
        .release_width = release > 0.0 ? release : edge,
        .first_pulse_end = first_pulse_end,
        .second_pulse_start = second_pulse_start,
        .second_pulse_end = second_pulse_start + static_cast<double>(recipe.second_pulse_ms),
    };
}

EnvelopeResult
envelope(const WaveRecipe& recipe, const EnvelopeParameters& parameters, double age_ms) noexcept {
    const double envelope_age_ms = age_ms / parameters.duration_scale;
    double value = smooth_gate(envelope_age_ms, parameters.attack_width) *
                   smooth_gate(parameters.duration_ms - envelope_age_ms, parameters.release_width);

    bool second_pulse = false;
    if (recipe.first_pulse_ms != 0) {
        if (envelope_age_ms < parameters.first_pulse_end) {
            value *= smooth_gate(envelope_age_ms, 1.5) *
                     smooth_gate(parameters.first_pulse_end - envelope_age_ms, 1.5);
        } else if (envelope_age_ms >= parameters.second_pulse_start &&
                   envelope_age_ms < parameters.second_pulse_end) {
            second_pulse = true;
            value *= smooth_gate(envelope_age_ms - parameters.second_pulse_start, 1.5) *
                     smooth_gate(parameters.second_pulse_end - envelope_age_ms, 1.5);
        } else {
            value = 0.0;
        }
    }

    return {
        .gain = std::clamp(value, 0.0, 1.0),
        .second_pulse = second_pulse,
    };
}

struct OscillatorTiming {
    double initial_seconds;
    double seconds_per_frame;
};

struct Oscillator {
    double sine;
    double cosine;
    double step_sine;
    double step_cosine;

    static Oscillator at_age(double frequency, double age_frames) noexcept {
        return at_time(frequency,
            {
                .initial_seconds = age_frames / static_cast<double>(body_pcm_sample_rate),
                .seconds_per_frame = 1.0 / static_cast<double>(body_pcm_sample_rate),
            });
    }

    static Oscillator at_time(double frequency, OscillatorTiming timing) noexcept {
        const double angle = 2.0 * std::numbers::pi * frequency * timing.initial_seconds;
        const double step = 2.0 * std::numbers::pi * frequency * timing.seconds_per_frame;
        return {
            .sine = std::sin(angle),
            .cosine = std::cos(angle),
            .step_sine = std::sin(step),
            .step_cosine = std::cos(step),
        };
    }

    void advance() noexcept {
        const double next_sine = (sine * step_cosine) + (cosine * step_sine);
        const double next_cosine = (cosine * step_cosine) - (sine * step_sine);
        sine = next_sine;
        cosine = next_cosine;
    }
};

struct OscillatorValues {
    double modulation;
    double first;
    double second;
};

float render_voice_sample(const BodyVoice& voice,
    const EnvelopeResult& env,
    double decay_gain,
    OscillatorValues oscillators,
    float noise) noexcept {
    const WaveRecipe& recipe = voice.wave;
    if (env.gain <= 0.0) {
        return 0.0F;
    }

    const double tone =
        recipe.first_pulse_ms != 0 && env.second_pulse && recipe.second_frequency > 0.0F
            ? oscillators.second
            : (oscillators.first * (1.0 - recipe.second_mix)) +
                  (oscillators.second * recipe.second_mix);
    const double carrier = recipe.noise == 0.0F ? tone
                                                : (tone * (1.0 - recipe.noise)) +
                                                      (static_cast<double>(noise) * recipe.noise);
    const double pulse_gain =
        recipe.modulation_hz > 0.0F ? 0.58 + (0.42 * oscillators.modulation) : 1.0;
    const double result =
        carrier * static_cast<double>(recipe.level) * (env.gain * decay_gain) * pulse_gain;
    return std::isfinite(result) ? static_cast<float>(std::clamp(result, -1.0, 1.0)) : 0.0F;
}

struct AudioEnvelopeValues {
    double primary;
    double secondary;
};

struct AudioEnvelopeTiming {
    double age_frames;
    double duration_frames;
};

struct VoiceFrameRange {
    std::uint32_t first;
    std::uint32_t end;
};

struct VoiceRenderWindow {
    double age_start;
    VoiceFrameRange frames;
};

struct VoiceScales {
    float scale;
    float hd_scale;
};

bool valid_audio_recipe(const WaveRecipe& recipe) noexcept {
    if (!valid_audio_haptic_envelope(recipe.audio_envelope) || !std::isfinite(recipe.frequency) ||
        recipe.frequency <= 0.0F || !std::isfinite(recipe.second_frequency) ||
        recipe.second_frequency < 0.0F) {
        return false;
    }
    if (recipe.second_frequency == 0.0F) {
        for (const float gain : recipe.audio_envelope.secondary) {
            if (gain != 0.0F) {
                return false;
            }
        }
    }
    return true;
}

AudioEnvelopeValues audio_envelope_values(const AudioHapticEnvelope& envelope,
    AudioEnvelopeTiming timing) noexcept {
    const double duration_ms = timing.duration_frames / 48.0;
    const double age_ms = timing.age_frames / 48.0;
    // Producers already apply DurationScale to the delivered voice duration.
    const double position = std::clamp(timing.age_frames / timing.duration_frames, 0.0, 1.0) *
                            static_cast<double>(audio_haptic_knot_count - 1U);
    const auto first = static_cast<std::size_t>(position);
    const auto second = std::min(first + 1U, audio_haptic_knot_count - 1U);
    const double fraction = position - static_cast<double>(first);
    const auto interpolate = [&](const auto& values) noexcept {
        return (static_cast<double>(values[first]) * (1.0 - fraction)) +
               (static_cast<double>(values[second]) * fraction);
    };
    const double edge_gain = smooth_gate(age_ms, 1.5) * smooth_gate(duration_ms - age_ms, 1.5);
    return {
        .primary = interpolate(envelope.primary) * edge_gain,
        .secondary = interpolate(envelope.secondary) * edge_gain,
    };
}

float render_audio_haptic_sample(const WaveRecipe& recipe,
    AudioEnvelopeValues envelope_values,
    double primary_oscillator,
    double secondary_oscillator) noexcept {
    const double result =
        static_cast<double>(recipe.level) * ((primary_oscillator * envelope_values.primary) +
                                                (secondary_oscillator * envelope_values.secondary));
    return std::isfinite(result) ? static_cast<float>(std::clamp(result, -1.0, 1.0)) : 0.0F;
}

double age_at_window_start(MonoTime begins_at, MonoTime started_at) noexcept {
    return std::chrono::duration<double>(begins_at - started_at).count() *
           static_cast<double>(body_pcm_sample_rate);
}

struct AgeWindow {
    double first_age;
    std::uint32_t frame_count;
};

std::uint32_t first_frame_at_age_or_after(const AgeWindow& window, double minimum_age) noexcept {
    std::uint32_t first = 0;
    std::uint32_t last = window.frame_count;
    while (first < last) {
        const std::uint32_t middle = first + ((last - first) / 2U);
        if (window.first_age + static_cast<double>(middle) < minimum_age) {
            first = middle + 1U;
        } else {
            last = middle;
        }
    }
    return first;
}

void accumulate_audio_voice(const BodyVoice& voice,
    std::span<float> stereo,
    VoiceRenderWindow window) noexcept {
    const WaveRecipe& recipe = voice.wave;
    const double gain = static_cast<double>(voice.position_gain) *
                        static_cast<double>(safe_scale(voice.scale)) *
                        static_cast<double>(safe_hd_scale(voice.hd_scale));
    const double duration_frames = static_cast<double>(voice.duration_ms) * 48.0;
    const double first_age = window.age_start + static_cast<double>(window.frames.first);
    Oscillator primary = Oscillator::at_age(static_cast<double>(recipe.frequency), first_age);
    const bool has_secondary = recipe.second_frequency > 0.0F;
    Oscillator secondary =
        has_secondary ? Oscillator::at_age(static_cast<double>(recipe.second_frequency), first_age)
                      : primary;

    for (std::uint32_t frame = window.frames.first; frame < window.frames.end; ++frame) {
        const double age = window.age_start + static_cast<double>(frame);
        const AudioEnvelopeValues gains = audio_envelope_values(recipe.audio_envelope,
            {.age_frames = age, .duration_frames = duration_frames});
        const float sample = render_audio_haptic_sample(recipe,
            gains,
            primary.sine,
            has_secondary ? secondary.sine : 0.0);
        const std::size_t index = static_cast<std::size_t>(frame) * 2U;
        if ((voice.channel_mask & 0x1U) != 0) {
            stereo[index] += static_cast<float>(static_cast<double>(sample) * gain);
        }
        if ((voice.channel_mask & 0x2U) != 0) {
            stereo[index + 1U] += static_cast<float>(static_cast<double>(sample) * gain);
        }
        primary.advance();
        if (has_secondary) {
            secondary.advance();
        }
    }
}

void accumulate_legacy_voice(const BodyVoice& voice,
    std::span<float> stereo,
    VoiceRenderWindow window,
    VoiceScales scales) noexcept {
    const WaveRecipe& recipe = voice.wave;
    const double first_age = window.age_start + static_cast<double>(window.frames.first);
    const double gain = static_cast<double>(voice.position_gain) *
                        static_cast<double>(scales.scale) * static_cast<double>(scales.hd_scale);
    const auto first_frequency = static_cast<double>(recipe.frequency);
    const auto second_frequency = static_cast<double>(recipe.second_frequency);
    const bool has_second_carrier = recipe.second_frequency > 0.0F;
    const bool has_modulation = recipe.modulation_hz > 0.0F;
    const bool has_decay = recipe.decay_per_second > 0.0F;
    const bool has_noise = recipe.noise != 0.0F;
    const double duration_scale = safe_duration_scale(voice.duration_scale);
    const EnvelopeParameters envelope_values = envelope_parameters(recipe, voice, duration_scale);
    Oscillator first = Oscillator::at_age(first_frequency, first_age);
    Oscillator second =
        has_second_carrier ? Oscillator::at_age(second_frequency, first_age) : first;
    Oscillator modulation{};
    if (has_modulation) {
        const double envelope_age_ms = (first_age / 48.0) / duration_scale;
        modulation = Oscillator::at_time(static_cast<double>(recipe.modulation_hz),
            {
                .initial_seconds = envelope_age_ms / 1000.0,
                .seconds_per_frame = (1.0 / 48.0 / duration_scale) / 1000.0,
            });
    }
    const double decay_step =
        has_decay ? std::exp(-static_cast<double>(recipe.decay_per_second) /
                             (static_cast<double>(body_pcm_sample_rate) * duration_scale))
                  : 1.0;
    const double first_envelope_age_ms = (first_age / 48.0) / duration_scale;
    double decay_gain = has_decay ? std::exp(-static_cast<double>(recipe.decay_per_second) *
                                             first_envelope_age_ms / 1000.0)
                                  : 1.0;
    std::optional<RollingNoise> noise;
    if (has_noise) {
        noise.emplace(SeededNoise{voice.stable_id},
            static_cast<std::int64_t>(std::floor(first_age)));
    }

    for (std::uint32_t frame = window.frames.first; frame < window.frames.end; ++frame) {
        const double age = window.age_start + static_cast<double>(frame);
        const float noise_sample =
            noise ? noise->at(static_cast<std::int64_t>(std::floor(age))) : 0.0F;

        const double age_ms = age / 48.0;
        const EnvelopeResult env = envelope(recipe, envelope_values, age_ms);
        const float sample = render_voice_sample(voice,
            env,
            decay_gain,
            {
                .modulation = modulation.sine,
                .first = first.sine,
                .second = has_second_carrier ? second.sine : first.sine,
            },
            noise_sample);
        const std::size_t index = static_cast<std::size_t>(frame) * 2U;
        if ((voice.channel_mask & 0x1U) != 0) {
            stereo[index] += static_cast<float>(static_cast<double>(sample) * gain);
        }
        if ((voice.channel_mask & 0x2U) != 0) {
            stereo[index + 1U] += static_cast<float>(static_cast<double>(sample) * gain);
        }
        if (has_decay) {
            decay_gain *= decay_step;
        }

        // Advance through pulse gaps so the next pulse keeps its absolute phase.
        first.advance();
        if (has_second_carrier) {
            second.advance();
        }
        if (has_modulation) {
            modulation.advance();
        }
    }
}

void accumulate_voice(const std::optional<BodyVoice>& optional_voice,
    MonoTime begins_at,
    std::span<float> stereo,
    std::uint32_t frame_count,
    bool& contains_held) noexcept {
    if (!optional_voice) {
        return;
    }
    const BodyVoice& voice = *optional_voice;
    const VoiceScales scales{
        .scale = safe_scale(voice.scale),
        .hd_scale = safe_hd_scale(voice.hd_scale),
    };
    const WaveRecipe& recipe = voice.wave;
    if (scales.scale <= 0.0F || scales.hd_scale <= 0.0F || recipe.frequency <= 0.0F ||
        voice.duration_ms == 0) {
        return;
    }

    const double age_start = age_at_window_start(begins_at, voice.started_at);
    const double duration_frames = static_cast<double>(voice.duration_ms) * 48.0;
    const AgeWindow age_window{.first_age = age_start, .frame_count = frame_count};
    const VoiceFrameRange frames{
        .first = first_frame_at_age_or_after(age_window, 0.0),
        .end = first_frame_at_age_or_after(age_window, duration_frames),
    };
    if (frames.first >= frames.end) {
        return;
    }
    if (voice.held) {
        contains_held = true;
    }
    const VoiceRenderWindow window{.age_start = age_start, .frames = frames};
    if (!voice.held && recipe.audio_envelope.enabled && valid_audio_recipe(recipe)) {
        accumulate_audio_voice(voice, stereo, window);
        return;
    }
    accumulate_legacy_voice(voice, stereo, window, scales);
}

float program_max_scale(const BodyProgram& program) noexcept {
    float maximum = 0.0F;
    for (const auto& voice : program.voices) {
        if (voice) {
            maximum = std::max(maximum, safe_scale(voice->scale));
        }
    }
    return maximum;
}
} // namespace

BodyPcmWindow build_body_pcm(const BodyProgram& program,
    MonoTime begins_at,
    std::span<float> stereo_destination) noexcept {
    const auto available_frames = stereo_destination.size() / 2U;
    const std::uint32_t frame_count = static_cast<std::uint32_t>(
        std::min<std::size_t>(available_frames, static_cast<std::size_t>(body_pcm_max_frames)));
    const std::size_t sample_count = static_cast<std::size_t>(frame_count) * 2U;
    std::fill_n(stereo_destination.begin(), sample_count, 0.0F);
    if (frame_count == 0) {
        return {};
    }

    bool contains_held = false;
    const float maximum_scale = program_max_scale(program);
    for (const auto& voice : program.voices) {
        accumulate_voice(voice, begins_at, stereo_destination, frame_count, contains_held);
    }

    const double knee =
        static_cast<double>(soft_knee_fraction) * static_cast<double>(maximum_scale);
    const double room =
        (static_cast<double>(maximum_peak) - static_cast<double>(soft_knee_fraction)) *
        static_cast<double>(maximum_scale);
    for (std::size_t index = 0; index < sample_count; ++index) {
        float& sample = stereo_destination[index];
        if (!std::isfinite(sample) || maximum_scale <= 0.0F) {
            sample = 0.0F;
        } else if (std::abs(static_cast<double>(sample)) > knee) {
            const double magnitude = std::abs(static_cast<double>(sample));
            const double excess = magnitude - knee;
            const double compressed = knee + ((room * excess) / (room + excess));
            sample = std::copysign(static_cast<float>(compressed), sample);
        }
    }
    return {
        .frame_count = frame_count,
        .contains_held = contains_held,
    };
}
} // namespace inputnext::adapters
