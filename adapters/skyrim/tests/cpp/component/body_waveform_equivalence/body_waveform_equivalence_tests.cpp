#include "skyrim_adapter/body_waveform.hpp"
#include "skyrim_adapter/model.hpp"
#include "support/test_runner.hpp"
#include "test_support.hpp"

#include <inputnext_adapters/body_program.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
using namespace std::chrono_literals;
using skyrim_adapter::BodyCue;
using skyrim_adapter::BodyCueKind;
using skyrim_adapter::BodyPlan;
using skyrim_adapter::MonoTime;

int failures{};

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

BodyCue cue(BodyCueKind kind, std::uint64_t stable_id, std::chrono::milliseconds duration) {
    BodyCue result{};
    result.kind = kind;
    result.started_at = MonoTime{};
    result.duration_ms = static_cast<std::uint32_t>(duration.count());
    result.stable_id = stable_id;
    result.sequence = stable_id + 1;
    result.source_id = static_cast<std::uint32_t>(stable_id + 2);
    result.priority = 3;
    result.scale = 0.8F;
    result.hd_scale = 1.25F;
    result.duration_scale = 1.5F;
    return result;
}

void test_physical_mapping_and_wrapper() {
    BodyPlan plan{};
    auto left_held = cue(BodyCueKind::concentration, 11, 300ms);
    left_held.spell = skyrim_adapter::SpellKind::fire;
    left_held.frequency_hz = 222.0F;
    plan.held[0] = left_held;

    auto right_held = cue(BodyCueKind::bow_tension, 21, 500ms);
    right_held.bow_phase = skyrim_adapter::BowPhase::drawn;
    plan.held[1] = right_held;

    auto left_transient = cue(BodyCueKind::melee_swing, 31, 70ms);
    left_transient.weapon = skyrim_adapter::WeaponClass::sword;
    plan.hand_transient[0] = left_transient;

    plan.hand_transient[1] = cue(BodyCueKind::bow_release, 41, 80ms);
    plan.body_transient = cue(BodyCueKind::incoming_hit, 51, 90ms);

    const inputnext::adapters::BodyProgram program = skyrim_adapter::make_body_program(plan);
    const auto& voice0 = skyrim_adapter::test::require_value(program.voices[0], "voice 0 missing");
    const auto& voice1 = skyrim_adapter::test::require_value(program.voices[1], "voice 1 missing");
    const auto& voice2 = skyrim_adapter::test::require_value(program.voices[2], "voice 2 missing");
    const auto& voice3 = skyrim_adapter::test::require_value(program.voices[3], "voice 3 missing");
    const auto& voice4 = skyrim_adapter::test::require_value(program.voices[4], "voice 4 missing");

    check(voice0.held && voice0.channel_mask == 0x1U && voice0.position_gain == 0.50F,
        "left held recipe maps to left routing with the existing held gain");
    check(voice0.wave.frequency == 222.0F, "physical recipe keeps the authored frequency override");
    check(voice1.held && voice1.channel_mask == 0x2U, "right held recipe maps to right routing");
    check(!voice2.held && voice2.channel_mask == 0x1U && voice2.position_gain == 0.75F,
        "melee swing keeps the existing left transient route and gain");
    check(!voice3.held && voice3.channel_mask == 0x2U && voice3.position_gain == 0.50F,
        "right transient keeps its existing route and gain");
    check(!voice4.held && voice4.channel_mask == 0x3U && voice4.position_gain == 0.75F,
        "body transient keeps its existing bilateral route and gain");
    check(voice0.stable_id == left_held.stable_id && voice0.sequence == left_held.sequence &&
              voice0.source_id == left_held.source_id && voice0.priority == left_held.priority,
        "mapping carries physical voice provenance");

    std::vector<float> wrapped(std::size_t{skyrim_adapter::body_pcm_max_frames} * 2U);
    std::vector<float> shared(wrapped.size());
    (void)skyrim_adapter::build_body_pcm(plan, MonoTime{} + 15ms, wrapped);
    (void)inputnext::adapters::build_body_pcm(program, MonoTime{} + 15ms, shared);
    check(wrapped == shared, "Skyrim compatibility wrapper renders the mapped shared program");
}

void test_semantic_labels_do_not_enter_physical_identity() {
    BodyPlan none_plan{};
    none_plan.body_transient = cue(BodyCueKind::none, 90, 100ms);
    BodyPlan unsupported_footstep{};
    unsupported_footstep.body_transient = cue(BodyCueKind::footstep, 90, 100ms);
    unsupported_footstep.body_transient->gait = skyrim_adapter::MovementGait::unknown;

    const auto none_program = skyrim_adapter::make_body_program(none_plan);
    const auto footstep_program = skyrim_adapter::make_body_program(unsupported_footstep);
    check(inputnext::adapters::same_body_program_identity(none_program, footstep_program),
        "different semantic labels with identical physical recipes share identity");
}

int run_tests() {
    test_physical_mapping_and_wrapper();
    test_semantic_labels_do_not_enter_physical_identity();
    return failures == 0 ? 0 : 1;
}
} // namespace

int main() {
    return inputnext::test::run_main(__FILE__, [] { return run_tests(); });
}
