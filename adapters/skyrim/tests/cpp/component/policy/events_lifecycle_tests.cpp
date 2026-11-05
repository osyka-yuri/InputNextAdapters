#include "policy/support/policy_test_support.hpp"
#include "test_support.hpp"
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <skyrim_adapter/detail/ingress.hpp>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::tests::policy {

TEST_CASE("skyrim.policy.event_freshness_boundaries", "[component][skyrim][policy]") {
    using namespace std::chrono_literals;
    using skyrim_adapter::detail::event_fresh_at;
    const MonoTime now = at(1000);

    INFO("event observed at exact evaluation time is fresh");
    CHECK(event_fresh_at(now, now));
    INFO("event observed at exact maximum age boundary is fresh");
    CHECK(event_fresh_at(now - event_max_age, now));
    INFO("event observed beyond maximum age boundary is stale");
    CHECK(!event_fresh_at(now - event_max_age - 1ms, now));
    INFO("future event relative to evaluation time is not fresh");
    CHECK(!event_fresh_at(now + 1ms, now));
}

TEST_CASE("skyrim.policy.freshness_priority_and_numeric_safety", "[component][skyrim][policy]") {
    const auto policy_state = std::make_unique<Policy>();
    auto& policy = *policy_state;
    Observation current = observation(at(300));
    Action expired = action(EventKind::bow_release, Hand::right, at(100), 7);
    expired.source_known = true;
    expired.source_kind = EquipmentKind::bow;
    const std::array<Action, 1> expired_actions{expired};
    EffectFrame const old = policy.step(current, expired_actions, at(300));
    INFO("expired burst is dropped without replay");
    CHECK(!old.right.has_value());
    CHECK(!old.rumble.has_value());

    Action future = expired;
    future.event.observed_at = at(301);
    current.sampled_at = at(300);
    const std::array<Action, 1> future_actions{future};
    EffectFrame const future_frame = policy.step(current, future_actions, at(300));
    INFO("future event timestamp is rejected");
    CHECK(!future_frame.right.has_value());
    CHECK(!future_frame.rumble.has_value());

    Action swing = action(EventKind::weapon_swing, Hand::right, at(400), 8);
    swing.source_known = true;
    swing.source_kind = EquipmentKind::weapon;
    swing.weapon = WeaponClass::sword;
    Action impact = action(EventKind::hit, Hand::right, at(400), 9);
    impact.incoming = true;
    const std::array<Action, 2> combat{swing, impact};
    current.sampled_at = at(400);
    EffectFrame priority = policy.step(current, combat, at(400));
    INFO("incoming damage outranks a simultaneous melee swing");
    REQUIRE(priority.rumble.has_value());
    CHECK(std::abs(require_value(priority.rumble).payload.low_frequency - 0.45F) < 0.001F);
    INFO("all emitted SDK fields are finite and within ABI ranges");
    CHECK(finite_legal(priority));

    const auto invalid_config_state = std::make_unique<Policy>(PolicyConfig{
        .trigger_scale = std::numeric_limits<float>::quiet_NaN(),
        .rumble_scale = 1.0F,
    });
    auto& invalid_config = *invalid_config_state;
    current.sampled_at = at(500);
    Action bow_release = action(EventKind::bow_release, Hand::right, at(500), 10);
    bow_release.source_known = true;
    bow_release.source_kind = EquipmentKind::bow;
    const std::array<Action, 1> bow_actions{bow_release};
    EffectFrame const invalid = invalid_config.step(current, bow_actions, at(500));
    INFO("invalid channel scale is neutralized without NaN output");
    CHECK(!invalid.right.has_value());
    INFO("invalid config cannot leak non-finite SDK payload fields");
    CHECK(finite_legal(invalid));

    const auto disabled_state =
        std::make_unique<Policy>(PolicyConfig{.trigger_scale = 0.0F, .rumble_scale = 0.0F});
    auto& disabled = *disabled_state;
    EffectFrame const off = disabled.step(current, bow_actions, at(500));
    INFO("zero scales disable channels instead of emitting zero-value effects");
    CHECK(!off.left.has_value());
    CHECK(!off.right.has_value());
    CHECK(!off.rumble.has_value());
}

TEST_CASE("skyrim.policy.source_less_empty_hand_swing_requires_current_empty_hand",
    "[component][skyrim][policy]") {
    const auto policy_state = std::make_unique<Policy>();
    auto& policy = *policy_state;
    Observation current = observation(at(7900));
    current.weapon_drawn = false;
    current.hands[1] = {
        .equipped_id = 0,
        .caster_source_id = 0,
        .equipment = EquipmentKind::none,
        .weapon = WeaponClass::none,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    Action swing = action(EventKind::weapon_swing, Hand::right, at(7900), 501);
    swing.attributed_player_id = current.player_id;
    swing.event.source_id = 0;
    swing.source_known = true;
    swing.source_kind = EquipmentKind::weapon;
    swing.weapon = WeaponClass::unarmed;
    const std::array<Action, 1> empty_hand_swing{swing};
    EffectFrame admitted = policy.step(current, empty_hand_swing, at(7900));
    INFO("an exact current empty-hand event produces the unarmed hand cue without a drawn weapon");
    REQUIRE(admitted.body.hand_transient[1].has_value());
    const auto& admitted_cue = require_value(admitted.body.hand_transient[1]);
    CHECK(admitted_cue.kind == BodyCueKind::melee_swing);
    CHECK(admitted_cue.weapon == WeaponClass::unarmed);
    CHECK(admitted_cue.source_id == 0);

    const auto typed_state = std::make_unique<Policy>();
    Action typed_swing = swing;
    typed_swing.event.source_id = 0xABC;
    typed_swing.event.sequence = 503;
    typed_swing.attributed_player_id = current.player_id;
    const std::array<Action, 1> typed_event{typed_swing};
    const EffectFrame typed_admitted = typed_state->step(current, typed_event, at(7900));
    INFO("native-attributed typed nonzero hand-to-hand source survives policy consumption");
    REQUIRE(typed_admitted.body.hand_transient[1].has_value());
    const auto& typed_admitted_cue = require_value(typed_admitted.body.hand_transient[1]);
    CHECK(typed_admitted_cue.kind == BodyCueKind::melee_swing);
    CHECK(typed_admitted_cue.weapon == WeaponClass::unarmed);
    CHECK(typed_admitted_cue.source_id == 0xABC);

    const auto changed_state = std::make_unique<Policy>();
    auto& changed_policy = *changed_state;
    current.sampled_at = at(7910);
    current.hands[1] = {
        .equipped_id = 99,
        .caster_source_id = 0,
        .equipment = EquipmentKind::weapon,
        .weapon = WeaponClass::sword,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    swing.event.sequence = 502;
    swing.event.observed_at = at(7910);
    const std::array<Action, 1> stale_empty_hand{swing};
    EffectFrame rejected = changed_policy.step(current, stale_empty_hand, at(7910));
    INFO("a formerly empty hand becoming armed before policy admission keeps the source-less swing "
         "quiet");
    CHECK(!rejected.body.hand_transient[1].has_value());
    CHECK(!rejected.rumble.has_value());
}

TEST_CASE("skyrim.policy.instances_and_reset", "[component][skyrim][policy]") {
    const auto first_owner = std::make_unique<Policy>();
    Policy& first = *first_owner;
    const auto second_owner = std::make_unique<Policy>();
    Policy& second = *second_owner;
    Observation current = observation(at(600));
    Action release = action(EventKind::bow_release, Hand::right, at(600), 11);
    release.source_known = true;
    release.source_kind = EquipmentKind::bow;
    const std::array<Action, 1> actions{release};
    EffectFrame const first_frame = first.step(current, actions, at(600));
    EffectFrame const second_idle = second.step(current, {}, at(600));
    INFO("separate policy instances do not share transient state");
    CHECK(first_frame.rumble.has_value());
    CHECK(!second_idle.rumble.has_value());

    first.reset();
    current.sampled_at = at(605);
    EffectFrame const after_reset = first.step(current, {}, at(605));
    INFO("reset removes active overlays and rumble immediately");
    CHECK(!after_reset.rumble.has_value());
    CHECK(!after_reset.right.has_value());

    first.reset();
    INFO("an independent instance remains usable after another resets");
    CHECK(finite_legal(second.step(current, {}, at(605))));
}

} // namespace skyrim_adapter::tests::policy
