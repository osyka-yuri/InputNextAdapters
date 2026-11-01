#include "effect_recipes.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fallout4_adapter/model.hpp>
#include <inputnext_adapters/body_program.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

namespace fallout4_adapter::waveform_tests {

namespace {
[[nodiscard]] inputnext::adapters::MonoTime at(std::int64_t milliseconds) noexcept {
    return inputnext::adapters::MonoTime{} + std::chrono::milliseconds{milliseconds};
}

using PcmWindow =
    std::array<float, static_cast<std::size_t>(inputnext::adapters::body_pcm_max_frames) * 2U>;

[[nodiscard]] inputnext::adapters::BodyProgram body_program_for(
    const detail::HapticRecipe& recipe) noexcept {
    using namespace inputnext::adapters;
    BodyVoice voice{};
    voice.wave = recipe.wave;
    voice.started_at = at(1000);
    voice.duration_ms = recipe.duration_ms;
    voice.stable_id = 1;
    voice.sequence = 1;
    voice.scale = 1.0F;
    voice.hd_scale = 1.0F;
    voice.duration_scale = 1.0F;
    voice.position_gain = 1.0F;
    voice.channel_mask = 0x3U;

    BodyProgram program{};
    program.voices[0] = voice;
    return program;
}

void render_pcm(const inputnext::adapters::BodyProgram& program,
    inputnext::adapters::MonoTime begins_at,
    std::span<float> destination) {
    static_cast<void>(inputnext::adapters::build_body_pcm(program, begins_at, destination));
}

[[nodiscard]] double rendered_energy(const inputnext::adapters::BodyProgram& program,
    inputnext::adapters::MonoTime begins_at) {
    const auto samples = std::make_unique<PcmWindow>();
    render_pcm(program, begins_at, *samples);
    double energy{};
    for (const float sample : *samples) {
        energy += static_cast<double>(sample) * sample;
    }
    return energy;
}

[[nodiscard]] std::array<double, 2> tail_rms(const inputnext::adapters::BodyProgram& program) {
    constexpr std::size_t frames = inputnext::adapters::body_pcm_sample_rate / 50U;
    std::array<float, frames * 2U> delivered{};
    render_pcm(program, at(1040), delivered);
    std::array<double, 2> channels{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            const double sample = delivered[(frame * 2U) + channel];
            channels[channel] += sample * sample;
        }
    }
    for (double& energy : channels) {
        energy = std::sqrt(energy / static_cast<double>(frames));
    }
    return channels;
}
} // namespace

TEST_CASE("fallout4.waveform.recipe_envelopes_are_finite_and_bounded",
    "[unit][fallout4][waveform]") {
    using detail::Cue;
    for (std::uint8_t index = 0; index < static_cast<std::uint8_t>(Cue::count); ++index) {
        const auto cue = static_cast<Cue>(index);
        const auto recipe = detail::recipe_for(cue);
        INFO("every authored haptic cue has a positive duration bounded by the 200 ms lease");
        CHECK(recipe.duration_ms > 0);
        CHECK(recipe.duration_ms <= 200);

        INFO("every classic fallback amplitude is finite and moderate");
        CHECK(std::isfinite(recipe.fallback_low));
        CHECK(recipe.fallback_low >= 0.0F);
        CHECK(recipe.fallback_low <= 0.78F);
        CHECK(std::isfinite(recipe.fallback_high));
        CHECK(recipe.fallback_high >= 0.0F);
        CHECK(recipe.fallback_high <= 0.78F);

        for (std::uint32_t elapsed = 0; elapsed <= recipe.duration_ms; ++elapsed) {
            const float gain =
                detail::envelope_gain(recipe.wave, recipe.duration_ms, static_cast<float>(elapsed));
            INFO("authored HD and classic envelopes stay finite and normalized over their whole "
                 "lifetime");
            CHECK(std::isfinite(gain));
            CHECK(gain >= 0.0F);
            CHECK(gain <= 1.0F);
        }
        INFO("a pre-onset envelope has zero output");
        CHECK(detail::envelope_gain(recipe.wave, recipe.duration_ms, -1.0F) == 0.0F);

        INFO("an expired envelope has zero output");
        CHECK(detail::envelope_gain(recipe.wave,
                  recipe.duration_ms,
                  static_cast<float>(recipe.duration_ms)) == 0.0F);
    }
}

