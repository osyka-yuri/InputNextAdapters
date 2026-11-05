#include "support.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <memory>
#include <skyrim_adapter/body_waveform.hpp>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>
#include <vector>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::effect_settings_tests {

TEST_CASE("skyrim.effect_settings.policy_tunes_a_surviving_world_candidate_once",
    "[component][skyrim][effect_settings]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.body[static_cast<std::size_t>(EffectId::knockdown)].duration_scale = 0.25F;
    config->effects.body[static_cast<std::size_t>(EffectId::stagger)].duration_scale = 2.0F;
    const auto policy_owner = std::make_unique<Policy>(*config);
    Policy& policy = *policy_owner;

    Observation sample = observation(at(1000));
    sample.world = WorldObservation{};
    (void)policy.step(sample, {}, at(1000));

    sample = observation(at(1100));
    sample.world = WorldObservation{};
    sample.world->swimming = true;
    sample.world->knocked_down = true;
    sample.world->staggered = true;
    const EffectFrame frame = policy.step(sample, {}, at(1160));
    INFO("policy admits the still-active lower-priority world effect after tuned expiry "
         "arbitration");
    REQUIRE(frame.body.body_transient.has_value());
    CHECK(require_value(frame.body.body_transient).world_kind == WorldCueKind::stagger_entry);

    INFO("policy tunes the selected authored world cue once and pairs its remaining classic "
         "fallback");
    CHECK(require_value(frame.body.body_transient).duration_ms == 210);
    REQUIRE(frame.rumble.has_value());
    CHECK(require_value(frame.rumble).ttl_ms == 150);
}

TEST_CASE("skyrim.effect_settings.policy_scales_body_and_triggers_independently",
    "[component][skyrim][effect_settings]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->trigger_scale = 0.5F;
    EffectAdjustment& swing_tuning =
        config->effects.body[static_cast<std::size_t>(EffectId::sword_swing)];
    swing_tuning.hd_scale = 1.5F;
    swing_tuning.classic_scale = 0.5F;
    swing_tuning.duration_scale = 1.5F;
    swing_tuning.frequency_hz = 230.0F;
    const auto policy_owner = std::make_unique<Policy>(*config);
    Policy& policy = *policy_owner;

    Action swing = action(EventKind::weapon_swing, Hand::right, at(100), 10);
    swing.source_known = true;
    swing.source_kind = EquipmentKind::weapon;
    swing.weapon = WeaponClass::sword;
    const std::array<Action, 1> swing_actions{swing};
    const EffectFrame swing_frame = policy.step(observation(at(100)), swing_actions, at(100));
    INFO("right hand transient cue missing");
    REQUIRE(swing_frame.body.hand_transient[1].has_value());
    const auto& swing_cue = require_value(swing_frame.body.hand_transient[1]);

    INFO("policy resolves the selected profile into its hand-routed HD cue");
    CHECK(swing_cue.hd_scale == 1.5F);
    CHECK(swing_cue.duration_ms == 90);
    CHECK(swing_cue.frequency_hz == 230.0F);

    INFO("classic gain and duration scale before the unchanged global rumble gain");
    REQUIRE(swing_frame.rumble.has_value());
    const auto& swing_rumble = require_value(swing_frame.rumble);
    CHECK(std::abs(swing_rumble.payload.low_frequency - 0.06F) < 1.0e-6F);
    CHECK(std::abs(swing_rumble.payload.high_frequency - 0.11F) < 1.0e-6F);
    CHECK(swing_rumble.ttl_ms == 90);

    BodyPlan hd_one{};
    auto cue_one = swing_cue;
    cue_one.hd_scale = 1.0F;
    hd_one.hand_transient[1] = cue_one;
    BodyPlan hd_tuned = hd_one;
    auto cue_tuned = cue_one;
    cue_tuned.hd_scale = 1.5F;
    hd_tuned.hand_transient[1] = cue_tuned;
    std::vector<float> one(std::size_t{body_pcm_max_frames} * 2U);
    std::vector<float> tuned(std::size_t{body_pcm_max_frames} * 2U);
    (void)build_body_pcm(hd_one, at(100), one);
    (void)build_body_pcm(hd_tuned, at(100), tuned);
    INFO("HDScale amplifies before the existing global peak limiter without changing classic gain");
    CHECK(peak(one) > 0.0F);
    CHECK(std::abs(peak(tuned) - (peak(one) * 1.5F)) < 1.0e-5F);

    const auto trigger_config = std::make_unique<PolicyConfig>();
    trigger_config->trigger_scale = 0.5F;
    TriggerAdjustment& fire_tuning =
        trigger_config->effects.triggers[static_cast<std::size_t>(TriggerId::fire)];
    fire_tuning.strength_scale = 2.0F;
    fire_tuning.start = 0.4F;
    fire_tuning.frequency_code = 125U;
    const auto trigger_policy_owner = std::make_unique<Policy>(*trigger_config);
    Policy& trigger_policy = *trigger_policy_owner;
    Action fire = action(EventKind::spell_fire, Hand::left, at(200), 20);
    fire.source_known = true;
    fire.source_kind = EquipmentKind::spell;
    fire.spell = SpellKind::fire;
    fire.cast_mode = CastMode::fire_and_forget;
    const std::array<Action, 1> fire_actions{fire};
    const EffectFrame fire_frame = trigger_policy.step(observation(at(200)), fire_actions, at(200));
    INFO("family trigger overrides compose with the global strength scale exactly once");
    REQUIRE(fire_frame.left.has_value());
    const auto& fire_trigger = require_value(fire_frame.left);
    CHECK(fire_trigger.payload.start == 0.4F);
    CHECK(fire_trigger.payload.frequency == 125.0F);
    CHECK(std::abs(fire_trigger.payload.strength - 0.28F) < 1.0e-6F);
}

