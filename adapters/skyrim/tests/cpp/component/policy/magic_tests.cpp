#include "policy/support/policy_test_support.hpp"
#include "test_support.hpp"
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <input_next_effects.h>
#include <memory>
#include <optional>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::tests::policy {
namespace {

struct HandPhases {
    CastPhase left{};
    CastPhase right{};
};

struct SpellOrigin {
    FormId source{};
    SpellKind spell{};
};

Observation dual_sample(MonoTime sampled_at, CastMode mode, HandPhases phases) {
    Observation current = observation(sampled_at);
    current.dual_cast = true;
    current.hands[0] = {
        .equipped_id = 42,
        .caster_source_id = 42,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = mode,
        .cast_phase = phases.left,
        .two_handed = false,
    };
    current.hands[1] = {
        .equipped_id = 42,
        .caster_source_id = 42,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = mode,
        .cast_phase = phases.right,
        .two_handed = false,
    };
    return current;
}

Action fire_action(Hand hand, MonoTime observed_at, std::uint64_t sequence, SpellOrigin origin) {
    Action result = action(EventKind::spell_fire, hand, observed_at, sequence);
    result.event.source_id = origin.source;
    result.source_known = true;
    result.source_kind = EquipmentKind::spell;
    result.spell = origin.spell;
    result.cast_mode = CastMode::fire_and_forget;
    return result;
}

MonoTime started_at_of(const std::optional<BodyCue>& cue) noexcept {
    return cue ? cue->started_at : MonoTime{};
}

} // namespace