TEST_CASE("fallout4.waveform.transition_recipes_are_soft_brief_and_lower_priority",
    "[unit][fallout4][waveform]") {
    using detail::Cue;
    constexpr std::array transition_cues{Cue::reload_start,
        Cue::power_armor_enter,
        Cue::power_armor_exit};
    const auto shot = detail::recipe_for(Cue::generic_shot);
    const auto damage = detail::recipe_for(Cue::outgoing_hit);
    std::array<float, transition_cues.size()> frequencies{};

    for (std::size_t index = 0; index < transition_cues.size(); ++index) {
        const auto recipe = detail::recipe_for(transition_cues[index]);
        frequencies[index] = recipe.wave.frequency;
        INFO("mechanic transition accents finish within 100 ms");
        CHECK(recipe.duration_ms <= 100);

        INFO("mechanic cues remain below shot and damage voices in mixer priority");
        CHECK(recipe.priority < shot.priority);
        CHECK(recipe.priority < damage.priority);

        INFO("mechanic accents use conservative HD and classic amplitudes");
        CHECK(recipe.wave.level <= 0.42F);
        CHECK(recipe.fallback_low <= 0.24F);
        CHECK(recipe.fallback_high <= 0.20F);

        INFO("mechanic accents start and release smoothly");
        CHECK(detail::envelope_gain(recipe.wave, recipe.duration_ms, 0.0F) == 0.0F);
        CHECK(detail::envelope_gain(recipe.wave,
                  recipe.duration_ms,
                  static_cast<float>(recipe.duration_ms - 1U)) < 0.05F);
    }
    INFO("reload, power-armor entry, and power-armor exit have distinct tactile signatures");
    CHECK(frequencies[0] != frequencies[1]);
    CHECK(frequencies[1] != frequencies[2]);
    CHECK(frequencies[0] != frequencies[2]);
}

TEST_CASE("fallout4.waveform.shot_recipe_has_two_distinct_impacts", "[unit][fallout4][waveform]") {
    const auto recipe = detail::recipe_for(detail::Cue::rifle_shot);
    const float first_peak = detail::envelope_gain(recipe.wave, recipe.duration_ms, 8.0F);
    const float gap = detail::envelope_gain(recipe.wave, recipe.duration_ms, 31.0F);
    const float second_peak = detail::envelope_gain(recipe.wave, recipe.duration_ms, 42.0F);
    INFO("the authored shot waveform has a clear two-part recoil with a real inter-pulse gap");
    CHECK(first_peak > 0.0F);
    CHECK(second_peak > 0.0F);
    CHECK(gap == 0.0F);

    INFO("both recoil transients fit inside the bounded recipe lifetime");
    CHECK(recipe.wave.first_pulse_ms + recipe.wave.pulse_gap_ms + recipe.wave.second_pulse_ms <
          recipe.duration_ms);
}

TEST_CASE("fallout4.waveform.damage_recipes_keep_authored_profile_priorities",
    "[unit][fallout4][waveform]") {
    using detail::Cue;
    const auto incoming = detail::recipe_for(Cue::incoming_hit);
    const auto blocked = detail::recipe_for(Cue::blocked_hit);
    const auto crippled = detail::recipe_for(Cue::crippled_hit);
    const auto explosive = detail::recipe_for(Cue::explosive_hit);
    const auto critical = detail::recipe_for(Cue::critical_hit);

    INFO("incoming arbitration overrides do not rewrite the authored priorities of damage recipes");
    CHECK(incoming.priority == 100);
    CHECK(explosive.priority == 100);
    CHECK(blocked.priority == 68);
    CHECK(crippled.priority == 86);
    CHECK(critical.priority == 90);

    INFO("the blocked-hit profile remains the same restrained waveform and fallback");
    CHECK(blocked.wave.frequency == 142.0F);
    CHECK(blocked.wave.level == 0.52F);
    CHECK(blocked.wave.noise == 0.12F);
    CHECK(blocked.fallback_low == 0.27F);
    CHECK(blocked.fallback_high == 0.32F);

    INFO("the crippled-hit profile remains unchanged for both incoming and outgoing role "
         "arbitration");
    CHECK(crippled.wave.frequency == 62.0F);
    CHECK(crippled.wave.level == 0.62F);
    CHECK(crippled.wave.noise == 0.18F);
    CHECK(crippled.fallback_low == 0.40F);
    CHECK(crippled.fallback_high == 0.38F);
}

