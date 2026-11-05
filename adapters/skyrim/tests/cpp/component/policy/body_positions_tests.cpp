#include "policy/support/policy_test_support.hpp"
#include "test_support.hpp"
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::tests::policy {

namespace {
inline std::uint64_t stable_id_of(const std::optional<BodyCue>& cue) noexcept {
    return cue ? cue->stable_id : 0;
}
} // namespace

TEST_CASE("skyrim.policy.body_spell_release_hand_history", "[component][skyrim][policy]") {
    const auto simultaneous = std::make_unique<Policy>();
    Observation current = observation(at(5000));
    Action left_fire = action(EventKind::spell_fire, Hand::left, at(5000), 101);
    left_fire.source_known = true;
    left_fire.source_kind = EquipmentKind::spell;
    left_fire.spell = SpellKind::fire;
    left_fire.cast_mode = CastMode::fire_and_forget;
    Action right_frost = left_fire;
    right_frost.event.hand = Hand::right;
    right_frost.event.sequence = 102;
    right_frost.event.source_id = 43;
    right_frost.spell = SpellKind::frost;
    const std::array<Action, 2> both_releases{left_fire, right_frost};
    EffectFrame both = simultaneous->step(current, both_releases, at(5000));
    INFO("simultaneous fire-and-forget releases retain independent hand positions");
    REQUIRE(both.body.hand_transient[0].has_value());
    REQUIRE(both.body.hand_transient[1].has_value());

    current.sampled_at = at(5020);
    Action replacement = left_fire;
    replacement.event.sequence = 103;
    replacement.event.source_id = 44;
    replacement.event.observed_at = at(5020);
    replacement.spell = SpellKind::shock;
    const std::array<Action, 1> replacement_event{replacement};
    EffectFrame replaced = simultaneous->step(current, replacement_event, at(5020));
    INFO("replacing one hand preserves the opposite release and its original start (left)");
    REQUIRE(replaced.body.hand_transient[0].has_value());
    INFO("replacing one hand preserves the opposite release and its original start (right)");
    REQUIRE(replaced.body.hand_transient[1].has_value());
    CHECK(require_value(replaced.body.hand_transient[0]).started_at == at(5020));
    CHECK(require_value(replaced.body.hand_transient[1]).started_at == at(5000));

    current.sampled_at = at(5080);
    EffectFrame staggered = simultaneous->step(current, {}, at(5080));
    INFO("staggered hand transients expire independently and retain the newer left cue");
    REQUIRE(staggered.body.hand_transient[0].has_value());
    CHECK(require_value(staggered.body.hand_transient[0]).source_id == 44);
    CHECK(!staggered.body.hand_transient[1].has_value());
}

TEST_CASE("skyrim.policy.body_unknown_spell_hand_routes_to_classic",
    "[component][skyrim][policy]") {
    const auto unknown_hand_policy = std::make_unique<Policy>();
    Action left_fire = action(EventKind::spell_fire, Hand::unknown, at(5000), 101);
    left_fire.source_known = true;
    left_fire.source_kind = EquipmentKind::spell;
    left_fire.spell = SpellKind::fire;
    left_fire.cast_mode = CastMode::fire_and_forget;
    const std::array<Action, 1> unknown_hand_action{left_fire};
    EffectFrame unknown_hand_frame =
        unknown_hand_policy->step(observation(at(5000)), unknown_hand_action, at(5000));
    INFO("unknown release hand keeps authored fallback rumble without inventing a PCM hand");
    REQUIRE(unknown_hand_frame.rumble.has_value());
    CHECK(!unknown_hand_frame.body.hand_transient[0].has_value());
    CHECK(!unknown_hand_frame.body.hand_transient[1].has_value());
    CHECK(!unknown_hand_frame.body.body_transient.has_value());
}

TEST_CASE("skyrim.policy.body_melee_independent_hand_history", "[component][skyrim][policy]") {
    const auto melee_sides = std::make_unique<Policy>();
    Action left_swing = action(EventKind::weapon_swing, Hand::left, at(5100), 110);
    left_swing.source_known = true;
    left_swing.source_kind = EquipmentKind::weapon;
    left_swing.weapon = WeaponClass::sword;
    Action right_swing = left_swing;
    right_swing.event.hand = Hand::right;
    right_swing.event.sequence = 111;
    right_swing.weapon = WeaponClass::axe;
    const std::array<Action, 2> dual_swings{left_swing, right_swing};
    Observation melee_observation = observation(at(5100));
    EffectFrame directed_swings = melee_sides->step(melee_observation, dual_swings, at(5100));
    INFO("simultaneous known one-handed swings use their matching independent hand positions "
         "(left)");
    REQUIRE(directed_swings.body.hand_transient[0].has_value());
    INFO("simultaneous known one-handed swings use their matching independent hand positions "
         "(right)");
    REQUIRE(directed_swings.body.hand_transient[1].has_value());
    CHECK(require_value(directed_swings.body.hand_transient[0]).kind == BodyCueKind::melee_swing);
    CHECK(require_value(directed_swings.body.hand_transient[0]).hand == Hand::left);
    CHECK(require_value(directed_swings.body.hand_transient[1]).hand == Hand::right);
    CHECK(!directed_swings.body.body_transient.has_value());

    melee_observation.sampled_at = at(5120);
    Action newer_left_swing = left_swing;
    newer_left_swing.event.sequence = 112;
    newer_left_swing.event.source_id = 46;
    newer_left_swing.event.observed_at = at(5120);
    const std::array<Action, 1> newer_left_swing_event{newer_left_swing};
    EffectFrame staggered_swings =
        melee_sides->step(melee_observation, newer_left_swing_event, at(5120));
    INFO("replacing one simultaneous swing preserves the opposite hand and its original start "
         "(left)");
    REQUIRE(staggered_swings.body.hand_transient[0].has_value());
    INFO("replacing one simultaneous swing preserves the opposite hand and its original start "
         "(right)");
    REQUIRE(staggered_swings.body.hand_transient[1].has_value());
    CHECK(require_value(staggered_swings.body.hand_transient[0]).started_at == at(5120));
    CHECK(require_value(staggered_swings.body.hand_transient[1]).started_at == at(5100));

    melee_observation.sampled_at = at(5165);
    EffectFrame independently_expired_swings = melee_sides->step(melee_observation, {}, at(5165));
    INFO("staggered melee swings expire independently by hand");
    REQUIRE(independently_expired_swings.body.hand_transient[0].has_value());
    CHECK(!independently_expired_swings.body.hand_transient[1].has_value());
    CHECK(
        require_value(independently_expired_swings.body.hand_transient[0]).started_at == at(5120));

    (*melee_sides).reset();
    melee_observation.sampled_at = at(5170);
    EffectFrame reset_swings = melee_sides->step(melee_observation, {}, at(5170));
    INFO("policy reset clears both hand-directed swing histories");
    CHECK(!reset_swings.body.hand_transient[0].has_value());
    CHECK(!reset_swings.body.hand_transient[1].has_value());
    CHECK(!reset_swings.body.body_transient.has_value());
}

TEST_CASE("skyrim.policy.body_melee_two_handed_bilateral", "[component][skyrim][policy]") {
    const auto two_handed_swing_policy = std::make_unique<Policy>();
    Action two_handed_swing = action(EventKind::weapon_swing, Hand::left, at(5100), 113);
    two_handed_swing.source_known = true;
    two_handed_swing.source_kind = EquipmentKind::weapon;
    two_handed_swing.weapon = WeaponClass::two_handed;
    const std::array<Action, 1> two_handed_swing_event{two_handed_swing};
    EffectFrame two_handed_swing_frame =
        two_handed_swing_policy->step(observation(at(5100)), two_handed_swing_event, at(5100));
    INFO("two-handed swings retain bilateral body placement even with a known event hand");
    REQUIRE(two_handed_swing_frame.body.body_transient.has_value());
    CHECK(
        require_value(two_handed_swing_frame.body.body_transient).kind == BodyCueKind::melee_swing);
    CHECK(require_value(two_handed_swing_frame.body.body_transient).weapon ==
          WeaponClass::two_handed);
    CHECK(!two_handed_swing_frame.body.hand_transient[0].has_value());
    CHECK(!two_handed_swing_frame.body.hand_transient[1].has_value());
}

TEST_CASE("skyrim.policy.body_melee_unknown_hand_bilateral", "[component][skyrim][policy]") {
    const auto unknown_swing_policy = std::make_unique<Policy>();
    Action unknown_swing = action(EventKind::weapon_swing, Hand::unknown, at(5100), 114);
    unknown_swing.source_known = true;
    unknown_swing.source_kind = EquipmentKind::weapon;
    unknown_swing.weapon = WeaponClass::sword;
    const std::array<Action, 1> unknown_swing_event{unknown_swing};
    EffectFrame unknown_swing_frame =
        unknown_swing_policy->step(observation(at(5100)), unknown_swing_event, at(5100));
    INFO("unknown-hand one-handed swings retain bilateral body placement without guessing a side");
    REQUIRE(unknown_swing_frame.body.body_transient.has_value());
    CHECK(require_value(unknown_swing_frame.body.body_transient).kind == BodyCueKind::melee_swing);
    CHECK(!unknown_swing_frame.body.hand_transient[0].has_value());
    CHECK(!unknown_swing_frame.body.hand_transient[1].has_value());
}

TEST_CASE("skyrim.policy.body_five_positions_overlay_and_interruptions",
    "[component][skyrim][policy]") {
    const auto five_positions = std::make_unique<Policy>();
    Observation current = observation(at(6000));
    current.hands[0] = {
        .equipped_id = 201,
        .caster_source_id = 301,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = CastMode::concentration,
        .cast_phase = CastPhase::casting,
        .two_handed = false,
    };
    current.hands[1] = {
        .equipped_id = 202,
        .caster_source_id = 302,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::frost,
        .cast_mode = CastMode::concentration,
        .cast_phase = CastPhase::casting,
        .two_handed = false,
    };
    Action left_release = action(EventKind::spell_fire, Hand::left, at(6000), 201);
    left_release.source_known = true;
    left_release.source_kind = EquipmentKind::spell;
    left_release.spell = SpellKind::fire;
    left_release.cast_mode = CastMode::fire_and_forget;
    Action right_release = left_release;
    right_release.event.hand = Hand::right;
    right_release.event.sequence = 202;
    right_release.event.source_id = 43;
    right_release.spell = SpellKind::frost;
    Action const voice = action(EventKind::voice_fire, Hand::voice, at(6000), 203);
    const std::array<Action, 3> all_positions{left_release, right_release, voice};
    EffectFrame overlap = five_positions->step(current, all_positions, at(6000));
    INFO("two concentration hands, two finite releases and a voice cue fit the bounded five "
         "positions");
    REQUIRE(overlap.body.held[0].has_value());
    REQUIRE(overlap.body.held[1].has_value());
    REQUIRE(overlap.body.hand_transient[0].has_value());
    REQUIRE(overlap.body.hand_transient[1].has_value());
    REQUIRE(overlap.body.body_transient.has_value());
    const std::uint64_t left_hold_id = stable_id_of(overlap.body.held[0]);

    current.sampled_at = at(6050);
    Action incoming = action(EventKind::hit, Hand::unknown, at(6050), 204);
    incoming.incoming = true;
    const std::array<Action, 1> body_impact{incoming};
    EffectFrame impact = five_positions->step(current, body_impact, at(6050));
    INFO(
        "a higher-priority body impact replaces the voice cue without erasing either hand history");
    REQUIRE(impact.body.held[0].has_value());
    REQUIRE(impact.body.held[1].has_value());
    REQUIRE(impact.body.hand_transient[0].has_value());
    REQUIRE(impact.body.hand_transient[1].has_value());
    REQUIRE(impact.body.body_transient.has_value());
    CHECK(require_value(impact.body.body_transient).kind == BodyCueKind::incoming_hit);
    CHECK(require_value(impact.body.held[0]).stable_id == left_hold_id);
    CHECK(require_value(impact.body.held[0]).started_at == at(6000));

    current.sampled_at = at(6060);
    Action const equip = action(EventKind::equip, Hand::right, at(6060), 205);
    const std::array<Action, 1> equip_event{equip};
    EffectFrame interrupted = five_positions->step(current, equip_event, at(6060));
    INFO("equip interruption clears all semantic cue histories");
    CHECK(!interrupted.body.held[0].has_value());
    CHECK(!interrupted.body.held[1].has_value());
    CHECK(!interrupted.body.hand_transient[0].has_value());
    CHECK(!interrupted.body.hand_transient[1].has_value());
    CHECK(!interrupted.body.body_transient.has_value());

    current.sampled_at = at(6070);
    EffectFrame resumed = five_positions->step(current, {}, at(6070));
    INFO("fresh observed concentration can start again after the interruption");
    CHECK(resumed.body.held[0].has_value());
    CHECK(resumed.body.held[1].has_value());
    CHECK(!resumed.body.hand_transient[0].has_value());
    CHECK(!resumed.body.hand_transient[1].has_value());
    CHECK(!resumed.body.body_transient.has_value());

    current.available = false;
    EffectFrame unavailable = five_positions->step(current, {}, at(6080));
    INFO("unavailable observations clear every semantic cue history");
    CHECK(!unavailable.body.held[0].has_value());
    CHECK(!unavailable.body.held[1].has_value());
    CHECK(!unavailable.body.hand_transient[0].has_value());
    CHECK(!unavailable.body.hand_transient[1].has_value());
    CHECK(!unavailable.body.body_transient.has_value());
}

} // namespace skyrim_adapter::tests::policy
