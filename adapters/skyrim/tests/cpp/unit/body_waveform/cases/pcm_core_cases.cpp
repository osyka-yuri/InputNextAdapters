#include "body_waveform/body_waveform_test_cases.hpp"
#include "body_waveform/body_waveform_test_support.hpp"
#include "skyrim_adapter/body_waveform.hpp"
#include "skyrim_adapter/model.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

namespace skyrim_adapter::body_waveform_tests {
namespace {
using namespace std::chrono_literals;
}

void test_empty_and_bounded_destinations() {
    BodyPlan const empty{};
    std::vector<float> long_output((std::size_t{body_pcm_max_frames} * 2U) + 3U);
    std::ranges::fill(long_output, 9.0F);
    const BodyPcmWindow long_window = build_body_pcm(empty, at(0), long_output);
    check(long_window.frame_count == body_pcm_max_frames && !long_window.contains_held,
        "long destinations are bounded to a neutral 200 ms window");
    check(std::all_of(long_output.begin(),
              long_output.begin() + std::size_t{body_pcm_max_frames} * 2U,
              [](float sample) { return sample == 0.0F; }),
        "neutral windows are exact zero");
    check(long_output[std::size_t{body_pcm_max_frames} * 2U] == 9.0F && long_output.back() == 9.0F,
        "samples beyond the bounded stereo window remain untouched");

    std::array<float, 9> short_output{};
    short_output.fill(7.0F);
    const BodyPcmWindow short_window = build_body_pcm(empty, at(0), short_output);
    check(short_window.frame_count == 4, "odd short destinations use only complete stereo frames");
    check(std::all_of(short_output.begin(),
              short_output.begin() + 8,
              [](float sample) { return sample == 0.0F; }) &&
              short_output[8] == 7.0F,
        "short writes stay within the complete stereo-frame boundary");
}

void test_determinism_range_stereo_and_dc() {
    BodyPlan plan{};
    BodyCue const voice = cue(BodyCueKind::voice_fire, 0x12345678U, 160ms, 0.8F);
    plan.body_transient = voice;
    std::vector<float> first(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> second(std::size_t{body_pcm_max_frames} * 2U);
    const BodyPcmWindow first_window = build_body_pcm(plan, at(0), first);
    const BodyPcmWindow second_window = build_body_pcm(plan, at(0), second);
    check(first_window.frame_count == body_pcm_max_frames &&
              second_window.frame_count == first_window.frame_count,
        "builder returns the requested bounded window length");
    check(first == second, "cue ID and original cue time produce deterministic samples");

    float peak = 0.0F;
    double mean = 0.0;
    double squared_sum = 0.0;
    for (const float sample : first) {
        check(std::isfinite(sample) && sample >= -1.0F && sample <= 1.0F,
            "all generated samples remain finite and normalized");
        peak = std::max(peak, std::abs(sample));
        mean += static_cast<double>(sample);
        squared_sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
    mean /= static_cast<double>(first.size());
    const double rms = std::sqrt(squared_sum / static_cast<double>(first.size()));
    check(peak <= (0.75F * voice.scale) + 1.0e-6F,
        "single-position output stays below the rumble-scale peak limit");
    check(peak > 0.20F && rms > 0.10,
        "strengthened body position keeps a useful peak and RMS level at 0.8 scale");
    check(std::abs(mean) < 0.002, "windowed tone and smoothed noise stay near zero DC");
    check(first[0] == 0.0F && first[(std::size_t{body_pcm_max_frames} * 2U) - 2U] == 0.0F,
        "natural cue onset and end are exact zero");

    BodyPlan held_plan{};
    held_plan.held[0] = voice;
    BodyPlan hand_plan{};
    hand_plan.hand_transient[0] = voice;
    std::vector<float> held(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> hand(std::size_t{body_pcm_max_frames} * 2U);
    (void)build_body_pcm(held_plan, at(0), held);
    (void)build_body_pcm(hand_plan, at(0), hand);
    float held_hand_difference = 0.0F;
    float body_gain_difference = 0.0F;
    float body_stereo_difference = 0.0F;
    for (std::size_t index = 0; index < first.size(); index += 2U) {
        held_hand_difference = std::max(held_hand_difference, std::abs(held[index] - hand[index]));
        body_gain_difference =
            std::max(body_gain_difference, std::abs(first[index] - (held[index] * 1.5F)));
        body_stereo_difference =
            std::max(body_stereo_difference, std::abs(first[index] - first[index + 1U]));
    }
    check(held_hand_difference < 1.0e-6F && body_gain_difference < 1.0e-6F &&
              body_stereo_difference == 0.0F,
        "held and hand gains match, with body position 1.5 times stronger");

    BodyPlan full_voice_plan{};
    full_voice_plan.body_transient = voice;
    full_voice_plan.body_transient->scale = 1.0F;
    std::vector<float> full_voice(std::size_t{body_pcm_max_frames} * 2U);
    (void)build_body_pcm(full_voice_plan, at(0), full_voice);
    float full_voice_peak = 0.0F;
    double full_voice_squared_sum = 0.0;
    for (float const sample : full_voice) {
        full_voice_peak = std::max(full_voice_peak, std::abs(sample));
        full_voice_squared_sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
    const double full_voice_rms =
        std::sqrt(full_voice_squared_sum / static_cast<double>(full_voice.size()));
    check(full_voice_peak > 0.25F && full_voice_peak <= 0.75F + 1.0e-6F && full_voice_rms > 0.12,
        "full-scale body cue has a strengthened peak and RMS below the normalized ceiling");
    BodyPlan reduced_voice_plan{};
    reduced_voice_plan.body_transient = voice;
    reduced_voice_plan.body_transient->scale = 0.4F;
    std::vector<float> reduced_voice(std::size_t{body_pcm_max_frames} * 2U);
    (void)build_body_pcm(reduced_voice_plan, at(0), reduced_voice);
    float single_scale_difference = 0.0F;
    for (std::size_t index = 0; index < first.size(); ++index) {
        single_scale_difference = std::max(single_scale_difference,
            std::abs(reduced_voice[index] - (full_voice[index] * 0.4F)));
    }
    check(single_scale_difference < 1.0e-6F,
        "an unsaturated single cue at 0.4 scale preserves proportional user attenuation");

    std::vector<float> fire(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> frost(std::size_t{body_pcm_max_frames} * 2U);
    BodyPlan spell{};
    BodyCue spell_cue = cue(BodyCueKind::spell_release, 21, 55ms);
    spell_cue.spell = SpellKind::fire;
    spell.hand_transient[0] = spell_cue;
    (void)build_body_pcm(spell, at(0), fire);
    spell_cue.spell = SpellKind::frost;
    spell.hand_transient[0] = spell_cue;
    (void)build_body_pcm(spell, at(0), frost);
    check(fire != frost, "known elemental families retain distinct authored waveforms");
    bool right_channel_silent = true;
    for (std::size_t frame = 0; frame < fire.size() / 2U; ++frame) {
        right_channel_silent = right_channel_silent && fire[(frame * 2U) + 1U] == 0.0F;
    }
    check(right_channel_silent, "left-hand cues are isolated to the left stereo actuator channel");
}

void test_complete_elapsed_time_slices() {
    BodyPlan plan{};
    auto held_cue = cue(BodyCueKind::concentration, 71, 400ms);
    held_cue.spell = SpellKind::fire;
    plan.held[0] = held_cue;
    plan.body_transient = cue(BodyCueKind::voice_fire, 72, 160ms);

    std::vector<float> initial(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> renewed(std::size_t{body_pcm_max_frames} * 2U);
    const BodyPcmWindow initial_window = build_body_pcm(plan, at(0), initial);
    const BodyPcmWindow renewed_window = build_body_pcm(plan, at(100), renewed);
    check(initial_window.contains_held && renewed_window.contains_held,
        "held presence is reported for every window that intersects its cue");

    const std::size_t offset_100_ms = std::size_t{100} * 48U;
    check(initial[(offset_100_ms * 2U) + 1U] == renewed[1],
        "renewal preserves the original voice carrier phase and seeded noise");
    check(std::any_of(renewed.begin() + 1,
              renewed.begin() + std::size_t{60} * 48U * 2U,
              [](float sample) { return sample != 0.0F; }),
        "the renewed window includes the voice cue's original remaining tail");
    bool voice_finished = true;
    for (std::size_t frame = std::size_t{60} * 48U; frame < body_pcm_max_frames; ++frame) {
        voice_finished = voice_finished && renewed[(frame * 2U) + 1U] == 0.0F;
    }
    check(voice_finished,
        "a 160 ms voice cue is not replayed after its original end at a 100 ms renewal");

    std::vector<float> full(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> slice(std::size_t{body_pcm_max_frames} * 2U);
    BodyPlan transient{};
    transient.body_transient = cue(BodyCueKind::bow_release, 99, 55ms);
    (void)build_body_pcm(transient, at(0), full);
    (void)build_body_pcm(transient, at(20), slice);
    check(full[std::size_t{20} * 48U * 2U] == slice[0] &&
              full[(std::size_t{20} * 48U * 2U) + 1U] == slice[1] &&
              (slice[0] != 0.0F || slice[1] != 0.0F),
        "an arbitrary active slice continues its original envelope without a new onset window");
    check(std::all_of(full.begin() + std::size_t{55} * 48U * 2U,
              full.end(),
              [](float sample) { return sample == 0.0F; }),
        "transient contribution ends at its original duration inside a longer output window");
}

} // namespace skyrim_adapter::body_waveform_tests