TEST_CASE("skyrim.effect_settings.global_body_disabled_preserves_trigger",
    "[component][skyrim][effect_settings]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.body_enabled = false;
    const auto policy = std::make_unique<Policy>(*config);

    Action fire = action(EventKind::spell_fire, Hand::left, at(300), 30);
    fire.source_known = true;
    fire.source_kind = EquipmentKind::spell;
    fire.spell = SpellKind::fire;
    fire.cast_mode = CastMode::fire_and_forget;
    const std::array<Action, 1> actions{fire};

    const EffectFrame trigger_survives = policy->step(observation(at(300)), actions, at(300));
    INFO("global BodyEnabled=false leaves adaptive trigger effects independent");
    CHECK(trigger_survives.left.has_value());
    CHECK(!trigger_survives.body.body_transient.has_value());
    CHECK(!trigger_survives.rumble.has_value());
}

TEST_CASE("skyrim.effect_settings.respect_game_vibration_disabled_allows_hd_and_classic",
    "[component][skyrim][effect_settings]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.respect_game_vibration = false;
    const auto policy = std::make_unique<Policy>(*config);

    Action fire = action(EventKind::spell_fire, Hand::left, at(300), 30);
    fire.source_known = true;
    fire.source_kind = EquipmentKind::spell;
    fire.spell = SpellKind::fire;
    fire.cast_mode = CastMode::fire_and_forget;
    const std::array<Action, 1> actions{fire};

    Observation game_preference_off = observation(at(300));
    game_preference_off.body_vibration_enabled = false;
    const EffectFrame opted_out = policy->step(game_preference_off, actions, at(300));
    INFO("RespectGameVibration=false preserves adaptive trigger output");
    CHECK(opted_out.left.has_value());
    INFO("RespectGameVibration=false explicitly allows HD body output while the game preference is "
         "off");
    CHECK(opted_out.body.hand_transient[0].has_value());
    INFO("RespectGameVibration=false explicitly allows classic fallback while the game preference "
         "is off");
    CHECK(opted_out.rumble.has_value());
}

