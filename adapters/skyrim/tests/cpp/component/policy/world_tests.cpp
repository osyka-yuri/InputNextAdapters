#include "policy/support/policy_test_support.hpp"
#include "test_support.hpp"
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <skyrim_adapter/content_observation.hpp>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>
#include <utility>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::tests::policy {

using test::require_location;

namespace {

Observation make_bow_observation(MonoTime timestamp) {
    Observation result = world_observation(timestamp);
    result.hands[1] = {
        .equipped_id = 701,
        .caster_source_id = 701,
        .equipment = EquipmentKind::bow,
        .weapon = WeaponClass::bow,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    result.bow = BowPhase::draw;
    return result;
}

void check_world_combat_arbitration() {
    const auto combat_policy = std::make_unique<Policy>();
    Observation current = world_observation(at(100));
    (void)combat_policy->step(current, {}, at(100));
    current = world_observation(at(120));
    world(current).staggered = true;
    Action hit = action(EventKind::hit, Hand::unknown, at(120), 1);
    hit.incoming = true;
    const std::array<Action, 1> hit_action{hit};
    const EffectFrame combat = combat_policy->step(current, hit_action, at(120));
    INFO("ordinary combat body feedback wins over a simultaneous environmental cue");
    REQUIRE(combat.body.body_transient.has_value());
    CHECK(require_value(combat.body.body_transient).kind == BodyCueKind::incoming_hit);
    INFO("ordinary combat rumble wins over a simultaneous environmental rumble");
    REQUIRE(combat.rumble.has_value());
    CHECK(require_value(combat.rumble).payload.low_frequency == 0.45F);
}

void check_world_knockdown_arbitration() {
    const auto knockdown_policy = std::make_unique<Policy>();
    Observation current = world_observation(at(200));
    (void)knockdown_policy->step(current, {}, at(200));
    current = world_observation(at(220));
    world(current).knocked_down = true;
    Action hit = action(EventKind::hit, Hand::unknown, at(220), 2);
    hit.incoming = true;
    const std::array<Action, 1> knockdown_hit{hit};
    const EffectFrame knockdown = knockdown_policy->step(current, knockdown_hit, at(220));
    INFO("actual knockdown can outrank an ordinary incoming hit");
    REQUIRE(knockdown.body.body_transient.has_value());
    CHECK(require_value(knockdown.body.body_transient).world_kind == WorldCueKind::knockdown_entry);
    INFO("knockdown rumble uses its matching higher-priority envelope");
    REQUIRE(knockdown.rumble.has_value());
    CHECK(require_value(knockdown.rumble).payload.low_frequency == 0.24F);
}

void check_world_silent_scale() {
    const auto silent_config = std::make_unique<PolicyConfig>();
    silent_config->trigger_scale = 1.0F;
    silent_config->rumble_scale = 0.0F;
    const auto silent_policy = std::make_unique<Policy>(*silent_config);
    Observation current = world_observation(at(300));
    (void)silent_policy->step(current, {}, at(300));
    current = world_observation(at(320));
    world(current).staggered = true;
    const EffectFrame silent = silent_policy->step(current, {}, at(320));
    INFO("zero body scale preserves silent world cue identity without waveform output");
    REQUIRE(silent.body.body_transient.has_value());
    CHECK(require_value(silent.body.body_transient).world_kind == WorldCueKind::stagger_entry);
    CHECK(require_value(silent.body.body_transient).scale == 0.0F);
    INFO("zero rumble scale silences the matching classic rumble");
    CHECK(!silent.rumble.has_value());
}

void check_world_audio_rebind_barrier() {
    const auto barrier_policy = std::make_unique<Policy>();
    Observation current = world_observation(at(400));
    (void)barrier_policy->step(current, {}, at(400));
    barrier_policy->reset_body_history(at(420));
    current = world_observation(at(420));
    world(current).staggered = true;
    {
        const EffectFrame blocked = barrier_policy->step(current, {}, at(420));
        INFO("audio rebind barrier suppresses its same-time world sample");
        CHECK(!blocked.body.body_transient.has_value());
    }
    current = world_observation(at(440));
    {
        const EffectFrame baseline = barrier_policy->step(current, {}, at(440));
        INFO("first fresh world sample after rebind establishes a silent baseline");
        CHECK(!baseline.body.body_transient.has_value());
    }
    current = world_observation(at(460));
    world(current).staggered = true;
    {
        const EffectFrame resumed = barrier_policy->step(current, {}, at(460));
        INFO("world transitions resume after the silent rebind baseline");
        REQUIRE(resumed.body.body_transient.has_value());
        CHECK(require_value(resumed.body.body_transient).world_kind == WorldCueKind::stagger_entry);
    }
}

void check_world_reset_behavior() {
    const auto reset_policy = std::make_unique<Policy>();
    Observation current = world_observation(at(500));
    (void)reset_policy->step(current, {}, at(500));
    current = world_observation(at(520));
    world(current).staggered = true;
    {
        const EffectFrame admitted = reset_policy->step(current, {}, at(520));
        INFO("world cue is admitted before a policy reset");
        REQUIRE(admitted.body.body_transient.has_value());
        CHECK(
            require_value(admitted.body.body_transient).world_kind == WorldCueKind::stagger_entry);
    }
    (*reset_policy).reset();
    current = world_observation(at(540));
    {
        const EffectFrame reset_baseline = reset_policy->step(current, {}, at(540));
        INFO("policy reset clears world history and treats the next sample as a silent baseline");
        CHECK(!reset_baseline.body.body_transient.has_value());
    }
}

void check_content_combat_arbitration(const ContentObservation& content) {
    const auto combat_policy = std::make_unique<Policy>();
    Observation current = observation(at(100));
    current.content = content;
    (void)combat_policy->step(current, {}, at(100));
    current = observation(at(120));
    current.content = content;
    current.content.cold_stage = 3;
    Action hit = action(EventKind::hit, Hand::unknown, at(120), 501);
    hit.incoming = true;
    const std::array<Action, 1> hit_action{hit};
    const EffectFrame combat = combat_policy->step(current, hit_action, at(120));
    INFO("ordinary combat body feedback wins over a simultaneous Survival stage cue");
    REQUIRE(combat.body.body_transient.has_value());
    CHECK(require_value(combat.body.body_transient).kind == BodyCueKind::incoming_hit);
    INFO("ordinary combat rumble wins over a simultaneous Survival stage rumble");
    REQUIRE(combat.rumble.has_value());
    CHECK(require_value(combat.rumble).payload.low_frequency == 0.45F);
}

void check_content_cold_pulse_and_hold(const ContentObservation& content) {
    const auto content_policy = std::make_unique<Policy>();
    Observation current = observation(at(200));
    current.content = content;
    (void)content_policy->step(current, {}, at(200));
    current = observation(at(220));
    current.content = content;
    current.content.cold_stage = 3;
    {
        const EffectFrame cold = content_policy->step(current, {}, at(220));
        INFO("cold pulse metadata maps exactly to its bounded transient body cue");
        REQUIRE(cold.body.body_transient.has_value());
        const auto& cold_cue = require_value(cold.body.body_transient);
        CHECK(cold_cue.kind == BodyCueKind::survival_cold);
        CHECK(cold_cue.content_stage == 3);
        CHECK(cold_cue.started_at == current.sampled_at);
        CHECK(cold_cue.duration_ms == 260);
        CHECK(cold_cue.priority == 7);
        CHECK(cold_cue.scale == 1.0F);

        INFO("cold pulse uses its existing fade envelope with a coordinator-safe sampled TTL");
        REQUIRE(cold.rumble.has_value());
        const auto& cold_rumble = require_value(cold.rumble);
        CHECK(std::abs(cold_rumble.payload.low_frequency - 0.09F) < 0.00001F);
        CHECK(std::abs(cold_rumble.payload.high_frequency - 0.12F) < 0.00001F);
        CHECK(cold_rumble.ttl_ms == 200);
    }

    current = observation(at(240));
    current.content = content;
    current.content.cold_stage = 3;
    {
        const EffectFrame held = content_policy->step(current, {}, at(240));
        INFO("the same Survival stage sample retains the original finite cue without replaying it");
        REQUIRE(held.body.body_transient.has_value());
        CHECK(require_value(held.body.body_transient).started_at == at(220));
    }
}

void check_content_silent_scale(const ContentObservation& content) {
    const auto silent_config = std::make_unique<PolicyConfig>();
    silent_config->trigger_scale = 1.0F;
    silent_config->rumble_scale = 0.0F;
    const auto silent_policy = std::make_unique<Policy>(*silent_config);
    Observation current = observation(at(300));
    current.content = content;
    (void)silent_policy->step(current, {}, at(300));
    current = observation(at(320));
    current.content = content;
    current.content.hunger_stage = 3;
    const EffectFrame silent = silent_policy->step(current, {}, at(320));
    INFO("zero rumble scale retains content cue identity with a silent body scale");
    REQUIRE(silent.body.body_transient.has_value());
    CHECK(require_value(silent.body.body_transient).kind == BodyCueKind::survival_hunger);
    CHECK(require_value(silent.body.body_transient).scale == 0.0F);
    INFO("zero rumble scale silences the matching content classic rumble");
    CHECK(!silent.rumble.has_value());
}

void check_content_rebind_and_reset(const ContentObservation& content) {
    const auto barrier_policy = std::make_unique<Policy>();
    Observation current = observation(at(400));
    current.content = content;
    (void)barrier_policy->step(current, {}, at(400));
    barrier_policy->reset_body_history(at(420));
    current = observation(at(420));
    current.content = content;
    current.content.exhaustion_stage = 3;
    {
        const EffectFrame blocked = barrier_policy->step(current, {}, at(420));
        INFO("audio rebind barrier suppresses its same-time content sample");
        CHECK(!blocked.body.body_transient.has_value());
    }
    current = observation(at(440));
    current.content = content;
    current.content.exhaustion_stage = 3;
    {
        const EffectFrame baseline = barrier_policy->step(current, {}, at(440));
        INFO("first fresh content sample after rebind establishes a baseline");
        CHECK(!baseline.body.body_transient.has_value());
    }
    current = observation(at(460));
    current.content = content;
    current.content.exhaustion_stage = 4;
    {
        const EffectFrame resumed = barrier_policy->step(current, {}, at(460));
        INFO("content edges resume only after the silent rebind baseline");
        REQUIRE(resumed.body.body_transient.has_value());
        CHECK(require_value(resumed.body.body_transient).kind == BodyCueKind::survival_exhaustion);
    }

    (*barrier_policy).reset();
    current = observation(at(480));
    current.content = content;
    current.content.exhaustion_stage = 5;
    {
        const EffectFrame reset_baseline = barrier_policy->step(current, {}, at(480));
        INFO("policy reset makes the next content sample a silent baseline");
        CHECK(!reset_baseline.body.body_transient.has_value());
    }
}

} // namespace

TEST_CASE("skyrim.policy.critical_health_heartbeat", "[component][skyrim][policy]") {
    const auto policy_owner = std::make_unique<Policy>();
    Policy& policy = *policy_owner;
    Observation current = observation(at(700));
    current.in_combat = true;
    current.health_ratio = 0.19F;
    {
        EffectFrame const baseline = policy.step(current, {}, at(700));
        INFO("critical health baseline starts quietly without a catch-up beat");
        CHECK(!baseline.rumble.has_value());
    }

    current.sampled_at = at(1750);
    {
        EffectFrame heartbeat = policy.step(current, {}, at(1750));
        INFO("low combat health produces the authored rumble and PCM heartbeat after its interval");
        REQUIRE(heartbeat.rumble.has_value());
        CHECK(require_value(heartbeat.rumble).payload.low_frequency == 0.16F);
        REQUIRE(heartbeat.body.body_transient.has_value());
        CHECK(require_value(heartbeat.body.body_transient).kind == BodyCueKind::critical_health);
    }

    current.sampled_at = at(1810);
    current.health_ratio = 0.24F;
    {
        EffectFrame const hysteresis = policy.step(current, {}, at(1810));
        INFO("health between 20 and 25 percent remains in the quiet beat gap");
        CHECK(!hysteresis.rumble.has_value());
    }

    current.sampled_at = at(2800);
    {
        EffectFrame second_pair = policy.step(current, {}, at(2800));
        INFO("health below the recovery threshold keeps the heartbeat active");
        REQUIRE(second_pair.rumble.has_value());
        CHECK(require_value(second_pair.rumble).payload.low_frequency == 0.16F);
    }

    current.sampled_at = at(2810);
    current.health_ratio = 0.25F;
    (void)policy.step(current, {}, at(2810));
    current.sampled_at = at(3900);
    {
        EffectFrame const recovered = policy.step(current, {}, at(3900));
        INFO("health at the recovery threshold disables later heartbeat pairs");
        CHECK(!recovered.rumble.has_value());
    }

    current.sampled_at = at(4000);
    current.in_combat = false;
    current.health_ratio = 0.10F;
    {
        EffectFrame const out_of_combat = policy.step(current, {}, at(4000));
        INFO("critical health heartbeat remains gated by combat state");
        CHECK(!out_of_combat.rumble.has_value());
    }

    const auto profile_disabled_config = std::make_unique<PolicyConfig>();
    profile_disabled_config->effects.body[static_cast<std::size_t>(EffectId::critical_health)]
        .enabled = false;
    const auto profile_disabled_state = std::make_unique<Policy>(*profile_disabled_config);
    Policy& profile_disabled = *profile_disabled_state;
    current = observation(at(5000));
    current.in_combat = true;
    current.health_ratio = 0.10F;
    (void)profile_disabled.step(current, {}, at(5000));
    current.sampled_at = at(6050);
    {
        const EffectFrame suppressed = profile_disabled.step(current, {}, at(6050));
        INFO("disabled Body.CriticalHealth profile suppresses both classic and PCM "
             "heartbeat output");
        CHECK(!suppressed.rumble.has_value());
        CHECK(!suppressed.body.body_transient.has_value());
    }
}

TEST_CASE("skyrim.policy.world_effect_policy_arbitration_and_barriers",
    "[component][skyrim][policy]") {
    check_world_combat_arbitration();
    check_world_knockdown_arbitration();
    check_world_silent_scale();
    check_world_audio_rebind_barrier();
    check_world_reset_behavior();
}

TEST_CASE("skyrim.policy.content_effect_policy_arbitration_and_barriers",
    "[component][skyrim][policy]") {
    ContentObservation content{};
    content.beast = BeastForm::none;
    content.survival_enabled = true;
    content.cold_stage = 2;
    content.hunger_stage = 2;
    content.exhaustion_stage = 2;

    check_content_combat_arbitration(content);
    check_content_cold_pulse_and_hold(content);
    check_content_silent_scale(content);
    check_content_rebind_and_reset(content);
}

TEST_CASE("skyrim.policy.world_survival_fade_preserves_concentration",
    "[component][skyrim][policy]") {
    const auto content_observation = [](MonoTime timestamp, unsigned cold_stage) {
        Observation result = observation(timestamp);
        result.content = ContentObservation{
            .beast = BeastForm::none,
            .survival_enabled = true,
            .cold_stage = cold_stage,
            .hunger_stage = 2U,
            .exhaustion_stage = 2U,
        };
        result.hands[0] = {
            .equipped_id = 42,
            .caster_source_id = 42,
            .equipment = EquipmentKind::spell,
            .weapon = WeaponClass::none,
            .spell = SpellKind::shock,
            .cast_mode = CastMode::concentration,
            .cast_phase = CastPhase::casting,
            .two_handed = false,
        };
        return result;
    };

    for (const auto [stage, duration] : {std::pair{4U, 320}, std::pair{5U, 380}}) {
        const auto held_only = std::make_unique<Policy>();
        const auto with_survival = std::make_unique<Policy>();
        Observation baseline = content_observation(at(3000), 2);
        (void)held_only->step(baseline, {}, at(3000));
        (void)with_survival->step(baseline, {}, at(3000));

        Observation raised = content_observation(at(3020), stage);
        (void)with_survival->step(raised, {}, at(3020));
        EffectFrame comparison{};
        EffectFrame overlap{};
        const std::int64_t final_age = duration - 30;
        for (std::int64_t age = 100; age < final_age; age += 100) {
            const MonoTime timestamp = at(3020 + age);
            baseline.sampled_at = timestamp;
            raised.sampled_at = timestamp;
            comparison = held_only->step(baseline, {}, timestamp);
            overlap = with_survival->step(raised, {}, timestamp);
        }
        const MonoTime timestamp = at(3020 + final_age);
        baseline.sampled_at = timestamp;
        raised.sampled_at = timestamp;
        comparison = held_only->step(baseline, {}, timestamp);
        overlap = with_survival->step(raised, {}, timestamp);
        INFO(
            "stage-four/five Survival fades retain the active concentration fallback contribution");
        REQUIRE(comparison.rumble.has_value());
        REQUIRE(overlap.rumble.has_value());
        CHECK(overlap.rumble->payload.low_frequency >= comparison.rumble->payload.low_frequency);
        CHECK(overlap.rumble->payload.high_frequency >= comparison.rumble->payload.high_frequency);
    }
}

TEST_CASE("skyrim.policy.world_stagger_fade_preserves_bow", "[component][skyrim][policy]") {
    const auto bow_only = std::make_unique<Policy>();
    const auto with_stagger = std::make_unique<Policy>();
    Observation bow = make_bow_observation(at(4000));
    (void)bow_only->step(bow, {}, at(4000));
    (void)with_stagger->step(bow, {}, at(4000));
    bow.sampled_at = at(4020);
    Observation staggered = make_bow_observation(at(4020));
    world(staggered).staggered = true;
    (void)with_stagger->step(staggered, {}, at(4020));
    bow.sampled_at = at(4120);
    staggered.sampled_at = at(4120);
    const EffectFrame bow_baseline = bow_only->step(bow, {}, at(4120));
    const EffectFrame stagger_overlap = with_stagger->step(staggered, {}, at(4120));
    INFO("late stagger fade retains active bow-draw fallback contribution");
    REQUIRE(bow_baseline.rumble.has_value());
    REQUIRE(stagger_overlap.rumble.has_value());
    CHECK(require_value(stagger_overlap.rumble).payload.low_frequency >=
          require_value(bow_baseline.rumble).payload.low_frequency);
    CHECK(require_value(stagger_overlap.rumble).payload.high_frequency >=
          require_value(bow_baseline.rumble).payload.high_frequency);
}

TEST_CASE("skyrim.policy.world_landing_fade_preserves_bow", "[component][skyrim][policy]") {
    const auto grounded_bow = std::make_unique<Policy>();
    const auto falling_bow = std::make_unique<Policy>();
    Observation ground = make_bow_observation(at(5000));
    Observation falling = make_bow_observation(at(5000));
    world(falling).airborne = true;
    require_location(world(falling)).z = 200.0F;
    (void)grounded_bow->step(ground, {}, at(5000));
    (void)falling_bow->step(falling, {}, at(5000));
    ground.sampled_at = at(5050);
    falling.sampled_at = at(5050);
    world(falling).airborne = true;
    require_location(world(falling)).z = 140.0F;
    (void)grounded_bow->step(ground, {}, at(5050));
    (void)falling_bow->step(falling, {}, at(5050));
    ground.sampled_at = at(5070);
    falling.sampled_at = at(5070);
    world(falling).airborne = false;
    require_location(world(falling)).z = 120.0F;
    (void)grounded_bow->step(ground, {}, at(5070));
    (void)falling_bow->step(falling, {}, at(5070));
    ground.sampled_at = at(5150);
    falling.sampled_at = at(5150);
    const EffectFrame ground_hold = grounded_bow->step(ground, {}, at(5150));
    const EffectFrame landing_overlap = falling_bow->step(falling, {}, at(5150));
    INFO("late landing fade retains active bow-draw fallback contribution");
    REQUIRE(ground_hold.rumble.has_value());
    REQUIRE(landing_overlap.rumble.has_value());
    CHECK(require_value(landing_overlap.rumble).payload.low_frequency >=
          require_value(ground_hold.rumble).payload.low_frequency);
    CHECK(require_value(landing_overlap.rumble).payload.high_frequency >=
          require_value(ground_hold.rumble).payload.high_frequency);
}

TEST_CASE("skyrim.policy.world_bow_priority_over_stagger_survival", "[component][skyrim][policy]") {
    const auto bow_attack_policy = std::make_unique<Policy>();
    Observation attack = world_observation(at(6000));
    attack.content = ContentObservation{
        .beast = BeastForm::none,
        .survival_enabled = true,
        .cold_stage = 2U,
        .hunger_stage = 2U,
        .exhaustion_stage = 2U,
    };
    (void)bow_attack_policy->step(attack, {}, at(6000));
    attack = world_observation(at(6020));
    attack.content = ContentObservation{
        .beast = BeastForm::none,
        .survival_enabled = true,
        .cold_stage = 5U,
        .hunger_stage = 2U,
        .exhaustion_stage = 2U,
    };
    world(attack).staggered = true;
    attack.hands[1] = {
        .equipped_id = 701,
        .caster_source_id = 701,
        .equipment = EquipmentKind::bow,
        .weapon = WeaponClass::bow,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    Action draw = action(EventKind::bow_draw, Hand::right, at(6020), 880);
    draw.source_known = true;
    draw.source_kind = EquipmentKind::bow;
    draw.weapon = WeaponClass::bow;
    const std::array<Action, 1> draw_events{draw};
    const EffectFrame stage_five_attack = bow_attack_policy->step(attack, draw_events, at(6020));
    INFO("stage-five Survival and stagger remain below a valid bow-draw body/fallback recipe");
    REQUIRE(stage_five_attack.body.body_transient.has_value());
    CHECK(require_value(stage_five_attack.body.body_transient).kind == BodyCueKind::bow_draw);
    CHECK(require_value(stage_five_attack.body.body_transient).priority == 15);
    REQUIRE(stage_five_attack.rumble.has_value());
    CHECK(require_value(stage_five_attack.rumble).payload.low_frequency >= 0.04F);
    CHECK(require_value(stage_five_attack.rumble).payload.high_frequency >= 0.14F);
}

TEST_CASE("skyrim.policy.world_melee_and_knockdown_priority", "[component][skyrim][policy]") {
    const auto melee_policy = std::make_unique<Policy>();
    Observation melee = world_observation(at(7000));
    (void)melee_policy->step(melee, {}, at(7000));
    melee = world_observation(at(7020));
    world(melee).staggered = true;
    Action swing = action(EventKind::weapon_swing, Hand::right, at(7020), 881);
    swing.source_known = true;
    swing.source_kind = EquipmentKind::weapon;
    swing.weapon = WeaponClass::two_handed;
    const std::array<Action, 1> swing_events{swing};
    const EffectFrame melee_overlap = melee_policy->step(melee, swing_events, at(7020));
    INFO("stagger remains below a valid two-handed melee swing body/fallback recipe");
    REQUIRE(melee_overlap.body.body_transient.has_value());
    CHECK(require_value(melee_overlap.body.body_transient).kind == BodyCueKind::melee_swing);
    CHECK(require_value(melee_overlap.body.body_transient).weapon == WeaponClass::two_handed);
    REQUIRE(melee_overlap.rumble.has_value());
    CHECK(require_value(melee_overlap.rumble).payload.low_frequency >= 0.26F);
    CHECK(require_value(melee_overlap.rumble).payload.high_frequency >= 0.20F);

    melee.sampled_at = at(7040);
    world(melee).knocked_down = true;
    const EffectFrame knockdown = melee_policy->step(melee, {}, at(7040));
    INFO("actual knockdown retains precedence over an ordinary two-handed swing");
    REQUIRE(knockdown.body.body_transient.has_value());
    CHECK(require_value(knockdown.body.body_transient).world_kind == WorldCueKind::knockdown_entry);
    CHECK(require_value(knockdown.body.body_transient).priority == 75);
}

} // namespace skyrim_adapter::tests::policy
