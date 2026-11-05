#include "policy/support/policy_test_support.hpp"
#include "test_support.hpp"
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <input_next_effects.h>
#include <memory>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::tests::policy {
namespace {

std::uint64_t check_bow_draw_and_attached(Policy& policy, Observation& current) {
    current = observation(at(9000));
    current.hands[1] = {
        .equipped_id = 700,
        .caster_source_id = 0,
        .equipment = EquipmentKind::bow,
        .weapon = WeaponClass::bow,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    current.bow = BowPhase::draw;
    std::uint64_t draw_id = 0;
    {
        const EffectFrame frame = policy.step(current, {}, at(9000));
        REQUIRE(frame.body.held[1].has_value());
        const auto& drawing_held1 = require_value(frame.body.held[1]);
        INFO("observed right-hand bow draw creates a held tension cue with its equipment identity");
        CHECK(drawing_held1.kind == BodyCueKind::bow_tension);
        CHECK(drawing_held1.hand == Hand::right);
        CHECK(drawing_held1.source_id == 700);
        CHECK(drawing_held1.bow_phase == BowPhase::draw);
        draw_id = drawing_held1.stable_id;
    }

    current.sampled_at = at(9020);
    current.bow = BowPhase::attached;
    std::uint64_t attached_id = 0;
    {
        const EffectFrame frame = policy.step(current, {}, at(9020));
        REQUIRE(frame.body.held[1].has_value());
        const auto& attached_held1 = require_value(frame.body.held[1]);
        INFO("attached phase starts a new phase-specific held cue");
        CHECK(attached_held1.bow_phase == BowPhase::attached);
        CHECK(attached_held1.started_at == at(9020));
        CHECK(attached_held1.stable_id != draw_id);
        attached_id = attached_held1.stable_id;
    }

    current.sampled_at = at(9120);
    {
        const EffectFrame frame = policy.step(current, {}, at(9120));
        REQUIRE(frame.body.held[1].has_value());
        const auto& renewed_held1 = require_value(frame.body.held[1]);
        INFO("100 ms observation renewal preserves attached cue identity and original onset");
        CHECK(renewed_held1.stable_id == attached_id);
        CHECK(renewed_held1.started_at == at(9020));
    }
    return attached_id;
}

void check_bow_equipment_drawn_and_release(Policy& policy,
    Observation& current,
    std::uint64_t attached_id) {
    current.sampled_at = at(9140);
    current.hands[1].equipped_id = 701;
    {
        const EffectFrame frame = policy.step(current, {}, at(9140));
        REQUIRE(frame.body.held[1].has_value());
        const auto& replaced_held1 = require_value(frame.body.held[1]);
        INFO("equipment source change replaces the held bow cue identity");
        CHECK(replaced_held1.source_id == 701);
        CHECK(replaced_held1.started_at == at(9140));
        CHECK(replaced_held1.stable_id != attached_id);
    }

    current.sampled_at = at(9160);
    current.bow = BowPhase::drawn;
    {
        const EffectFrame frame = policy.step(current, {}, at(9160));
        REQUIRE(frame.body.held[1].has_value());
        REQUIRE(frame.body.body_transient.has_value());
        const auto& drawn_held1 = require_value(frame.body.held[1]);
        const auto& drawn_transient = require_value(frame.body.body_transient);
        INFO("drawn phase uses its held texture while preserving the existing full-draw transient");
        CHECK(drawn_held1.bow_phase == BowPhase::drawn);
        CHECK(drawn_transient.kind == BodyCueKind::bow_full_draw);
    }

    current.sampled_at = at(9170);
    Action release = action(EventKind::bow_release, Hand::right, at(9170), 91);
    release.source_known = true;
    release.source_kind = EquipmentKind::bow;
    release.weapon = WeaponClass::bow;
    const std::array<Action, 1> release_event{release};
    {
        const EffectFrame frame = policy.step(current, release_event, at(9170));
        INFO("fresh bow-release event cancels sustained tension and retains its release transient");
        CHECK(!frame.body.held[1].has_value());
        REQUIRE(frame.body.body_transient.has_value());
        CHECK(require_value(frame.body.body_transient).kind == BodyCueKind::bow_release);
    }

    current.sampled_at = at(9180);
    current.bow = BowPhase::releasing;
    {
        const EffectFrame frame = policy.step(current, {}, at(9180));
        INFO("releasing phase cancels held bow tension immediately");
        CHECK(!frame.body.held[1].has_value());
    }
}

void check_bow_inactive_phases(Policy& policy, Observation& current) {
    const auto check_inactive_phase = [&](std::int64_t timestamp, BowPhase phase) {
        current.sampled_at = at(timestamp);
        current.bow = phase;
        const EffectFrame inactive = policy.step(current, {}, at(timestamp));
        INFO("released, absent, and unknown bow phases do not create held tension");
        CHECK(!inactive.body.held[0].has_value());
        CHECK(!inactive.body.held[1].has_value());
    };
    check_inactive_phase(9200, BowPhase::released);
    check_inactive_phase(9220, BowPhase::none);
    check_inactive_phase(9240, BowPhase::unknown);
}

void check_bow_left_hand_and_interruption(Policy& policy, Observation& current) {
    current.sampled_at = at(9260);
    current.bow = BowPhase::attached;
    current.hands[1] = {};
    current.hands[0] = {
        .equipped_id = 702,
        .caster_source_id = 0,
        .equipment = EquipmentKind::bow,
        .weapon = WeaponClass::bow,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    {
        const EffectFrame frame = policy.step(current, {}, at(9260));
        REQUIRE(frame.body.held[0].has_value());
        const auto& left_bow_held0 = require_value(frame.body.held[0]);
        INFO("a coherent left-hand bow maps to the left held position");
        CHECK(left_bow_held0.hand == Hand::left);
        CHECK(left_bow_held0.source_id == 702);
        CHECK(!frame.body.held[1].has_value());
    }

    current.sampled_at = at(9280);
    current.weapon_drawn = false;
    {
        const EffectFrame frame = policy.step(current, {}, at(9280));
        INFO("sheathed weapon observation cancels held bow tension");
        CHECK(!frame.body.held[0].has_value());
        CHECK(!frame.body.held[1].has_value());
    }

    current.sampled_at = at(9300);
    current.weapon_drawn = true;
    Action const equip = action(EventKind::equip, Hand::left, at(9300), 92);
    const std::array<Action, 1> equip_event{equip};
    {
        const EffectFrame frame = policy.step(current, equip_event, at(9300));
        INFO("equip interruption cancels the current held bow cue");
        CHECK(!frame.body.held[0].has_value());
        CHECK(!frame.body.held[1].has_value());
    }

    current.sampled_at = at(9320);
    {
        const EffectFrame frame = policy.step(current, {}, at(9320));
        INFO("fresh left-hand bow observation can resume after equip interruption");
        CHECK(frame.body.held[0].has_value());
    }
}

void check_bow_suppression_and_death(Policy& policy, Observation& current) {
    current.sampled_at = at(9340);
    current.menus_clear = false;
    {
        const EffectFrame frame = policy.step(current, {}, at(9340));
        INFO("menu suppression invalidates held bow tension");
        CHECK(!frame.body.held[0].has_value());
    }

    current.sampled_at = at(9360);
    current.menus_clear = true;
    {
        const EffectFrame frame = policy.step(current, {}, at(9360));
        INFO("fresh observation can restore held bow tension after menu suppression");
        CHECK(frame.body.held[0].has_value());
    }

    current.sampled_at = at(9380);
    current.alive = false;
    {
        const EffectFrame frame = policy.step(current, {}, at(9380));
        INFO("death invalidates held bow tension");
        CHECK(!frame.body.held[0].has_value());
    }
}

void check_bow_release_fencing(Policy& policy,
    const Observation& current,
    const std::array<Action, 1>& release_event) {
    {
        const EffectFrame released = policy.step(current, release_event, at(10'000));
        INFO("bow release clears only its held hand while retaining its transient cue");
        CHECK(!released.body.held[1].has_value());
        REQUIRE(released.body.body_transient.has_value());
        CHECK(require_value(released.body.body_transient).kind == BodyCueKind::bow_release);
    }

    {
        const EffectFrame repeated_release = policy.step(current, release_event, at(10'020));
        INFO("replayed still-fresh bow-release action keeps the same-sample hand fenced");
        CHECK(!repeated_release.body.held[1].has_value());
    }

    {
        const EffectFrame stale_release = policy.step(current, {}, at(10'060));
        INFO("repeated worker tick with the release observation cannot restore bow hold, "
             "rumble, or trigger fallback");
        CHECK(!stale_release.body.held[1].has_value());
        CHECK(!stale_release.rumble.has_value());
        CHECK(!stale_release.right.has_value());
    }
}

void check_bow_release_recovery(Policy& policy,
    Observation& current,
    const std::array<Action, 1>& release_event) {
    current.sampled_at = at(10'010);
    std::uint64_t re_admitted_id = 0;
    MonoTime re_admitted_start{};
    {
        const EffectFrame fresh_after_release = policy.step(current, {}, at(10'061));
        REQUIRE(fresh_after_release.body.held[1].has_value());
        const auto& fresh_held1 = require_value(fresh_after_release.body.held[1]);
        INFO("a newer coherent sample re-admits bow hold after release");
        CHECK(fresh_held1.kind == BodyCueKind::bow_tension);
        re_admitted_id = fresh_held1.stable_id;
        re_admitted_start = fresh_held1.started_at;
    }

    {
        const EffectFrame duplicate_after_recovery =
            policy.step(current, release_event, at(10'080));
        REQUIRE(duplicate_after_recovery.body.held[1].has_value());
        REQUIRE(duplicate_after_recovery.right.has_value());
        const auto& dup_held1 = require_value(duplicate_after_recovery.body.held[1]);
        const auto& dup_right = require_value(duplicate_after_recovery.right);
        INFO("duplicate release after recovery preserves held cue identity, trigger, and "
             "steady bow fallback");
        CHECK(dup_held1.stable_id == re_admitted_id);
        CHECK(dup_held1.started_at == re_admitted_start);
        CHECK(dup_right.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
        CHECK(duplicate_after_recovery.rumble.has_value());
    }
}

void check_bow_equip_interruption_and_recovery(Policy& policy,
    Observation& current,
    const std::array<Action, 1>& equip_event) {
    {
        const EffectFrame interrupted = policy.step(current, equip_event, at(11'000));
        INFO("equip interruption clears held bow feedback");
        CHECK(!interrupted.body.held[1].has_value());
    }

    {
        const EffectFrame repeated_interruption = policy.step(current, equip_event, at(11'000));
        INFO("replayed interruption action keeps the same-sample held feedback fenced");
        CHECK(!repeated_interruption.body.held[1].has_value());
    }

    {
        const EffectFrame stale_interruption = policy.step(current, {}, at(11'020));
        INFO("same-sample worker ticks cannot restore held cues or bow fallback after "
             "interruption");
        CHECK(!stale_interruption.body.held[1].has_value());
        CHECK(!stale_interruption.rumble.has_value());
        CHECK(!stale_interruption.right.has_value());
    }

    current.sampled_at = at(11'021);
    std::uint64_t after_equip_id = 0;
    {
        const EffectFrame fresh_after_interruption = policy.step(current, {}, at(11'021));
        REQUIRE(fresh_after_interruption.body.held[1].has_value());
        const auto& fresh_interruption_held1 = require_value(fresh_after_interruption.body.held[1]);
        INFO("a newer coherent sample re-admits held bow feedback after interruption");
        CHECK(fresh_interruption_held1.stable_id != 0);
        after_equip_id = fresh_interruption_held1.stable_id;
    }

    {
        const EffectFrame duplicate_equip_after_recovery =
            policy.step(current, equip_event, at(11'022));
        REQUIRE(duplicate_equip_after_recovery.body.held[1].has_value());
        const auto& dup_equip_held1 = require_value(duplicate_equip_after_recovery.body.held[1]);
        INFO("duplicate equip after recovery does not reset the newer held cue");
        CHECK(dup_equip_held1.stable_id == after_equip_id);
    }
}

void check_bow_sheathe_interruption_and_recovery(Policy& policy,
    Observation& current,
    const std::array<Action, 1>& sheathe_event) {
    {
        const EffectFrame sheathed = policy.step(current, sheathe_event, at(11'030));
        INFO("sheathe interruption clears held bow feedback");
        CHECK(!sheathed.body.held[1].has_value());
    }

    {
        const EffectFrame stale_sheathe = policy.step(current, {}, at(11'040));
        INFO("same-sample worker ticks cannot restore held cues or fallback after sheath "
             "interruption");
        CHECK(!stale_sheathe.body.held[1].has_value());
        CHECK(!stale_sheathe.rumble.has_value());
        CHECK(!stale_sheathe.right.has_value());
    }

    current.sampled_at = at(11'041);
    std::uint64_t after_sheathe_id = 0;
    {
        const EffectFrame fresh_after_sheathe = policy.step(current, {}, at(11'041));
        REQUIRE(fresh_after_sheathe.body.held[1].has_value());
        const auto& fresh_sheathe_held1 = require_value(fresh_after_sheathe.body.held[1]);
        INFO("a newer coherent sample re-admits held bow feedback after sheath interruption");
        CHECK(fresh_sheathe_held1.stable_id != 0);
        after_sheathe_id = fresh_sheathe_held1.stable_id;
    }

    {
        const EffectFrame duplicate_sheathe_after_recovery =
            policy.step(current, sheathe_event, at(11'042));
        REQUIRE(duplicate_sheathe_after_recovery.body.held[1].has_value());
        const auto& dup_sheathe_held1 =
            require_value(duplicate_sheathe_after_recovery.body.held[1]);
        INFO("duplicate sheath after recovery does not reset the newer held cue");
        CHECK(dup_sheathe_held1.stable_id == after_sheathe_id);
    }
}

} // namespace

TEST_CASE("skyrim.policy.bow_phase_and_cancel", "[component][skyrim][policy]") {
    const auto policy_owner = std::make_unique<Policy>();
    Policy& policy = *policy_owner;
    Observation current = observation(at(100));
    current.hands[1] = {
        .equipped_id = 100,
        .caster_source_id = 0,
        .equipment = EquipmentKind::bow,
        .weapon = WeaponClass::bow,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    current.bow = BowPhase::draw;
    EffectFrame first = policy.step(current, {}, at(100));
    INFO("active bow draw uses rumble as its main feedback");
    REQUIRE(first.rumble.has_value());
    INFO("draw phase uses its authored low rumble envelope");
    CHECK(require_value(first.rumble).payload.low_frequency == 0.03F);
    INFO("first phase baseline does not add the supplementary trigger pulse");
    CHECK(!first.right.has_value());

    current.sampled_at = at(120);
    current.bow = BowPhase::attached;
    EffectFrame drawing = policy.step(current, {}, at(120));
    INFO("attached bow phase continues with moderate rumble");
    REQUIRE(drawing.rumble.has_value());
    CHECK(require_value(drawing.rumble).payload.high_frequency >= 0.10F);
    INFO("draw phase adds the moderate right trigger supplement");
    REQUIRE(drawing.right.has_value());
    CHECK(require_value(drawing.right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);

    current.sampled_at = at(140);
    current.bow = BowPhase::drawn;
    EffectFrame ready = policy.step(current, {}, at(140));
    INFO("actual drawn transition emits one readiness cue");
    REQUIRE(ready.rumble.has_value());
    CHECK(require_value(ready.rumble).payload.high_frequency == 0.22F);
    INFO("actual full-draw transition adds its semantic body cue");
    REQUIRE(ready.body.body_transient.has_value());
    CHECK(require_value(ready.body.body_transient).kind == BodyCueKind::bow_full_draw);

    current.sampled_at = at(190);
    EffectFrame const held = policy.step(current, {}, at(190));
    INFO("holding at the drawn phase does not repeat a readiness pulse");
    CHECK(!held.rumble.has_value());
    INFO("fully drawn bow has no persistent trigger resistance");
    CHECK(!held.right.has_value());

    Action release = action(EventKind::bow_release, Hand::right, at(200), 1);
    release.source_known = true;
    release.source_kind = EquipmentKind::bow;
    current.sampled_at = at(200);
    const std::array<Action, 1> release_events{release};
    EffectFrame released = policy.step(current, release_events, at(200));
    INFO("fresh bow release asks to clear the right trigger overlay");
    REQUIRE(released.right.has_value());
    CHECK(require_value(released.right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_OFF);
    INFO("bow release uses its rumble snap cue");
    REQUIRE(released.rumble.has_value());
    CHECK(require_value(released.rumble).payload.high_frequency == 0.52F);
    INFO("actual bow release adds a separate string-snap body cue");
    REQUIRE(released.body.body_transient.has_value());
    CHECK(require_value(released.body.body_transient).kind == BodyCueKind::bow_release);

    current.sampled_at = at(205);
    Action duplicate = release;
    duplicate.event.observed_at = at(205);
    const std::array<Action, 1> duplicate_event{duplicate};
    EffectFrame deduplicated = policy.step(current, duplicate_event, at(205));
    INFO("exact duplicate within 20 ms does not restart its trigger overlay");
    REQUIRE(deduplicated.right.has_value());
    CHECK(require_value(deduplicated.right).ttl_ms == 55);

    Action const equip = action(EventKind::equip, Hand::right, at(210), 2);
    current.sampled_at = at(210);
    const std::array<Action, 1> equip_event{equip};
    EffectFrame const cancelled = policy.step(current, equip_event, at(210));
    INFO("equip interruption discards the old release burst immediately");
    CHECK(!cancelled.rumble.has_value());
    CHECK(!cancelled.right.has_value());
}

TEST_CASE("skyrim.policy.sustained_bow_tension_history", "[component][skyrim][policy]") {
    const auto policy_owner = std::make_unique<Policy>();
    Policy& policy = *policy_owner;
    Observation current{};

    const std::uint64_t attached_id = check_bow_draw_and_attached(policy, current);
    check_bow_equipment_drawn_and_release(policy, current, attached_id);
    check_bow_inactive_phases(policy, current);
    check_bow_left_hand_and_interruption(policy, current);
    check_bow_suppression_and_death(policy, current);
}

TEST_CASE("skyrim.policy.bow_release_requires_fresh_sample", "[component][skyrim][policy]") {
    Observation current = observation(at(10'000));
    current.hands[1] = {
        .equipped_id = 801,
        .caster_source_id = 0,
        .equipment = EquipmentKind::bow,
        .weapon = WeaponClass::bow,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    current.bow = BowPhase::draw;

    const auto released_policy = std::make_unique<Policy>();
    (void)released_policy->step(current, {}, at(10'000));
    Action release = action(EventKind::bow_release, Hand::right, at(10'000), 801);
    release.source_known = true;
    release.source_kind = EquipmentKind::bow;
    release.weapon = WeaponClass::bow;
    const std::array<Action, 1> release_event{release};

    check_bow_release_fencing(*released_policy, current, release_event);
    check_bow_release_recovery(*released_policy, current, release_event);
}

TEST_CASE("skyrim.policy.bow_equip_sheathe_requires_fresh_sample", "[component][skyrim][policy]") {
    Observation current = observation(at(11'000));
    current.hands[1] = {
        .equipped_id = 802,
        .caster_source_id = 0,
        .equipment = EquipmentKind::bow,
        .weapon = WeaponClass::bow,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    current.bow = BowPhase::attached;
    const auto interrupted_policy = std::make_unique<Policy>();
    (void)interrupted_policy->step(current, {}, at(11'000));
    Action const equip = action(EventKind::equip, Hand::right, at(11'000), 802);
    const std::array<Action, 1> equip_event{equip};

    check_bow_equip_interruption_and_recovery(*interrupted_policy, current, equip_event);

    current.sampled_at = at(11'030);
    Action const sheathe = action(EventKind::begin_sheathe, Hand::right, at(11'030), 803);
    const std::array<Action, 1> sheathe_event{sheathe};

    check_bow_sheathe_interruption_and_recovery(*interrupted_policy, current, sheathe_event);
}

TEST_CASE("skyrim.policy.bow_release_preserves_other_hand", "[component][skyrim][policy]") {
    Observation current = observation(at(12'000));
    current.hands[0] = {
        .equipped_id = 803,
        .caster_source_id = 804,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = CastMode::concentration,
        .cast_phase = CastPhase::casting,
        .two_handed = false,
    };
    current.hands[1] = {
        .equipped_id = 805,
        .caster_source_id = 0,
        .equipment = EquipmentKind::bow,
        .weapon = WeaponClass::bow,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    current.bow = BowPhase::draw;
    const auto independent_hands = std::make_unique<Policy>();
    (void)independent_hands->step(current, {}, at(12'000));
    Action right_release = action(EventKind::bow_release, Hand::right, at(12'000), 803);
    right_release.source_known = true;
    right_release.source_kind = EquipmentKind::bow;
    right_release.weapon = WeaponClass::bow;
    const std::array<Action, 1> right_release_event{right_release};
    EffectFrame independent = independent_hands->step(current, right_release_event, at(12'000));
    REQUIRE(independent.body.held[0].has_value());
    const auto& ind_held0 = require_value(independent.body.held[0]);
    INFO("right bow release leaves the independent left concentration hand active");
    CHECK(ind_held0.kind == BodyCueKind::concentration);
    CHECK(!independent.body.held[1].has_value());
}

} // namespace skyrim_adapter::tests::policy