TEST_CASE("skyrim.effect_settings.global_triggers_disabled_preserves_body_and_classic",
    "[component][skyrim][effect_settings]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.triggers_enabled = false;
    const auto policy = std::make_unique<Policy>(*config);

    Action fire = action(EventKind::spell_fire, Hand::left, at(300), 30);
    fire.source_known = true;
    fire.source_kind = EquipmentKind::spell;
    fire.spell = SpellKind::fire;
    fire.cast_mode = CastMode::fire_and_forget;
    const std::array<Action, 1> actions{fire};

    const EffectFrame body_survives = policy->step(observation(at(300)), actions, at(300));
    INFO("global TriggersEnabled=false suppresses adaptive trigger output");
    CHECK(!body_survives.left.has_value());
    INFO("global TriggersEnabled=false leaves HD body output independent");
    CHECK(body_survives.body.hand_transient[0].has_value());
    INFO("global TriggersEnabled=false leaves classic output independent");
    CHECK(body_survives.rumble.has_value());
}

TEST_CASE("skyrim.effect_settings.body_family_disabled_preserves_trigger",
    "[component][skyrim][effect_settings]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.body[static_cast<std::size_t>(EffectId::fire_release)].enabled = false;
    const auto policy = std::make_unique<Policy>(*config);

    Action fire = action(EventKind::spell_fire, Hand::left, at(300), 30);
    fire.source_known = true;
    fire.source_kind = EquipmentKind::spell;
    fire.spell = SpellKind::fire;
    fire.cast_mode = CastMode::fire_and_forget;
    const std::array<Action, 1> actions{fire};

    const EffectFrame family_trigger = policy->step(observation(at(300)), actions, at(300));
    INFO("disabling a body family removes its HD/classic outputs while preserving its trigger");
    CHECK(family_trigger.left.has_value());
    CHECK(!family_trigger.body.hand_transient[0].has_value());
    CHECK(!family_trigger.rumble.has_value());
}

TEST_CASE("skyrim.effect_settings.trigger_family_disabled_preserves_body_and_classic",
    "[component][skyrim][effect_settings]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.triggers[static_cast<std::size_t>(TriggerId::fire)].enabled = false;
    const auto policy = std::make_unique<Policy>(*config);

    Action fire = action(EventKind::spell_fire, Hand::left, at(300), 30);
    fire.source_known = true;
    fire.source_kind = EquipmentKind::spell;
    fire.spell = SpellKind::fire;
    fire.cast_mode = CastMode::fire_and_forget;
    const std::array<Action, 1> actions{fire};

    const EffectFrame family_body = policy->step(observation(at(300)), actions, at(300));
    INFO("disabling a trigger family suppresses only its adaptive trigger output");
    CHECK(!family_body.left.has_value());
    INFO("disabling a trigger family does not suppress the matching HD body effect");
    CHECK(family_body.body.hand_transient[0].has_value());
    INFO("disabling a trigger family does not suppress the matching classic fallback");
    CHECK(family_body.rumble.has_value());
}

TEST_CASE("skyrim.effect_settings.zero_output_gains_inactive_before_admission",
    "[component][skyrim][effect_settings]") {
    const auto config = std::make_unique<PolicyConfig>();
    EffectAdjustment& adjustment =
        config->effects.body[static_cast<std::size_t>(EffectId::fire_release)];
    adjustment.hd_scale = 0.0F;
    adjustment.classic_scale = 0.0F;
    const auto policy = std::make_unique<Policy>(*config);

    Action fire = action(EventKind::spell_fire, Hand::left, at(300), 30);
    fire.source_known = true;
    fire.source_kind = EquipmentKind::spell;
    fire.spell = SpellKind::fire;
    fire.cast_mode = CastMode::fire_and_forget;
    const std::array<Action, 1> actions{fire};

    const EffectFrame zero_frame = policy->step(observation(at(300)), actions, at(300));
    INFO("a profile with both output gains at zero is inactive before candidate admission");
    CHECK(!zero_frame.body.hand_transient[0].has_value());
    CHECK(!zero_frame.rumble.has_value());
}

