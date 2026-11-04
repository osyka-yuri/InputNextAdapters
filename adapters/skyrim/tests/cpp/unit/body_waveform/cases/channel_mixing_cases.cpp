#include "body_waveform/body_waveform_test_cases.hpp"
#include "body_waveform/body_waveform_test_support.hpp"
#include "skyrim_adapter/body_waveform.hpp"
#include "skyrim_adapter/model.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace skyrim_adapter::body_waveform_tests {
namespace {
using namespace std::chrono_literals;

BodyPlan overloaded_plan(float scale) {
    BodyPlan plan{};
    plan.held[0] = cue(BodyCueKind::incoming_hit, 0xA11CEU, 200ms, scale);
    plan.held[1] = cue(BodyCueKind::incoming_hit, 0xA11CEU, 200ms, scale);
    plan.hand_transient[0] = cue(BodyCueKind::incoming_hit, 0xA11CEU, 200ms, scale);
    plan.hand_transient[1] = cue(BodyCueKind::incoming_hit, 0xA11CEU, 200ms, scale);
    plan.body_transient = cue(BodyCueKind::incoming_hit, 0xA11CEU, 200ms, scale);
    return plan;
}
} // namespace

void test_dual_spell_waveform_is_coherent_and_bounded() {
    BodyCue left = cue(BodyCueKind::concentration, 0xD0A1CA57U, 240ms);
    left.spell = SpellKind::fire;
    left.dual_cast = true;
    left.source_id = 42;
    left.hand = Hand::left;
    BodyCue right = left;
    right.hand = Hand::right;

    BodyPlan held{};
    held.held[0] = left;
    held.held[1] = right;
    std::vector<float> held_pcm(std::size_t{body_pcm_max_frames} * 2U);
    const BodyPcmWindow held_window = build_body_pcm(held, at(0), held_pcm);
    bool channels_match = true;
    bool held_is_audible = false;
    for (std::size_t frame = 0; frame < body_pcm_max_frames; ++frame) {
        channels_match = channels_match && held_pcm[frame * 2U] == held_pcm[(frame * 2U) + 1U];
        held_is_audible = held_is_audible || held_pcm[frame * 2U] != 0.0F;
    }
    check(held_window.contains_held && channels_match && held_is_audible,
        "dual held spell uses the same nonzero carrier and texture on both actuator channels");

    BodyCue solo_release = cue(BodyCueKind::spell_release, 0xD0A1CA57U, 55ms);
    solo_release.spell = SpellKind::fire;
    BodyPlan solo{};
    solo.body_transient = solo_release;
    BodyPlan dual = solo;
    dual.body_transient->dual_cast = true;
    std::vector<float> solo_pcm(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> dual_pcm(std::size_t{body_pcm_max_frames} * 2U);
    (void)build_body_pcm(solo, at(0), solo_pcm);
    (void)build_body_pcm(dual, at(0), dual_pcm);
    float dual_peak = 0.0F;
    bool dual_is_bilateral = true;
    for (std::size_t frame = 0; frame < body_pcm_max_frames; ++frame) {
        const float sample = dual_pcm[frame * 2U];
        dual_peak = std::max(dual_peak, std::abs(sample));
        dual_is_bilateral = dual_is_bilateral && sample == dual_pcm[(frame * 2U) + 1U];
    }
    check(dual_pcm != solo_pcm && dual_is_bilateral && dual_peak > 0.0F &&
              dual_peak <= 0.48F + 1.0e-6F,
        "dual release adds its half-frequency tone without increasing the solo level ceiling");
    check(std::all_of(dual_pcm.begin() + std::size_t{55} * 48U * 2U,
              dual_pcm.end(),
              [](float sample) { return sample == 0.0F; }),
        "dual release remains silent after its original finite recipe duration");

    dual.body_transient->scale = 0.0F;
    std::ranges::fill(dual_pcm, 1.0F);
    (void)build_body_pcm(dual, at(0), dual_pcm);
    check(std::ranges::all_of(dual_pcm, [](float sample) { return sample == 0.0F; }),
        "zero-scale dual spell output is exact silence");
}

void test_melee_swing_hand_gain_and_overlap() {
    constexpr std::array<float, 2> scales{1.0F, 0.35F};
    for (const float scale : scales) {
        BodyCue swing = cue(BodyCueKind::melee_swing, 0x51D3U, 40ms, scale);
        swing.weapon = WeaponClass::sword;
        BodyPlan body_reference{};
        body_reference.body_transient = swing;
        std::vector<float> bilateral(std::size_t{body_pcm_max_frames} * 2U);
        (void)build_body_pcm(body_reference, at(0), bilateral);

        for (const bool right_hand : {false, true}) {
            BodyPlan hand_plan{};
            hand_plan.hand_transient[right_hand ? 1U : 0U] = swing;
            std::vector<float> directed(std::size_t{body_pcm_max_frames} * 2U);
            (void)build_body_pcm(hand_plan, at(0), directed);
            bool exact_match = true;
            bool opposite_silent = true;
            for (std::size_t frame = 0; frame < body_pcm_max_frames; ++frame) {
                const std::size_t active = (frame * 2U) + (right_hand ? 1U : 0U);
                const std::size_t opposite = (frame * 2U) + (right_hand ? 0U : 1U);
                exact_match = exact_match && directed[active] == bilateral[frame * 2U];
                opposite_silent = opposite_silent && directed[opposite] == 0.0F;
            }
            check(exact_match && opposite_silent,
                "one-hand melee swing matches the previous bilateral waveform at full and reduced "
                "scale");
        }
    }

    BodyPlan overlap{};
    BodyCue swing = cue(BodyCueKind::melee_swing, 0x51D3U, 40ms, 0.8F);
    swing.weapon = WeaponClass::sword;
    overlap.hand_transient[0] = swing;
    overlap.body_transient = cue(BodyCueKind::incoming_hit, 0x1A2AC7U, 100ms, 0.2F);
    std::vector<float> combined(std::size_t{body_pcm_max_frames} * 2U);
    (void)build_body_pcm(overlap, at(0), combined);
    bool both_channels_active = false;
    float peak = 0.0F;
    for (std::size_t frame = 0; frame < body_pcm_max_frames; ++frame) {
        const float left = combined[frame * 2U];
        const float right = combined[(frame * 2U) + 1U];
        both_channels_active = both_channels_active || (left != 0.0F && right != 0.0F);
        peak = std::max({peak, std::abs(left), std::abs(right)});
    }
    check(both_channels_active && peak <= (0.75F * 0.8F) + 1.0e-6F,
        "a directed swing overlaps the bilateral impact within the existing scaled limiter bound");
}

void test_five_position_peak_and_hand_isolation() {
    BodyPlan plan{};
    auto left_held = cue(BodyCueKind::concentration, 1, 200ms);
    left_held.spell = SpellKind::fire;
    plan.held[0] = left_held;
    auto right_held = cue(BodyCueKind::concentration, 2, 200ms);
    right_held.spell = SpellKind::shock;
    plan.held[1] = right_held;
    auto left_transient = cue(BodyCueKind::spell_release, 3, 95ms);
    left_transient.spell = SpellKind::shock;
    plan.hand_transient[0] = left_transient;
    auto right_transient = cue(BodyCueKind::spell_release, 4, 75ms);
    right_transient.spell = SpellKind::frost;
    plan.hand_transient[1] = right_transient;
    plan.body_transient = cue(BodyCueKind::incoming_hit, 5, 100ms);

    std::vector<float> output(std::size_t{body_pcm_max_frames} * 2U);
    const BodyPcmWindow window = build_body_pcm(plan, at(0), output);
    float peak = 0.0F;
    for (float const sample : output) {
        check(std::isfinite(sample), "overlapping positions stay finite");
        peak = std::max(peak, std::abs(sample));
    }
    check(window.contains_held && peak <= 0.75F,
        "five simultaneous positions use fixed headroom under the normalized peak ceiling");

    BodyPlan left_only{};
    auto left_healing = cue(BodyCueKind::concentration, 11, 100ms);
    left_healing.spell = SpellKind::healing;
    left_only.held[0] = left_healing;
    std::vector<float> stereo(std::size_t{4'800} * 2U);
    (void)build_body_pcm(left_only, at(0), stereo);
    bool right_held_silent = true;
    for (std::size_t frame = 0; frame < stereo.size() / 2U; ++frame) {
        right_held_silent = right_held_silent && stereo[(frame * 2U) + 1U] == 0.0F;
    }
    check(right_held_silent, "left held cues do not leak into the right hand channel");
}

void test_overloaded_mix_limiting_scale_and_slices() {
    BodyPlan const full_scale_plan = overloaded_plan(1.0F);
    std::vector<float> full_scale(std::size_t{body_pcm_max_frames} * 2U);
    const BodyPcmWindow full_window = build_body_pcm(full_scale_plan, at(0), full_scale);
    float peak = 0.0F;
    bool above_knee = false;
    for (float const sample : full_scale) {
        check(std::isfinite(sample), "overloaded mixed output stays finite");
        const float magnitude = std::abs(sample);
        peak = std::max(peak, magnitude);
        above_knee = above_knee || magnitude > 0.60F;
    }
    check(full_window.contains_held && above_knee && peak < 0.75F,
        "overloaded mix is compressed above the knee while staying below the ceiling");

    BodyPlan single_position{};
    single_position.body_transient = cue(BodyCueKind::incoming_hit, 0xA11CEU, 200ms);
    std::vector<float> single_pcm(std::size_t{body_pcm_max_frames} * 2U);
    (void)build_body_pcm(single_position, at(0), single_pcm);
    bool observed_compression = false;
    for (std::size_t index = 0; index < full_scale.size(); ++index) {
        const float single_magnitude = std::abs(single_pcm[index]);
        const float mixed_magnitude = std::abs(full_scale[index]);
        if (single_magnitude > 0.30F && single_magnitude < 0.55F && mixed_magnitude > 0.60F) {
            observed_compression =
                observed_compression || mixed_magnitude < single_magnitude * 1.75F;
        }
    }
    check(observed_compression,
        "overloaded peaks grow less than the corresponding unsaturated single-position sum");

    const auto matches_scaled = [&full_scale](float scale, const std::vector<float>& scaled) {
        float maximum_difference = 0.0F;
        for (std::size_t index = 0; index < full_scale.size(); ++index) {
            maximum_difference =
                std::max(maximum_difference, std::abs(scaled[index] - (full_scale[index] * scale)));
        }
        return maximum_difference < 3.0e-7F;
    };
    std::vector<float> quarter_scale(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> half_scale(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> zero_scale(std::size_t{body_pcm_max_frames} * 2U, 1.0F);
    (void)build_body_pcm(overloaded_plan(0.25F), at(0), quarter_scale);
    (void)build_body_pcm(overloaded_plan(0.50F), at(0), half_scale);
    (void)build_body_pcm(overloaded_plan(0.0F), at(0), zero_scale);
    check(matches_scaled(0.25F, quarter_scale) && matches_scaled(0.50F, half_scale),
        "uniform scale changes preserve linear attenuation through an overloaded mix");
    check(std::ranges::all_of(zero_scale, [](float sample) { return sample == 0.0F; }),
        "zero-scale overloaded positions produce exact silence");

    std::vector<float> shifted_slice(std::size_t{120} * 48U * 2U);
    const std::size_t shifted_frames = std::size_t{30} * 48U;
    (void)build_body_pcm(full_scale_plan, at(30), shifted_slice);
    bool same_elapsed_samples = true;
    for (std::size_t index = 0; index < shifted_slice.size(); ++index) {
        same_elapsed_samples = same_elapsed_samples &&
                               shifted_slice[index] == full_scale[(shifted_frames * 2U) + index];
    }
    check(same_elapsed_samples,
        "active overload protection preserves samples at matching absolute cue times");

    BodyPlan invalid_scales = overloaded_plan(0.0F);
    invalid_scales.body_transient = cue(BodyCueKind::voice_fire, 33, 160ms);
    test::require_value(invalid_scales.held[0], "overloaded plan held cue missing").scale =
        std::numeric_limits<float>::quiet_NaN();
    test::require_value(invalid_scales.hand_transient[0],
        "overloaded plan hand transient cue missing")
        .scale = std::numeric_limits<float>::infinity();
    BodyPlan valid_only{};
    valid_only.body_transient =
        test::require_value(invalid_scales.body_transient, "voice fire cue missing");
    std::vector<float> mixed_invalid(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> only_valid(std::size_t{body_pcm_max_frames} * 2U);
    (void)build_body_pcm(invalid_scales, at(0), mixed_invalid);
    (void)build_body_pcm(valid_only, at(0), only_valid);
    check(mixed_invalid == only_valid,
        "nonfinite cue scales are ignored without changing valid mixed output");
}

} // namespace skyrim_adapter::body_waveform_tests