TEST_CASE("fallout4.waveform.short_contact_recipes_retain_renderer_energy",
    "[unit][fallout4][waveform]") {
    using detail::Cue;
    constexpr std::array short_cues{Cue::footstep,
        Cue::landing,
        Cue::blocked_hit,
        Cue::outgoing_hit,
        Cue::ordinary_step,
        Cue::sprint_step};
    constexpr std::array<std::uint16_t, short_cues.size()> expected_durations{56,
        80,
        70,
        74,
        64,
        56};

    for (std::size_t index = 0; index < short_cues.size(); ++index) {
        CAPTURE(short_cues[index]);
        const auto recipe = detail::recipe_for(short_cues[index]);
        INFO("latency-sensitive contact cue keeps its bounded authored duration");
        CHECK(recipe.duration_ms == expected_durations[index]);

        INFO("latency-sensitive contact cue keeps a smooth onset and release");
        CHECK(detail::envelope_gain(recipe.wave, recipe.duration_ms, 0.0F) == 0.0F);
        CHECK(detail::envelope_gain(recipe.wave,
                  recipe.duration_ms,
                  static_cast<float>(recipe.duration_ms - 1U)) < 0.05F);

        if (short_cues[index] == Cue::blocked_hit || short_cues[index] == Cue::outgoing_hit) {
            INFO("contact second pulse continues past the 40 ms delivery-delay boundary");
            CHECK(recipe.wave.first_pulse_ms == 26);
            CHECK(recipe.wave.pulse_gap_ms == 8);
            CHECK(recipe.wave.second_pulse_ms >
                  40U - recipe.wave.first_pulse_ms - recipe.wave.pulse_gap_ms);
        }

        const auto program = body_program_for(recipe);
        const double baseline = rendered_energy(program, at(1000));
        const double after_20_ms = rendered_energy(program, at(1020));
        const double after_40_ms = rendered_energy(program, at(1040));
        INFO("short contact cue produces actual build_body_pcm signal at its original timestamp");
        CHECK(baseline > 0.0);

        INFO("short contact cue retains at least one fifth of baseline renderer energy after a 20 "
             "ms delivery delay");
        CHECK(after_20_ms >= baseline * 0.20);

        INFO("short contact cue retains at least two percent of baseline renderer energy after a "
             "40 ms delivery delay");
        CHECK(after_40_ms >= baseline * 0.02);

        const auto active_samples = std::make_unique<PcmWindow>();
        render_pcm(program, at(1000), *active_samples);
        INFO("actual short-cue PCM stays finite and within normalized sample bounds");
        CHECK(std::ranges::all_of(*active_samples,
            [](float sample) { return std::isfinite(sample) && std::abs(sample) <= 1.0F; }));

        INFO("actual short-cue PCM begins at a smooth zero-valued onset");
        CHECK(std::abs(active_samples->front()) < 0.0001F);

        active_samples->fill(0.0F);
        const auto expired = inputnext::adapters::build_body_pcm(program,
            at(1000 + recipe.duration_ms),
            *active_samples);
        INFO("the original short-cue timestamp still expires at its exact finite recipe boundary");
        CHECK(!expired.contains_held);
        CHECK(std::ranges::all_of(*active_samples, [](float sample) { return sample == 0.0F; }));
    }
}

TEST_CASE("fallout4.waveform.quiet_steps_have_absolute_tail_rms", "[unit][fallout4][waveform]") {
    // Authored digital budget: .01 FS plus 50% headroom, not a perceptual detection threshold.
    constexpr double minimum_tail_rms = 0.015;
    for (const auto cue :
        {detail::Cue::ordinary_step, detail::Cue::sprint_step, detail::Cue::footstep}) {
        for (const float scale : {1.0F, 0.5F}) {
            for (std::uint64_t seed = 1; seed <= 16; ++seed) {
                CAPTURE(cue, scale, seed);
                auto program = body_program_for(detail::recipe_for(cue));
                for (auto& voice : program.voices) {
                    if (voice) {
                        voice->scale = PolicyConfig{}.body_scale * scale;
                        voice->stable_id = seed;
                    }
                }
                INFO("RMS uses all frames in the fixed 40-60 ms window, including silence; "
                     "each actuator channel is checked at default BodyScale and half gain");
                for (const double rms : tail_rms(program)) {
                    CHECK(rms >= minimum_tail_rms * scale);
                }
            }
        }
    }
}

TEST_CASE("fallout4.waveform.melee_contact_recipe_is_distinct_and_conservative",
    "[unit][fallout4][waveform]") {
    const auto melee = detail::recipe_for(detail::Cue::melee_hit);
    const auto remote_contact = detail::recipe_for(detail::Cue::outgoing_hit);
    INFO("confirmed melee contact has a lower, weightier tone and conservative amplitude at "
         "ordinary-contact priority");
    CHECK(melee.wave.frequency < remote_contact.wave.frequency);
    CHECK(melee.wave.level < remote_contact.wave.level);
    CHECK(melee.duration_ms <= 100);
    CHECK(melee.priority == remote_contact.priority);
}

TEST_CASE("fallout4.waveform.modulation_and_release_are_smooth", "[unit][fallout4][waveform]") {
    auto wave = detail::recipe_for(detail::Cue::landing).wave;
    wave.modulation_hz = 11.0F;
    const float before_end = detail::envelope_gain(wave, 52, 50.0F);
    const float at_end = detail::envelope_gain(wave, 52, 52.0F);
    INFO("release taper reaches silence smoothly before the exact finite endpoint");
    CHECK(before_end >= 0.0F);
    CHECK(before_end < 0.05F);
    CHECK(at_end == 0.0F);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    INFO("non-finite elapsed time cannot produce a waveform sample");
    CHECK(detail::envelope_gain(wave, 52, nan) == 0.0F);
}

} // namespace fallout4_adapter::waveform_tests