TEST_CASE("skyrim.effect_settings.zero_hd_gain_leaves_classic_active_and_renders_exact_silence",
    "[component][skyrim][effect_settings]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.body[static_cast<std::size_t>(EffectId::fire_release)].hd_scale = 0.0F;
    const auto policy = std::make_unique<Policy>(*config);

    Action fire = action(EventKind::spell_fire, Hand::left, at(300), 30);
    fire.source_known = true;
    fire.source_kind = EquipmentKind::spell;
    fire.spell = SpellKind::fire;
    fire.cast_mode = CastMode::fire_and_forget;
    const std::array<Action, 1> actions{fire};

    const EffectFrame classic_only_frame = policy->step(observation(at(300)), actions, at(300));
    INFO("HDScale=0 leaves the independent classic route active");
    CHECK(classic_only_frame.body.hand_transient[0].has_value());
    CHECK(classic_only_frame.rumble.has_value());
    REQUIRE(classic_only_frame.body.hand_transient[0].has_value());

    BodyPlan plan{};
    plan.hand_transient[0] = classic_only_frame.body.hand_transient[0];
    std::vector<float> silent(std::size_t{body_pcm_max_frames} * 2U, 1.0F);
    (void)build_body_pcm(plan, at(300), silent);
    INFO("zero HDScale renders exact silence");
    CHECK(std::ranges::all_of(silent, [](float sample) { return sample == 0.0F; }));
}

TEST_CASE("skyrim.effect_settings.zero_classic_gain_leaves_hd_active",
    "[component][skyrim][effect_settings]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.body[static_cast<std::size_t>(EffectId::fire_release)].classic_scale = 0.0F;
    const auto policy = std::make_unique<Policy>(*config);

    Action fire = action(EventKind::spell_fire, Hand::left, at(300), 30);
    fire.source_known = true;
    fire.source_kind = EquipmentKind::spell;
    fire.spell = SpellKind::fire;
    fire.cast_mode = CastMode::fire_and_forget;
    const std::array<Action, 1> actions{fire};

    const EffectFrame hd_only_frame = policy->step(observation(at(300)), actions, at(300));
    INFO("ClassicScale=0 leaves the independent HD route active");
    CHECK(hd_only_frame.body.hand_transient[0].has_value());
    CHECK(!hd_only_frame.rumble.has_value());
}

TEST_CASE("skyrim.effect_settings.game_vibration_preference_lifecycle_for_held_bow",
    "[component][skyrim][effect_settings]") {
    const auto policy = std::make_unique<Policy>();
    Observation held = observation(at(400));
    held.hands[1].equipped_id = 77;
    held.hands[1].equipment = EquipmentKind::bow;
    held.hands[1].weapon = WeaponClass::bow;
    held.bow = BowPhase::draw;
    const EffectFrame held_before_off = policy->step(held, {}, at(400));
    INFO("fresh held body cues can be admitted before game vibration turns off");
    CHECK(held_before_off.body.held[1].has_value());

    held.sampled_at = at(410);
    held.body_vibration_enabled = false;
    const EffectFrame held_during_off = policy->step(held, {}, at(410));
    INFO("game vibration off clears held body and classic output immediately");
    CHECK(!held_during_off.body.held[1].has_value());
    CHECK(!held_during_off.rumble.has_value());

    held.sampled_at = at(420);
    held.body_vibration_enabled = true;
    const EffectFrame held_after_reenable = policy->step(held, {}, at(420));
    INFO("reenabling game vibration admits a fresh held cue without replaying cleared transients");
    CHECK(held_after_reenable.body.held[1].has_value());
}

} // namespace skyrim_adapter::effect_settings_tests
