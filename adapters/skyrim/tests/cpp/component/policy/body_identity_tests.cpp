#include "policy/support/policy_test_support.hpp"
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <skyrim_adapter/content_observation.hpp>
#include <skyrim_adapter/model.hpp>

namespace skyrim_adapter::tests::policy {
namespace {

BodyCue make_base_held_cue() {
    BodyCue held{};
    held.kind = BodyCueKind::concentration;
    held.hand = Hand::left;
    held.spell = SpellKind::fire;
    held.started_at = at(8000);
    held.duration_ms = 200;
    held.stable_id = 91;
    held.scale = 0.8F;
    return held;
}

BodyCue make_bow_held_cue() {
    BodyCue held = make_base_held_cue();
    held.kind = BodyCueKind::bow_tension;
    held.bow_phase = BowPhase::attached;
    return held;
}

BodyCue make_base_transformation_cue() {
    BodyCue transformation{};
    transformation.kind = BodyCueKind::transformation;
    transformation.beast = BeastForm::werewolf;
    transformation.started_at = at(8100);
    transformation.duration_ms = 300;
    return transformation;
}

void check_concentration_identity() {
    const BodyCue held = make_base_held_cue();
    BodyPlan first{};
    first.held[0] = held;
    BodyPlan renewed = first;
    auto renewed_held = held;
    renewed_held.duration_ms = 300;
    renewed.held[0] = renewed_held;
    INFO(
        "identity comparison ignores the moving finite horizon of an unchanged concentration hold");
    CHECK(same_body_plan_identity(first, renewed));

    BodyPlan confirmed_dual = first;
    auto dual_held = held;
    dual_held.dual_cast = true;
    confirmed_dual.held[0] = dual_held;
    INFO("dual-cast semantics participate in held cue identity");
    CHECK(!same_body_plan_identity(first, confirmed_dual));
}

void check_bow_tension_identity() {
    const BodyCue held = make_bow_held_cue();
    BodyPlan bow_first{};
    bow_first.held[0] = held;
    BodyPlan bow_renewed = bow_first;
    auto bow_renewed_held = held;
    bow_renewed_held.duration_ms = 300;
    bow_renewed.held[0] = bow_renewed_held;
    INFO("identity comparison preserves an unchanged bow phase across finite horizon renewal");
    CHECK(same_body_plan_identity(bow_first, bow_renewed));

    auto bow_drawn_held = held;
    bow_drawn_held.bow_phase = BowPhase::drawn;
    bow_renewed.held[0] = bow_drawn_held;
    INFO("bow phase changes produce a distinct plan identity");
    CHECK(!same_body_plan_identity(bow_first, bow_renewed));
}

void check_transient_duration_identity() {
    BodyCue transient = make_bow_held_cue();
    transient.kind = BodyCueKind::spell_release;
    transient.duration_ms = 55;
    BodyPlan original_transient{};
    original_transient.hand_transient[0] = transient;
    BodyPlan changed_transient = original_transient;
    auto changed_cue = transient;
    changed_cue.duration_ms = 60;
    changed_transient.hand_transient[0] = changed_cue;
    INFO("identity comparison retains the authored duration of finite transient cues");
    CHECK(!same_body_plan_identity(original_transient, changed_transient));
}

void check_transformation_identity() {
    const BodyCue transformation = make_base_transformation_cue();
    BodyPlan werewolf{};
    werewolf.body_transient = transformation;
    BodyPlan vampire = werewolf;
    auto vampire_cue = transformation;
    vampire_cue.beast = BeastForm::vampire_lord;
    vampire.body_transient = vampire_cue;
    INFO("verified beast identity distinguishes typed transformation cues");
    CHECK(!same_body_plan_identity(werewolf, vampire));
}

void check_survival_stage_identity() {
    BodyCue transformation = make_base_transformation_cue();
    transformation.kind = BodyCueKind::survival_cold;
    transformation.beast = BeastForm::none;
    transformation.content_stage = 3;
    BodyPlan stage_three{};
    stage_three.body_transient = transformation;
    BodyPlan stage_four = stage_three;
    auto stage_four_cue = transformation;
    stage_four_cue.content_stage = 4;
    stage_four.body_transient = stage_four_cue;
    INFO("Survival stage distinguishes otherwise identical transient cues");
    CHECK(!same_body_plan_identity(stage_three, stage_four));
}

void check_footstep_identity() {
    BodyCue footstep = make_bow_held_cue();
    footstep.kind = BodyCueKind::footstep;
    footstep.gait = MovementGait::walking;
    footstep.mounted = false;
    BodyPlan player_step{};
    player_step.body_transient = footstep;
    BodyPlan faster_step = player_step;
    auto running_step = footstep;
    running_step.gait = MovementGait::running;
    faster_step.body_transient = running_step;
    INFO("footstep gait participates in body cue identity");
    CHECK(!same_body_plan_identity(player_step, faster_step));

    faster_step = player_step;
    auto mounted_step = footstep;
    mounted_step.mounted = true;
    faster_step.body_transient = mounted_step;
    INFO("player and mount footsteps retain distinct body cue identities");
    CHECK(!same_body_plan_identity(player_step, faster_step));
}

void check_voice_identity() {
    BodyCue one_word_shout{};
    one_word_shout.kind = BodyCueKind::voice_fire;
    one_word_shout.shout_level = ShoutLevel::one_word;
    one_word_shout.started_at = at(8200);
    one_word_shout.duration_ms = 110;
    BodyPlan one_word_plan{};
    one_word_plan.body_transient = one_word_shout;
    BodyPlan two_word_plan = one_word_plan;
    two_word_plan.body_transient->shout_level = ShoutLevel::two_words;
    INFO(
        "shout level participates in body cue identity so a different level replaces its waveform");
    CHECK(!same_body_plan_identity(one_word_plan, two_word_plan));
}

} // namespace

TEST_CASE("skyrim.policy.body_plan_identity", "[component][skyrim][policy]") {
    check_concentration_identity();
    check_bow_tension_identity();
    check_transient_duration_identity();
    check_transformation_identity();
    check_survival_stage_identity();
    check_footstep_identity();
    check_voice_identity();
}

} // namespace skyrim_adapter::tests::policy
