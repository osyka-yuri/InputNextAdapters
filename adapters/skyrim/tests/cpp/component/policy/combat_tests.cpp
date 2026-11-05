#include "policy/support/policy_test_support.hpp"
#include "test_support.hpp"
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <input_next_effects.h>
#include <memory>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::tests::policy {

TEST_CASE("skyrim.policy.block_feedback_and_draw_gate", "[component][skyrim][policy]") {
    const auto policy_owner = std::make_unique<Policy>();
    Policy& policy = *policy_owner;
    Observation current = observation(at(350));
    current.blocking = true;
    current.hands[1] = {
        .equipped_id = 500,
        .caster_source_id = 0,
        .equipment = EquipmentKind::weapon,
        .weapon = WeaponClass::sword,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    EffectFrame right_hand_block = policy.step(current, {}, at(350));
    INFO("a right-hand weapon block feeds back through the left trigger");
    REQUIRE(right_hand_block.left.has_value());
    CHECK(std::abs(require_value(right_hand_block.left).payload.strength - 0.22F) < 0.001F);

    current.sampled_at = at(370);
    current.weapon_drawn = false;
    EffectFrame const sheathed = policy.step(current, {}, at(370));
    INFO("held weapon feedback is withdrawn while the weapon is sheathed");
    CHECK(!sheathed.left.has_value());
    CHECK(!sheathed.right.has_value());
    CHECK(!sheathed.rumble.has_value());
}

TEST_CASE("skyrim.policy.shield_block_impact_overlay", "[component][skyrim][policy]") {
    const auto policy_owner = std::make_unique<Policy>();
    Policy& policy = *policy_owner;
    Observation current = observation(at(1000));
    current.blocking = true;
    current.shield_blocking = true;
    current.hands[0] = {
        .equipped_id = 501,
        .caster_source_id = 0,
        .equipment = EquipmentKind::shield,
        .weapon = WeaponClass::none,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };

    Action impact = action(EventKind::hit, Hand::unknown, at(1000), 30);
    impact.incoming = true;
    impact.event.flags = static_cast<std::uint32_t>(HitFlag::blocked);
    const std::array<Action, 1> impact_event{impact};
    EffectFrame hit = policy.step(current, impact_event, at(1000));
    INFO("a confirmed shield-blocked hit adds the authored left-trigger impact overlay");
    REQUIRE(hit.left.has_value());
    const auto& hit_trigger = require_value(hit.left);
    CHECK(hit_trigger.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    CHECK(hit_trigger.payload.start == 0.08F);
    CHECK(hit_trigger.payload.strength == 0.64F);
    CHECK(hit_trigger.ttl_ms == 60);

    INFO("the shield impact keeps its matching brief blocked-hit rumble");
    REQUIRE(hit.rumble.has_value());
    const auto& hit_rumble = require_value(hit.rumble);
    CHECK(hit_rumble.payload.low_frequency == 0.55F);
    CHECK(hit_rumble.payload.high_frequency == 0.18F);
    CHECK(hit_rumble.ttl_ms == 60);

    current.sampled_at = at(1020);
    EffectFrame duplicate = policy.step(current, impact_event, at(1020));
    INFO("a duplicate blocked hit does not restart the impact overlay");
    REQUIRE(duplicate.left.has_value());
    CHECK(require_value(duplicate.left).payload.strength == 0.64F);
    CHECK(require_value(duplicate.left).ttl_ms == 40);

    current.sampled_at = at(1060);
    EffectFrame held = policy.step(current, {}, at(1060));
    INFO("the ongoing shield block returns to its moderate held feedback after impact expires");
    REQUIRE(held.left.has_value());
    CHECK(require_value(held.left).payload.strength == 0.40F);
    CHECK(require_value(held.left).ttl_ms == hold_ttl_ms);
    INFO("the expired shield impact rumble does not extend with the held block");
    CHECK(!held.rumble.has_value());
}

TEST_CASE("skyrim.policy.shield_impact_guards_and_equipment_loss", "[component][skyrim][policy]") {
    const auto blocked_hit = [](MonoTime timestamp, std::uint64_t sequence) {
        Action hit = action(EventKind::hit, Hand::unknown, timestamp, sequence);
        hit.incoming = true;
        hit.event.flags = static_cast<std::uint32_t>(HitFlag::blocked);
        return hit;
    };

    auto policy = std::make_unique<Policy>();
    Observation current = observation(at(1100));
    current.blocking = true;
    current.hands[1] = {
        .equipped_id = 510,
        .caster_source_id = 0,
        .equipment = EquipmentKind::weapon,
        .weapon = WeaponClass::sword,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    Action const weapon_block = blocked_hit(at(1100), 31);
    const std::array<Action, 1> weapon_block_event{weapon_block};
    EffectFrame weapon_result = policy->step(current, weapon_block_event, at(1100));
    INFO("a weapon block keeps its held feedback without receiving a shield-impact overlay");
    REQUIRE(weapon_result.left.has_value());
    CHECK(require_value(weapon_result.left).payload.strength == 0.22F);
    CHECK(require_value(weapon_result.left).ttl_ms == hold_ttl_ms);

    policy = std::make_unique<Policy>();
    current = observation(at(1120));
    current.blocking = true;
    current.shield_blocking = true;
    current.hands[0] = {
        .equipped_id = 511,
        .caster_source_id = 0,
        .equipment = EquipmentKind::shield,
        .weapon = WeaponClass::none,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    Action unblocked_damage = action(EventKind::hit, Hand::unknown, at(1120), 32);
    unblocked_damage.incoming = true;
    const std::array<Action, 1> unblocked_event{unblocked_damage};
    EffectFrame unblocked_result = policy->step(current, unblocked_event, at(1120));
    INFO("unblocked damage during shield use does not create the impact overlay");
    REQUIRE(unblocked_result.left.has_value());
    CHECK(require_value(unblocked_result.left).payload.strength == 0.40F);
    CHECK(require_value(unblocked_result.left).ttl_ms == hold_ttl_ms);

    policy = std::make_unique<Policy>();
    current = observation(at(1140));
    current.blocking = true;
    current.hands[0] = {
        .equipped_id = 512,
        .caster_source_id = 0,
        .equipment = EquipmentKind::shield,
        .weapon = WeaponClass::none,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    Action const no_shield_block = blocked_hit(at(1140), 33);
    const std::array<Action, 1> no_shield_block_event{no_shield_block};
    EffectFrame const no_block_observation = policy->step(current, no_shield_block_event, at(1140));
    INFO("equipped shield without a shield-block observation cannot create an impact overlay");
    CHECK(!no_block_observation.left.has_value());

    policy = std::make_unique<Policy>();
    current = observation(at(1160));
    current.blocking = true;
    current.shield_blocking = true;
    Action const missing_shield = blocked_hit(at(1160), 34);
    const std::array<Action, 1> missing_shield_event{missing_shield};
    EffectFrame const missing_equipment = policy->step(current, missing_shield_event, at(1160));
    INFO("a shield-block observation without equipped shield evidence cannot create a trigger "
         "overlay");
    CHECK(!missing_equipment.left.has_value());

    policy = std::make_unique<Policy>();
    current = observation(at(1180));
    current.blocking = true;
    current.shield_blocking = true;
    current.hands[0] = {
        .equipped_id = 513,
        .caster_source_id = 0,
        .equipment = EquipmentKind::shield,
        .weapon = WeaponClass::none,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    Action const impact = blocked_hit(at(1180), 35);
    const std::array<Action, 1> impact_event{impact};
    INFO("the cancellation case begins with an active shield impact overlay");
    CHECK(policy->step(current, impact_event, at(1180)).left.has_value());

    current.sampled_at = at(1200);
    current.blocking = false;
    current.shield_blocking = false;
    current.hands[0] = {};
    Action const unequip = action(EventKind::equip, Hand::left, at(1200), 36);
    const std::array<Action, 1> unequip_event{unequip};
    EffectFrame const cancelled = policy->step(current, unequip_event, at(1200));
    INFO("shield equipment loss clears an active impact instead of extending it");
    CHECK(!cancelled.left.has_value());
    CHECK(!cancelled.rumble.has_value());

    current.sampled_at = at(1210);
    EffectFrame const after_loss = policy->step(current, {}, at(1210));
    INFO("the cancelled shield impact is not replayed on a later observation");
    CHECK(!after_loss.left.has_value());
    CHECK(!after_loss.rumble.has_value());
}

TEST_CASE("skyrim.policy.actual_crossbow_shots_and_deduplication", "[component][skyrim][policy]") {
    const auto policy_state = std::make_unique<Policy>();
    auto& policy = *policy_state;
    Observation current = observation(at(7000));
    current.hands[1] = {
        .equipped_id = 701,
        .caster_source_id = 0,
        .equipment = EquipmentKind::crossbow,
        .weapon = WeaponClass::crossbow,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    EffectFrame const phase_only = policy.step(current, {}, at(7000));
    INFO("equipped crossbow state alone never synthesizes a shot or reload cue");
    CHECK(!phase_only.body.body_transient.has_value());
    CHECK(!phase_only.rumble.has_value());

    current.sampled_at = at(7100);
    Action shot_event = action(EventKind::player_bow_shot, Hand::unknown, at(7100), 301);
    shot_event.attributed_player_id = 1;
    shot_event.event.source_id = 701;
    shot_event.source_known = true;
    shot_event.source_kind = EquipmentKind::crossbow;
    shot_event.weapon = WeaponClass::crossbow;
    const std::array<Action, 1> shot_events{shot_event};
    EffectFrame shot = policy.step(current, shot_events, at(7100));
    INFO("actual current crossbow event carries its matching equipment identity and finite recipe");
    REQUIRE(shot.body.body_transient.has_value());
    const auto& shot_cue = require_value(shot.body.body_transient);
    CHECK(shot_cue.kind == BodyCueKind::crossbow_shot);
    CHECK(shot_cue.source_id == 701);
    CHECK(shot_cue.duration_ms == 95);
    CHECK(shot_cue.priority == 55);
    CHECK(!shot.left.has_value());
    CHECK(!shot.right.has_value());

    INFO("actual crossbow event retains its bounded classic fallback envelope");
    REQUIRE(shot.rumble.has_value());
    CHECK(require_value(shot.rumble).payload.low_frequency <= 0.60F);
    CHECK(require_value(shot.rumble).payload.high_frequency <= 0.32F);

    current.sampled_at = at(7110);
    Action duplicate_callback = shot_event;
    duplicate_callback.event.sequence = 302;
    duplicate_callback.event.observed_at = at(7110);
    const std::array<Action, 1> duplicate_callbacks{duplicate_callback};
    EffectFrame coalesced = policy.step(current, duplicate_callbacks, at(7110));
    INFO("same actor/source callbacks inside 20 ms coalesce across coordinator sequences");
    REQUIRE(coalesced.body.body_transient.has_value());
    CHECK(require_value(coalesced.body.body_transient).started_at == at(7100));

    current.sampled_at = at(7140);
    Action replay = shot_event;
    replay.event.observed_at = at(7140);
    const std::array<Action, 1> replay_event{replay};
    EffectFrame no_replay = policy.step(current, replay_event, at(7140));
    INFO("a repeated sequence cannot replay a crossbow event beyond the coalescing window");
    REQUIRE(no_replay.body.body_transient.has_value());
    CHECK(require_value(no_replay.body.body_transient).started_at == at(7100));

    const auto ordinary_bow_policy_state = std::make_unique<Policy>();
    auto& ordinary_bow_policy = *ordinary_bow_policy_state;
    Observation ordinary = observation(at(7200));
    ordinary.hands[1] = {
        .equipped_id = 801,
        .caster_source_id = 0,
        .equipment = EquipmentKind::bow,
        .weapon = WeaponClass::bow,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    Action ordinary_bow = action(EventKind::player_bow_shot, Hand::unknown, at(7200), 303);
    ordinary_bow.attributed_player_id = 1;
    ordinary_bow.event.source_id = 801;
    ordinary_bow.source_known = true;
    ordinary_bow.source_kind = EquipmentKind::bow;
    ordinary_bow.weapon = WeaponClass::bow;
    const std::array<Action, 1> ordinary_events{ordinary_bow};
    EffectFrame const ordinary_result =
        ordinary_bow_policy.step(ordinary, ordinary_events, at(7200));
    INFO("ordinary bow callback is quiet and remains owned by existing bow events");
    CHECK(!ordinary_result.body.body_transient.has_value());
    CHECK(!ordinary_result.rumble.has_value());

    const auto wrong_source_policy_state = std::make_unique<Policy>();
    auto& wrong_source_policy = *wrong_source_policy_state;
    Observation wrong_source = observation(at(7300));
    wrong_source.hands[1] = {
        .equipped_id = 701,
        .caster_source_id = 0,
        .equipment = EquipmentKind::crossbow,
        .weapon = WeaponClass::crossbow,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    Action mismatched = shot_event;
    mismatched.event.sequence = 304;
    mismatched.event.source_id = 702;
    mismatched.event.observed_at = at(7300);
    const std::array<Action, 1> mismatched_events{mismatched};
    EffectFrame const mismatched_result =
        wrong_source_policy.step(wrong_source, mismatched_events, at(7300));
    INFO("crossbow callback from a different source than the equipped weapon remains quiet");
    CHECK(!mismatched_result.body.body_transient.has_value());
    CHECK(!mismatched_result.rumble.has_value());
}

} // namespace skyrim_adapter::tests::policy
