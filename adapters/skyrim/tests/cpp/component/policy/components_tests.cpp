#include "policy/support/policy_test_support.hpp"
#include "test_support.hpp"
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <input_next_effects.h>
#include <skyrim_adapter/detail/body_cue_history.hpp>
#include <skyrim_adapter/detail/critical_health.hpp>
#include <skyrim_adapter/detail/effect_mixer.hpp>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/model.hpp>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::tests::policy {

TEST_CASE("skyrim.policy.component_ownership_and_mixing", "[component][skyrim][policy]") {
    detail::BodyCueHistory history;
    const MonoTime first_sample = at(9000);
    Observation bow = observation(first_sample);
    bow.weapon_drawn = true;
    bow.bow = BowPhase::draw;
    bow.hands[1] = {
        .equipped_id = 700,
        .caster_source_id = 0,
        .equipment = EquipmentKind::bow,
        .weapon = WeaponClass::bow,
    };

    history.refresh_held(bow, first_sample, false, EffectSettings{}, 1.0F);
    const auto first_held = history.held_cue(1, first_sample);
    INFO("history owns and materializes admitted held cues");
    REQUIRE(first_held.has_value());
    CHECK(require_value(first_held).kind == BodyCueKind::bow_tension);

    const MonoTime renewal_sample = at(9050);
    bow.sampled_at = renewal_sample;
    history.expire(renewal_sample);
    history.refresh_held(bow, renewal_sample, false, EffectSettings{}, 1.0F);
    const auto renewed_held = history.held_cue(1, renewal_sample);
    INFO("held cue renewal preserves its source identity and start");
    REQUIRE(renewed_held.has_value());
    REQUIRE(first_held.has_value());
    CHECK(require_value(renewed_held).started_at == require_value(first_held).started_at);
    CHECK(require_value(renewed_held).stable_id == require_value(first_held).stable_id);

    history.fence_held_admission(1, renewal_sample);
    history.clear_history();
    INFO("clearing cue history preserves the strict held admission fence");
    CHECK(!history.held_admission_open(1, renewal_sample));
    CHECK(history.held_admission_open(1, at(9051)));
    history.refresh_held(bow, renewal_sample, false, EffectSettings{}, 1.0F);
    INFO("sample at the admission fence cannot restore the held cue");
    CHECK(!history.held_cue(1, renewal_sample).has_value());
    bow.sampled_at = at(9051);
    history.refresh_held(bow, at(9051), false, EffectSettings{}, 1.0F);
    INFO("newer sample reopens held cue admission");
    CHECK(history.held_cue(1, at(9051)).has_value());

    detail::EffectMixer mixer;
    detail::EffectRequests requests{};
    detail::EffectMixer::offer_trigger(requests.left_request,
        {.mode = INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK,
            .start = 0.0F,
            .end = 0.0F,
            .strength = 0.5F,
            .frequency = 0.0F},
        first_sample,
        200,
        20);
    detail::EffectMixer::offer_trigger(requests.left_request,
        {.mode = INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK,
            .start = 0.0F,
            .end = 0.0F,
            .strength = 1.0F,
            .frequency = 0.0F},
        first_sample,
        200,
        19);
    detail::EffectMixer::offer_trigger(requests.left_held,
        {.mode = INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK,
            .start = 0.0F,
            .end = 0.0F,
            .strength = 1.0F,
            .frequency = 0.0F},
        first_sample,
        200,
        10);
    detail::EffectMixer::offer_rumble(requests.rumble_request,
        0,
        0.2F,
        0.3F,
        0.0F,
        0.0F,
        first_sample,
        200,
        0,
        0,
        0,
        20);
    const EffectFrame mixed = mixer.finish(first_sample, requests, {}, {}, 0.5F, 0.5F);
    INFO("higher-priority trigger overlay wins before channel scaling");
    REQUIRE(mixed.left.has_value());
    CHECK(require_value(mixed.left).payload.strength == 0.25F);
    INFO("persistent rumble envelope is mixed and scaled by its owner");
    REQUIRE(mixed.rumble.has_value());
    CHECK(require_value(mixed.rumble).payload.low_frequency == 0.1F);
    CHECK(require_value(mixed.rumble).payload.high_frequency == 0.15F);

    mixer.reset_rumble();
    const EffectFrame trigger_preserved = mixer.finish(at(9010), {}, {}, {}, 1.0F, 1.0F);
    INFO("body-history reset boundary clears rumble while retaining trigger overlays");
    REQUIRE(trigger_preserved.left.has_value());
    CHECK(require_value(trigger_preserved.left).payload.strength == 0.5F);
    CHECK(!trigger_preserved.rumble.has_value());
    mixer.reset();
    INFO("full mixer reset clears trigger overlays");
    CHECK(!mixer.finish(at(9020), {}, {}, {}, 1.0F, 1.0F).left.has_value());

    detail::CriticalHealthHeartbeat heartbeat;
    Observation health = observation(at(10000));
    health.in_combat = true;
    health.health_ratio = 0.19F;
    INFO("critical health baseline schedules rather than immediately emitting a heartbeat");
    CHECK(!heartbeat.update(health, at(10000), true, EffectSettings{}, 1.0F).has_value());
    health.health_ratio = 0.15F;
    INFO("critical health heartbeat respects its deterministic interval");
    CHECK(!heartbeat.update(health, at(11049), true, EffectSettings{}, 1.0F).has_value());
    const auto heartbeat_cue = heartbeat.update(health, at(11050), true, EffectSettings{}, 1.0F);
    INFO("critical health component emits a tuned cue at the heartbeat deadline");
    REQUIRE(heartbeat_cue.has_value());
    CHECK(require_value(heartbeat_cue).kind == BodyCueKind::critical_health);
    health.health_ratio = 0.25F;
    INFO("critical health component exits the hysteresis band at its recovery threshold");
    CHECK(!heartbeat.update(health, at(11060), true, EffectSettings{}, 1.0F).has_value());
    health.health_ratio = 0.19F;
    INFO("critical health re-entry starts a fresh heartbeat interval");
    CHECK(!heartbeat.update(health, at(11070), true, EffectSettings{}, 1.0F).has_value());
    heartbeat.reset();
    INFO("critical health reset restores baseline scheduling");
    CHECK(!heartbeat.update(health, at(12120), true, EffectSettings{}, 1.0F).has_value());
}

} // namespace skyrim_adapter::tests::policy