TEST_CASE("skyrim.policy.magic_concentration_equip_fence_recovery", "[component][skyrim][policy]") {
    Observation current = observation(at(13'000));
    current.hands[0] = {
        .equipped_id = 806,
        .caster_source_id = 807,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = CastMode::concentration,
        .cast_phase = CastPhase::casting,
        .two_handed = false,
    };
    const auto concentration_policy = std::make_unique<Policy>();
    (void)concentration_policy->step(current, {}, at(13'000));
    Action const equip_concentration = action(EventKind::equip, Hand::left, at(13'000), 804);
    const std::array<Action, 1> equip_concentration_event{equip_concentration};
    EffectFrame interrupted_concentration =
        concentration_policy->step(current, equip_concentration_event, at(13'000));
    INFO("interruption fences the concentration body, trigger, and rumble routes together");
    CHECK(!interrupted_concentration.body.held[0].has_value());
    CHECK(!interrupted_concentration.left.has_value());
    CHECK(!interrupted_concentration.rumble.has_value());

    EffectFrame stale_concentration = concentration_policy->step(current, {}, at(13'020));
    INFO("repeated same-sample ticks cannot restore any concentration feedback route");
    CHECK(!stale_concentration.body.held[0].has_value());
    CHECK(!stale_concentration.left.has_value());
    CHECK(!stale_concentration.rumble.has_value());

    current.sampled_at = at(13'021);
    EffectFrame fresh_concentration = concentration_policy->step(current, {}, at(13'021));
    INFO("newer coherent sample restores body, trigger, and rumble concentration feedback");
    CHECK(fresh_concentration.body.held[0].has_value());
    CHECK(fresh_concentration.left.has_value());
    CHECK(fresh_concentration.rumble.has_value());

    EffectFrame duplicate_equip_concentration =
        concentration_policy->step(current, equip_concentration_event, at(13'022));
    INFO("replayed equip action after recovery remains idempotent across all concentration routes");
    CHECK(duplicate_equip_concentration.body.held[0].has_value());
    CHECK(duplicate_equip_concentration.left.has_value());
    CHECK(duplicate_equip_concentration.rumble.has_value());
}

TEST_CASE("skyrim.policy.magic_and_dual_hands", "[component][skyrim][policy]") {
    const auto policy = std::make_unique<Policy>();
    Observation current = observation(at(200));
    current.hands[0] = {
        .equipped_id = 200,
        .caster_source_id = 300,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = CastMode::fire_and_forget,
        .cast_phase = CastPhase::charging,
        .two_handed = false,
    };
    EffectFrame charging = policy->step(current, {}, at(200));
    INFO("fire-and-forget charge uses phase-only moderate feedback");
    REQUIRE(charging.left.has_value());
    CHECK(require_value(charging.left).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    CHECK(std::abs(require_value(charging.left).payload.strength - 0.34F) < 0.001F);

    current.sampled_at = at(220);
    current.hands[0].cast_mode = CastMode::concentration;
    current.hands[0].cast_phase = CastPhase::casting;
    current.hands[0].spell = SpellKind::shock;
    EffectFrame streaming = policy->step(current, {}, at(220));
    INFO("known concentration uses its sustained spell recipe");
    REQUIRE(streaming.left.has_value());
    CHECK(require_value(streaming.left).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);

    Action cancelled_cast = action(EventKind::spell_cast, Hand::left, at(230), 3);
    cancelled_cast.source_known = true;
    cancelled_cast.source_kind = EquipmentKind::spell;
    cancelled_cast.spell = SpellKind::fire;
    cancelled_cast.cast_mode = CastMode::fire_and_forget;
    current.hands[0] = {};
    current.sampled_at = at(230);
    const std::array<Action, 1> cancelled_actions{cancelled_cast};
    EffectFrame const cancelled = policy->step(current, cancelled_actions, at(230));
    INFO("spell-cast alone does not produce a release pulse");
    CHECK(!cancelled.rumble.has_value());

    Action unknown_spell = action(EventKind::spell_fire, Hand::right, at(240), 4);
    unknown_spell.source_kind = EquipmentKind::spell;
    unknown_spell.spell = SpellKind::unknown;
    unknown_spell.cast_mode = CastMode::fire_and_forget;
    current.hands[0] = {};
    current.sampled_at = at(240);
    const std::array<Action, 1> unknown_actions{unknown_spell};
    EffectFrame const unknown = policy->step(current, unknown_actions, at(240));
    INFO("unknown caster metadata never receives an invented elemental effect");
    CHECK(!unknown.right.has_value());
    CHECK(!unknown.rumble.has_value());

    Action left_fire = action(EventKind::spell_fire, Hand::left, at(260), 5);
    left_fire.source_known = true;
    left_fire.source_kind = EquipmentKind::spell;
    left_fire.spell = SpellKind::fire;
    left_fire.cast_mode = CastMode::fire_and_forget;
    Action right_frost = left_fire;
    right_frost.event.hand = Hand::right;
    right_frost.event.source_id = 43;
    right_frost.event.sequence = 6;
    right_frost.spell = SpellKind::frost;
    current.sampled_at = at(260);
    const std::array<Action, 2> dual_actions{left_fire, right_frost};
    EffectFrame dual = policy->step(current, dual_actions, at(260));
    INFO("left-hand spell release occupies the left trigger independently");
    REQUIRE(dual.left.has_value());
    CHECK(require_value(dual.left).payload.frequency == 38.0F);
    INFO("right-hand spell release occupies the right trigger independently");
    REQUIRE(dual.right.has_value());
    CHECK(require_value(dual.right).payload.frequency == 18.0F);

    const auto concentrating_policy = std::make_unique<Policy>();
    Observation concentrating = observation(at(300));
    concentrating.hands[0] = {
        .equipped_id = 201,
        .caster_source_id = 301,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::frost,
        .cast_mode = CastMode::concentration,
        .cast_phase = CastPhase::casting,
        .two_handed = false,
    };
    concentrating.hands[1] = {
        .equipped_id = 202,
        .caster_source_id = 302,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::shock,
        .cast_mode = CastMode::concentration,
        .cast_phase = CastPhase::casting,
        .two_handed = false,
    };
    EffectFrame both_streams = concentrating_policy->step(concentrating, {}, at(300));
    INFO("simultaneous sustained spells compose by per-component maximum across hands");
    REQUIRE(both_streams.rumble.has_value());
    CHECK(require_value(both_streams.rumble).payload.low_frequency == 0.10F);
    CHECK(require_value(both_streams.rumble).payload.high_frequency == 0.15F);

    concentrating.sampled_at = at(320);
    concentrating.hands[0] = {};
    concentrating.hands[1] = {};
    EffectFrame const released_streams = concentrating_policy->step(concentrating, {}, at(320));
    INFO("held magic feedback withdraws on the first observation without an active caster");
    CHECK(!released_streams.left.has_value());
    CHECK(!released_streams.right.has_value());
    CHECK(!released_streams.rumble.has_value());
}

TEST_CASE("skyrim.policy.magic_dual_held_concentration_history", "[component][skyrim][policy]") {
    const auto held_policy = std::make_unique<Policy>();
    Observation held_observation = dual_sample(at(8000),
        CastMode::concentration,
        HandPhases{.left = CastPhase::casting, .right = CastPhase::casting});
    EffectFrame dual_held = held_policy->step(held_observation, {}, at(8000));
    REQUIRE(dual_held.body.held[0].has_value());
    REQUIRE(dual_held.body.held[1].has_value());
    const auto& dual_left = require_value(dual_held.body.held[0]);
    const auto& dual_right = require_value(dual_held.body.held[1]);
    INFO("confirmed concentration enters both held positions with one start and shared carrier "
         "seed");
    CHECK(dual_left.dual_cast);
    CHECK(dual_right.dual_cast);
    CHECK(dual_left.started_at == at(8000));
    CHECK(dual_right.started_at == at(8000));
    CHECK(dual_left.stable_id == dual_right.stable_id);
    const auto dual_seed = dual_left.stable_id;

    held_observation.sampled_at = at(8100);
    EffectFrame renewed = held_policy->step(held_observation, {}, at(8100));
    REQUIRE(renewed.body.held[0].has_value());
    REQUIRE(renewed.body.held[1].has_value());
    const auto& renewed_left = require_value(renewed.body.held[0]);
    const auto& renewed_right = require_value(renewed.body.held[1]);
    INFO("dual concentration renewal keeps one original onset, shared seed, and finite horizon");
    CHECK(renewed_left.started_at == at(8000));
    CHECK(renewed_right.started_at == at(8000));
    CHECK(renewed_left.stable_id == dual_seed);
    CHECK(renewed_right.stable_id == dual_seed);
    CHECK(renewed_left.duration_ms == 300);
    CHECK(renewed_right.duration_ms == 300);

    held_observation.sampled_at = at(8120);
    held_observation.dual_cast = false;
    EffectFrame exited = held_policy->step(held_observation, {}, at(8120));
    REQUIRE(exited.body.held[0].has_value());
    REQUIRE(exited.body.held[1].has_value());
    const auto& exited_left = require_value(exited.body.held[0]);
    const auto& exited_right = require_value(exited.body.held[1]);
    INFO("leaving native dual state starts ordinary independent held identities");
    CHECK(!exited_left.dual_cast);
    CHECK(!exited_right.dual_cast);
    CHECK(exited_left.started_at == at(8120));
    CHECK(exited_right.started_at == at(8120));
    CHECK(exited_left.stable_id != exited_right.stable_id);

    held_observation.sampled_at = at(8140);
    held_observation.hands[1].equipped_id = 43;
    held_observation.hands[1].caster_source_id = 43;
    held_observation.hands[1].spell = SpellKind::frost;
    EffectFrame independent = held_policy->step(held_observation, {}, at(8140));
    REQUIRE(independent.body.held[0].has_value());
    REQUIRE(independent.body.held[1].has_value());
    const auto& ind_left = require_value(independent.body.held[0]);
    const auto& ind_right = require_value(independent.body.held[1]);
    INFO("different independent spells retain their separate ordinary seeds");
    CHECK(!ind_left.dual_cast);
    CHECK(!ind_right.dual_cast);
    CHECK(ind_left.spell == SpellKind::fire);
    CHECK(ind_right.spell == SpellKind::frost);
    CHECK(ind_left.stable_id != ind_right.stable_id);
    const auto independent_left_seed = ind_left.stable_id;
    const auto independent_right_seed = ind_right.stable_id;

    held_observation.sampled_at = at(8160);
    EffectFrame independent_renewal = held_policy->step(held_observation, {}, at(8160));
    REQUIRE(independent_renewal.body.held[0].has_value());
    REQUIRE(independent_renewal.body.held[1].has_value());
    const auto& ind_ren_left = require_value(independent_renewal.body.held[0]);
    const auto& ind_ren_right = require_value(independent_renewal.body.held[1]);
    INFO("ordinary independent concentration renewals keep their original starts and seeds");
    CHECK(ind_ren_left.stable_id == independent_left_seed);
    CHECK(ind_ren_right.stable_id == independent_right_seed);
    CHECK(ind_ren_left.started_at == at(8120));
    CHECK(ind_ren_right.started_at == at(8140));

    held_observation.sampled_at = at(8180);
    held_observation.dual_cast = true;
    held_observation.hands[1].equipped_id = 42;
    held_observation.hands[1].caster_source_id = 42;
    held_observation.hands[1].spell = SpellKind::fire;
    EffectFrame reentered = held_policy->step(held_observation, {}, at(8180));
    REQUIRE(reentered.body.held[0].has_value());
    REQUIRE(reentered.body.held[1].has_value());
    const auto& reentered_left = require_value(reentered.body.held[0]);
    const auto& reentered_right = require_value(reentered.body.held[1]);
    INFO("re-entering confirmed dual casting creates a new shared held identity");
    CHECK(reentered_left.dual_cast);
    CHECK(reentered_right.dual_cast);
    CHECK(reentered_left.started_at == at(8180));
    CHECK(reentered_right.started_at == at(8180));
    CHECK(reentered_left.stable_id == reentered_right.stable_id);
    CHECK(reentered_left.stable_id != independent_left_seed);
}

TEST_CASE("skyrim.policy.magic_dual_release_pairing", "[component][skyrim][policy]") {
    const auto release_policy = std::make_unique<Policy>();
    Observation release_observation = dual_sample(at(9000),
        CastMode::fire_and_forget,
        HandPhases{.left = CastPhase::charging, .right = CastPhase::casting});
    Action const left_fire =
        fire_action(Hand::left, at(9000), 900, SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> left_event{left_fire};
    EffectFrame first_release = release_policy->step(release_observation, left_event, at(9000));
    INFO("a confirmed actual hand fire creates one bilateral release with the original recipe "
         "metadata");
    REQUIRE(first_release.body.body_transient.has_value());
    const auto& released_cue = require_value(first_release.body.body_transient);
    CHECK(released_cue.kind == BodyCueKind::spell_release);
    CHECK(released_cue.dual_cast);
    CHECK(released_cue.hand == Hand::unknown);
    CHECK(released_cue.source_id == 42);
    CHECK(released_cue.duration_ms == 55);
    CHECK(released_cue.priority == 45);
    CHECK(!first_release.body.hand_transient[0].has_value());
    CHECK(!first_release.body.hand_transient[1].has_value());

    INFO("dual body attribution leaves the accepted elemental trigger recipe unchanged");
    REQUIRE(first_release.left.has_value());
    const auto& left_trigger = require_value(first_release.left);
    CHECK(left_trigger.ttl_ms == 55);
    CHECK(left_trigger.payload.strength == 0.28F);
    CHECK(left_trigger.payload.frequency == 38.0F);

    release_observation.sampled_at = at(9010);
    Action const right_fire = fire_action(Hand::right,
        at(9010),
        901,
        SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> right_event{right_fire};
    EffectFrame paired_release = release_policy->step(release_observation, right_event, at(9010));
    INFO("opposite hand fire in a later burst merges into the original bilateral release");
    REQUIRE(paired_release.body.body_transient.has_value());
    CHECK(require_value(paired_release.body.body_transient).dual_cast);
    CHECK(require_value(paired_release.body.body_transient).started_at == at(9000));
    CHECK(!paired_release.body.hand_transient[0].has_value());
    CHECK(!paired_release.body.hand_transient[1].has_value());

    INFO("paired fire preserves the first release rumble onset instead of restarting the classic "
         "pulse");
    REQUIRE(paired_release.rumble.has_value());
    CHECK(require_value(paired_release.rumble).ttl_ms == 15);
}

TEST_CASE("skyrim.policy.magic_dual_same_burst_expiry", "[component][skyrim][policy]") {
    Action const left_fire =
        fire_action(Hand::left, at(9000), 900, SpellOrigin{.source = 42, .spell = SpellKind::fire});
    Action const right_fire = fire_action(Hand::right,
        at(9010),
        901,
        SpellOrigin{.source = 42, .spell = SpellKind::fire});

    const auto same_burst_policy = std::make_unique<Policy>();
    Observation same_burst_observation = dual_sample(at(9010),
        CastMode::fire_and_forget,
        HandPhases{.left = CastPhase::charging, .right = CastPhase::casting});
    const std::array<Action, 2> same_burst_events{left_fire, right_fire};
    EffectFrame same_burst =
        same_burst_policy->step(same_burst_observation, same_burst_events, at(9010));
    INFO("same-burst paired fire keeps one original body/fallback onset and both hand triggers");
    REQUIRE(same_burst.body.body_transient.has_value());
    CHECK(require_value(same_burst.body.body_transient).started_at == at(9000));
    REQUIRE(same_burst.rumble.has_value());
    CHECK(require_value(same_burst.rumble).ttl_ms == 15);
    CHECK(same_burst.left.has_value());
    CHECK(same_burst.right.has_value());

    same_burst_observation.sampled_at = at(9054);
    const EffectFrame before_paired_expiry =
        same_burst_policy->step(same_burst_observation, {}, at(9054));
    INFO("one original dual-fire fallback remains active immediately before its finite expiry");
    REQUIRE(before_paired_expiry.rumble.has_value());
    CHECK(require_value(before_paired_expiry.rumble).ttl_ms == 1);

    same_burst_observation.sampled_at = at(9055);
    const EffectFrame after_paired_expiry =
        same_burst_policy->step(same_burst_observation, {}, at(9055));
    INFO("paired dual-fire fallback expires at the original 55ms boundary without replay");
    CHECK(!after_paired_expiry.rumble.has_value());
}

TEST_CASE("skyrim.policy.magic_dual_fresh_cast_after_pair", "[component][skyrim][policy]") {
    const auto release_policy = std::make_unique<Policy>();
    Observation release_observation = dual_sample(at(9000),
        CastMode::fire_and_forget,
        HandPhases{.left = CastPhase::charging, .right = CastPhase::casting});
    Action const left_fire =
        fire_action(Hand::left, at(9000), 900, SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> left_event{left_fire};
    (void)release_policy->step(release_observation, left_event, at(9000));

    release_observation.sampled_at = at(9010);
    Action const right_fire = fire_action(Hand::right,
        at(9010),
        901,
        SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> right_event{right_fire};
    (void)release_policy->step(release_observation, right_event, at(9010));

    release_observation.sampled_at = at(9015);
    Action const later_fire =
        fire_action(Hand::left, at(9015), 902, SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> later_event{later_fire};
    EffectFrame later_release = release_policy->step(release_observation, later_event, at(9015));
    INFO("a later same-source cast remains eligible after the two-hand pair is consumed");
    REQUIRE(later_release.body.body_transient.has_value());
    CHECK(require_value(later_release.body.body_transient).dual_cast);
    CHECK(require_value(later_release.body.body_transient).started_at == at(9015));
}

TEST_CASE("skyrim.policy.magic_dual_audio_reset_barrier", "[component][skyrim][policy]") {
    const auto release_policy = std::make_unique<Policy>();
    Observation release_observation = dual_sample(at(9000),
        CastMode::fire_and_forget,
        HandPhases{.left = CastPhase::charging, .right = CastPhase::casting});
    release_policy->reset_body_history(at(9020));
    release_observation.sampled_at = at(9020);
    const EffectFrame audio_baseline = release_policy->step(release_observation, {}, at(9020));
    INFO("audio reset sample at its barrier remains a silent body baseline");
    CHECK(!audio_baseline.body.held[0].has_value());
    CHECK(!audio_baseline.body.body_transient.has_value());

    release_observation.sampled_at = at(9021);
    Action const after_audio_reset = fire_action(Hand::right,
        at(9021),
        903,
        SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> after_audio_event{after_audio_reset};
    EffectFrame audio_release =
        release_policy->step(release_observation, after_audio_event, at(9021));
    INFO("audio rebind clears the bounded dual release stamp before fresh events resume");
    REQUIRE(audio_release.body.body_transient.has_value());
    CHECK(require_value(audio_release.body.body_transient).dual_cast);
    CHECK(require_value(audio_release.body.body_transient).started_at == at(9021));
}

TEST_CASE("skyrim.policy.magic_dual_policy_reset_barrier", "[component][skyrim][policy]") {
    const auto release_policy = std::make_unique<Policy>();
    Observation release_observation = dual_sample(at(9030),
        CastMode::fire_and_forget,
        HandPhases{.left = CastPhase::charging, .right = CastPhase::casting});
    Action const before_control_reset =
        fire_action(Hand::left, at(9030), 904, SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> before_control_event{before_control_reset};
    (void)release_policy->step(release_observation, before_control_event, at(9030));
    (*release_policy).reset();
    release_observation.sampled_at = at(9031);
    Action const after_control_reset = fire_action(Hand::right,
        at(9031),
        905,
        SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> after_control_event{after_control_reset};
    EffectFrame control_release =
        release_policy->step(release_observation, after_control_event, at(9031));
    INFO("control reset clears the bounded dual release stamp");
    REQUIRE(control_release.body.body_transient.has_value());
    CHECK(require_value(control_release.body.body_transient).started_at == at(9031));
}

TEST_CASE("skyrim.policy.magic_dual_equip_interruption_barrier", "[component][skyrim][policy]") {
    const auto release_policy = std::make_unique<Policy>();
    Observation release_observation = dual_sample(at(9040),
        CastMode::fire_and_forget,
        HandPhases{.left = CastPhase::charging, .right = CastPhase::casting});
    Action const before_effect_reset =
        fire_action(Hand::left, at(9040), 906, SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> before_effect_event{before_effect_reset};
    (void)release_policy->step(release_observation, before_effect_event, at(9040));
    release_observation.sampled_at = at(9041);
    Action const equip = action(EventKind::equip, Hand::right, at(9041), 907);
    const std::array<Action, 1> equip_event{equip};
    (void)release_policy->step(release_observation, equip_event, at(9041));
    release_observation.sampled_at = at(9042);
    Action const after_effect_reset = fire_action(Hand::right,
        at(9042),
        908,
        SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> after_effect_event{after_effect_reset};
    EffectFrame const effect_release =
        release_policy->step(release_observation, after_effect_event, at(9042));
    INFO("effect interruption clears the bounded dual release stamp");
    CHECK(started_at_of(effect_release.body.body_transient) == at(9042));
}

TEST_CASE("skyrim.policy.magic_native_dual_flag_cleared", "[component][skyrim][policy]") {
    const auto cleared_flag_policy = std::make_unique<Policy>();
    Observation release_observation = dual_sample(at(9100),
        CastMode::fire_and_forget,
        HandPhases{.left = CastPhase::charging, .right = CastPhase::casting});
    release_observation.dual_cast = false;
    release_observation.sampled_at = at(9100);
    Action const ordinary_fire =
        fire_action(Hand::left, at(9100), 910, SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> ordinary_event{ordinary_fire};
    EffectFrame ordinary_release =
        cleared_flag_policy->step(release_observation, ordinary_event, at(9100));
    INFO("a cleared native flag preserves the ordinary per-hand release");
    REQUIRE(ordinary_release.body.hand_transient[0].has_value());
    CHECK(!require_value(ordinary_release.body.hand_transient[0]).dual_cast);
    CHECK(!ordinary_release.body.body_transient.has_value());
}

TEST_CASE("skyrim.policy.magic_unrelated_source_not_deduplicated", "[component][skyrim][policy]") {
    const auto unrelated_policy = std::make_unique<Policy>();
    Observation release_observation = dual_sample(at(9200),
        CastMode::fire_and_forget,
        HandPhases{.left = CastPhase::charging, .right = CastPhase::casting});
    release_observation.dual_cast = true;
    release_observation.sampled_at = at(9200);
    Action const unrelated_left =
        fire_action(Hand::left, at(9200), 920, SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> unrelated_left_event{unrelated_left};
    (void)unrelated_policy->step(release_observation, unrelated_left_event, at(9200));
    release_observation.sampled_at = at(9205);
    Action const unrelated_right = fire_action(Hand::right,
        at(9205),
        921,
        SpellOrigin{.source = 43, .spell = SpellKind::frost});
    const std::array<Action, 1> unrelated_right_event{unrelated_right};
    EffectFrame unrelated_release =
        unrelated_policy->step(release_observation, unrelated_right_event, at(9205));
    INFO("a different source and spell are not suppressed by the dual release stamp");
    REQUIRE(unrelated_release.body.hand_transient[1].has_value());
    CHECK(require_value(unrelated_release.body.hand_transient[1]).spell == SpellKind::frost);
    CHECK(!require_value(unrelated_release.body.hand_transient[1]).dual_cast);
}

TEST_CASE("skyrim.policy.magic_dual_menu_stale_barrier", "[component][skyrim][policy]") {
    using namespace std::chrono_literals;
    const auto barrier_policy = std::make_unique<Policy>();
    Observation release_observation = dual_sample(at(9300),
        CastMode::fire_and_forget,
        HandPhases{.left = CastPhase::charging, .right = CastPhase::casting});
    release_observation.sampled_at = at(9300);
    Action const fresh_fire =
        fire_action(Hand::left, at(9300), 930, SpellOrigin{.source = 42, .spell = SpellKind::fire});
    const std::array<Action, 1> fresh_event{fresh_fire};
    (void)barrier_policy->step(release_observation, fresh_event, at(9300));
    release_observation.sampled_at = at(9301);
    release_observation.menus_clear = false;
    const EffectFrame menu_barrier = barrier_policy->step(release_observation, {}, at(9301));
    INFO("menu suppression clears active body feedback and its dedup history");
    CHECK(!menu_barrier.body.body_transient.has_value());
    CHECK(!menu_barrier.body.hand_transient[0].has_value());

    release_observation.menus_clear = true;
    release_observation.sampled_at = at(9302) - observation_max_age - 1ms;
    const EffectFrame stale_barrier = barrier_policy->step(release_observation, {}, at(9302));
    INFO("stale observation remains quiet and clears active body feedback");
    CHECK(!stale_barrier.body.body_transient.has_value());
    CHECK(!stale_barrier.body.hand_transient[0].has_value());
}

} // namespace skyrim_adapter::tests::policy
