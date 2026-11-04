#include "body_waveform/body_waveform_test_cases.hpp"
#include "body_waveform/body_waveform_test_support.hpp"
#include "skyrim_adapter/body_waveform.hpp"
#include "skyrim_adapter/content_observation.hpp"
#include "skyrim_adapter/model.hpp"

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

void test_footstep_waveforms_are_finite_bounded_and_finite_duration() {
    struct StepCase {
        MovementGait gait;
        bool mounted;
        std::uint32_t duration_ms;
    };

    constexpr std::array cases{
        StepCase{.gait = MovementGait::walking, .mounted = false, .duration_ms = 50},
        StepCase{.gait = MovementGait::running, .mounted = false, .duration_ms = 55},
        StepCase{.gait = MovementGait::sprinting, .mounted = false, .duration_ms = 60},
        StepCase{.gait = MovementGait::sneaking, .mounted = false, .duration_ms = 55},
        StepCase{.gait = MovementGait::walking, .mounted = true, .duration_ms = 60},
        StepCase{.gait = MovementGait::running, .mounted = true, .duration_ms = 65},
        StepCase{.gait = MovementGait::sprinting, .mounted = true, .duration_ms = 70},
    };
    std::vector<float> active(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> expired(std::size_t{32} * 2U);
    std::vector<float> zero_scale(std::size_t{body_pcm_max_frames} * 2U, 1.0F);
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const StepCase& item = cases[index];
        BodyCue step = cue(BodyCueKind::footstep,
            500U + index,
            std::chrono::milliseconds{item.duration_ms},
            1.0F);
        step.gait = item.gait;
        step.mounted = item.mounted;
        BodyPlan plan{};
        plan.body_transient = step;
        (void)build_body_pcm(plan, at(0), active);
        const bool nonzero =
            std::ranges::any_of(active, [](float sample) { return std::abs(sample) > 0.0001F; });
        const bool legal = std::ranges::all_of(active,
            [](float sample) { return std::isfinite(sample) && std::abs(sample) <= 0.75F; });
        bool bilateral = true;
        for (std::size_t frame = 0; frame < active.size(); frame += 2U) {
            bilateral = bilateral && active[frame] == active[frame + 1U];
        }
        check(nonzero,
            "each supported player and mount gait has an energized waveform at scale one");
        check(legal, "footstep waveforms remain finite and inside the .75 body peak limit");
        check(bilateral, "footstep feedback stays bilateral through the body transient position");

        (void)build_body_pcm(plan, at(item.duration_ms), expired);
        check(std::ranges::all_of(expired, [](float sample) { return sample == 0.0F; }),
            "expired footstep waveforms produce exact silence");

        step.scale = 0.0F;
        plan.body_transient = step;
        std::ranges::fill(zero_scale, 1.0F);
        (void)build_body_pcm(plan, at(0), zero_scale);
        check(std::ranges::all_of(zero_scale, [](float sample) { return sample == 0.0F; }),
            "zero-scale footstep waveforms produce exact silence");
    }

    for (const StepCase quiet : {
             StepCase{.gait = MovementGait::unknown, .mounted = false, .duration_ms = 25},
             StepCase{.gait = MovementGait::sneaking, .mounted = true, .duration_ms = 35},
         }) {
        BodyCue step =
            cue(BodyCueKind::footstep, 600U, std::chrono::milliseconds{quiet.duration_ms}, 1.0F);
        step.gait = quiet.gait;
        step.mounted = quiet.mounted;
        BodyPlan plan{};
        plan.body_transient = step;
        (void)build_body_pcm(plan, at(0), active);
        check(std::ranges::all_of(active, [](float sample) { return sample == 0.0F; }),
            "unknown or unsupported mounted gait remains silent");
    }
}

void test_world_waveforms_are_finite_limited_and_silent_after_expiry() {
    constexpr std::array cues{
        WorldCueKind::stamina_depleted,
        WorldCueKind::stagger_entry,
        WorldCueKind::knockdown_entry,
        WorldCueKind::killmove_entry,
        WorldCueKind::landing,
    };
    std::vector<float> active(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> expired(std::size_t{32} * 2U);
    for (const WorldCueKind kind : cues) {
        BodyCue world_cue =
            cue(BodyCueKind::none, static_cast<std::uint64_t>(kind) + 101U, 200ms, 1.0F);
        world_cue.world_kind = kind;
        BodyPlan plan{};
        plan.body_transient = world_cue;
        (void)build_body_pcm(plan, at(0), active);
        const bool nonzero =
            std::ranges::any_of(active, [](float sample) { return std::abs(sample) > 0.0001F; });
        const bool legal = std::ranges::all_of(active,
            [](float sample) { return std::isfinite(sample) && std::abs(sample) <= 0.75F; });
        check(nonzero, "every world waveform is nonzero at body scale one");
        check(legal, "every world waveform remains finite and inside the body peak limit");

        (void)build_body_pcm(plan, at(200), expired);
        check(std::ranges::all_of(expired, [](float sample) { return sample == 0.0F; }),
            "expired world waveforms produce silence");
    }
}

void test_content_waveforms_are_finite_limited_and_silent_after_expiry() {
    struct RecipeCase {
        BodyCueKind kind;
        BeastForm beast;
        unsigned stage;
        std::uint32_t duration_ms;
    };

    constexpr std::array cases{
        RecipeCase{.kind = BodyCueKind::survival_cold,
            .beast = BeastForm::none,
            .stage = 3,
            .duration_ms = 380},
        RecipeCase{.kind = BodyCueKind::survival_hunger,
            .beast = BeastForm::none,
            .stage = 4,
            .duration_ms = 380},
        RecipeCase{.kind = BodyCueKind::survival_exhaustion,
            .beast = BeastForm::none,
            .stage = 5,
            .duration_ms = 380},
        RecipeCase{.kind = BodyCueKind::transformation,
            .beast = BeastForm::werewolf,
            .stage = 0,
            .duration_ms = 300},
        RecipeCase{
            .kind = BodyCueKind::transformation,
            .beast = BeastForm::vampire_lord,
            .stage = 0,
            .duration_ms = 300,
        },
    };
    std::vector<float> active(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> expired(std::size_t{32} * 2U);
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const RecipeCase& item = cases[index];
        BodyCue content_cue =
            cue(item.kind, 301U + index, std::chrono::milliseconds{item.duration_ms}, 1.0F);
        content_cue.beast = item.beast;
        content_cue.content_stage = item.stage;
        BodyPlan plan{};
        plan.body_transient = content_cue;
        (void)build_body_pcm(plan, at(0), active);
        const bool nonzero =
            std::ranges::any_of(active, [](float sample) { return std::abs(sample) > 0.0001F; });
        const bool legal = std::ranges::all_of(active,
            [](float sample) { return std::isfinite(sample) && std::abs(sample) <= 0.75F; });
        check(nonzero, "every content recipe is nonzero at body scale one");
        check(legal, "every content recipe remains finite and inside the body peak limit");

        (void)build_body_pcm(plan, at(item.duration_ms), expired);
        check(std::ranges::all_of(expired, [](float sample) { return sample == 0.0F; }),
            "expired content recipes produce silence");
    }
}

} // namespace skyrim_adapter::body_waveform_tests
