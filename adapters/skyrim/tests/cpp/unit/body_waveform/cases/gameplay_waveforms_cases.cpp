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
#include <cstdint>
#include <vector>

namespace skyrim_adapter::body_waveform_tests {
namespace {
using namespace std::chrono_literals;
}

void test_voice_level_waveforms_are_ordered_and_window_safe() {
    struct LevelCase {
        ShoutLevel level;
        std::uint32_t duration_ms;
    };

    constexpr std::array cases{
        LevelCase{.level = ShoutLevel::one_word, .duration_ms = 110},
        LevelCase{.level = ShoutLevel::two_words, .duration_ms = 170},
        LevelCase{.level = ShoutLevel::three_words, .duration_ms = 240},
    };
    constexpr std::size_t slice_offset_frames = std::size_t{60} * std::size_t{48};
    constexpr std::size_t overlap_frames = body_pcm_max_frames - slice_offset_frames;
    std::array<double, cases.size()> energy{};
    std::vector<float> active(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> continued(overlap_frames * 2U);
    std::vector<float> expired(std::size_t{64} * 2U);

    for (std::size_t index = 0; index < cases.size(); ++index) {
        const LevelCase& item = cases[index];
        BodyCue shout = cue(BodyCueKind::voice_fire,
            0x5A017U,
            std::chrono::milliseconds{item.duration_ms},
            1.0F);
        shout.shout_level = item.level;
        BodyPlan plan{};
        plan.body_transient = shout;

        (void)build_body_pcm(plan, at(0), active);
        bool channels_match = true;
        float peak = 0.0F;
        double squared_sum = 0.0;
        for (std::size_t frame = 0; frame < active.size(); frame += 2U) {
            const float left = active[frame];
            const float right = active[frame + 1U];
            channels_match = channels_match && left == right;
            peak = std::max({peak, std::abs(left), std::abs(right)});
            squared_sum += (static_cast<double>(left) * static_cast<double>(left)) +
                           (static_cast<double>(right) * static_cast<double>(right));
            check(std::isfinite(left) && std::isfinite(right),
                "each shout-level waveform remains finite over a full PCM window");
        }
        energy[index] = squared_sum;
        check(channels_match, "shout feedback remains symmetric across body actuators");
        check(peak > 0.0F && peak <= 0.75F + 1.0e-6F,
            "shout waveform stays audible below the body limiter");

        (void)build_body_pcm(plan, at(60), continued);
        bool slices_match = true;
        for (std::size_t sample = 0; sample < continued.size(); ++sample) {
            slices_match =
                slices_match && continued[sample] == active[(slice_offset_frames * 2U) + sample];
        }
        check(slices_match, "shout waveform renders consistently when PCM windows renew mid-cue");

        (void)build_body_pcm(plan, at(item.duration_ms), expired);
        check(std::ranges::all_of(expired, [](float sample) { return sample == 0.0F; }),
            "an expired shout waveform is exact silence");
    }
    check(energy[0] < energy[1] && energy[1] < energy[2],
        "successive shout levels add measured PCM energy without duplicating spoken-word pulses");

    BodyCue const unknown = cue(BodyCueKind::voice_fire, 0x5A018U, 160ms, 1.0F);
    BodyCue invalid = unknown;
    invalid.shout_level = static_cast<ShoutLevel>(0xFFU);
    BodyPlan unknown_plan{};
    unknown_plan.body_transient = unknown;
    BodyPlan invalid_plan{};
    invalid_plan.body_transient = invalid;
    const auto unknown_program = make_body_program(unknown_plan);
    const auto& unknown_voice =
        test::require_value(unknown_program.voices[4], "unknown shout waveform missing");
    check(unknown_voice.wave.frequency == 80.0F && unknown_voice.wave.second_frequency == 0.0F &&
              unknown_voice.wave.second_mix == 0.0F && unknown_voice.wave.level == 0.42F &&
              unknown_voice.wave.noise == 0.04F && unknown_voice.wave.attack_ms == 3.0F &&
              unknown_voice.wave.release_ms == 26.0F && unknown_voice.wave.decay_per_second == 5.0F,
        "unknown shout levels use the fallback PCM recipe");
    std::vector<float> invalid_output(active.size());
    (void)build_body_pcm(unknown_plan, at(0), active);
    (void)build_body_pcm(invalid_plan, at(0), invalid_output);
    check(active == invalid_output, "invalid shout levels use the unknown-level PCM waveform");
}

void test_sustained_bow_tension_recipes_and_renewal() {
    const auto make_bow = [](BowPhase phase, float scale = 1.0F) {
        BodyCue result = cue(BodyCueKind::bow_tension, 0xB0A70001U, 400ms, scale);
        result.hand = Hand::left;
        result.source_kind = EquipmentKind::bow;
        result.weapon = WeaponClass::bow;
        result.source_id = 700;
        result.bow_phase = phase;
        return result;
    };

    struct FrameRange {
        std::size_t begin{};
        std::size_t end{};
    };

    const auto energy = [](const std::vector<float>& samples, FrameRange range) {
        double sum = 0.0;
        for (std::size_t frame = range.begin; frame < range.end; ++frame) {
            const double sample = samples[frame * 2U];
            sum += sample * sample;
        }
        return sum;
    };

    std::array<BodyPlan, 3> phase_plans{};
    const std::array phases{BowPhase::draw, BowPhase::attached, BowPhase::drawn};
    std::array<std::vector<float>, 3> phase_pcm{
        std::vector<float>(std::size_t{body_pcm_max_frames} * 2U),
        std::vector<float>(std::size_t{body_pcm_max_frames} * 2U),
        std::vector<float>(std::size_t{body_pcm_max_frames} * 2U),
    };
    for (std::size_t index = 0; index < phases.size(); ++index) {
        phase_plans[index].held[0] = make_bow(phases[index]);
        const BodyPcmWindow window = build_body_pcm(phase_plans[index], at(0), phase_pcm[index]);
        check(window.contains_held, "active bow phase reports sustained held PCM");
        check(energy(phase_pcm[index],
                  FrameRange{.begin = std::size_t{110} * 48U, .end = std::size_t{190} * 48U}) >
                  0.01,
            "bow phase retains measurable PCM energy beyond the first 100 ms");
        bool right_silent = true;
        for (std::size_t frame = 0; frame < body_pcm_max_frames; ++frame) {
            right_silent = right_silent && phase_pcm[index][(frame * 2U) + 1U] == 0.0F;
        }
        check(right_silent, "left-hand bow tension stays on the left stereo channel");
    }
    check(phase_pcm[0] != phase_pcm[1] && phase_pcm[1] != phase_pcm[2],
        "draw, attached and drawn phases use distinct sustained waveform recipes");
    const double early_energy = energy(phase_pcm[1],
        FrameRange{.begin = std::size_t{40} * 48U, .end = std::size_t{100} * 48U});
    const double late_energy = energy(phase_pcm[1],
        FrameRange{.begin = std::size_t{120} * 48U, .end = std::size_t{190} * 48U});
    check(late_energy > early_energy * 0.35 && late_energy < early_energy * 3.0,
        "attached phase remains bounded without transient exponential decay");

    BodyPlan extended = phase_plans[1];
    test::require_value(extended.held[0], "phase plan held cue missing").duration_ms = 600;
    std::vector<float> renewal_slice(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> extended_slice(std::size_t{body_pcm_max_frames} * 2U);
    (void)build_body_pcm(phase_plans[1], at(100), renewal_slice);
    (void)build_body_pcm(extended, at(100), extended_slice);
    check(renewal_slice == extended_slice,
        "finite observation renewal keeps the original carrier, envelope, and seeded texture");

    BodyPlan silent{};
    silent.held[0] = make_bow(BowPhase::draw, 0.0F);
    std::vector<float> zero_scale(std::size_t{body_pcm_max_frames} * 2U, 1.0F);
    (void)build_body_pcm(silent, at(0), zero_scale);
    check(std::ranges::all_of(zero_scale, [](float sample) { return sample == 0.0F; }),
        "zero-scale held bow cue produces exact silence");

    BodyPlan unsupported{};
    unsupported.held[0] = make_bow(BowPhase::releasing);
    std::vector<float> released(std::size_t{body_pcm_max_frames} * 2U, 1.0F);
    (void)build_body_pcm(unsupported, at(0), released);
    check(std::ranges::all_of(released, [](float sample) { return sample == 0.0F; }),
        "release phases have no sustained bow texture");
}

void test_heartbeat_preserves_authored_cadence() {
    BodyPlan plan{};
    plan.body_transient = cue(BodyCueKind::critical_health, 19, 200ms);
    std::vector<float> output(std::size_t{body_pcm_max_frames} * 2U);
    (void)build_body_pcm(plan, at(0), output);

    const auto has_energy = [&output](std::size_t begin_frame, std::size_t end_frame) {
        for (std::size_t frame = begin_frame; frame < end_frame; ++frame) {
            if (std::abs(output[frame * 2U]) > 0.001F) {
                return true;
            }
        }
        return false;
    };
    check(has_energy(0, std::size_t{55} * 48U) &&
              has_energy(std::size_t{155} * 48U, std::size_t{200} * 48U),
        "critical-health cue produces its two rounded beats");
    check(std::all_of(output.begin() + std::size_t{55} * 48U * 2U,
              output.begin() + std::size_t{155} * 48U * 2U,
              [](float sample) { return sample == 0.0F; }),
        "critical-health cue retains the existing 100 ms beat gap");
}

} // namespace skyrim_adapter::body_waveform_tests
