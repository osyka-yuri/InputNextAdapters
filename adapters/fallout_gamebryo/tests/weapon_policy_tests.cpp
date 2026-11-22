#include "policy_test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/policy.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <input_next_effects.h>
#include <inputnext_adapters/output_frame.hpp>

#include <array>
#include <chrono>
#include <memory>

namespace fallout_gamebryo::tests {
using namespace std::chrono_literals;
using inputnext::adapters::TriggerIntent;

TEST_CASE("fallout_gamebryo.policy.shot_body_recoil_and_trigger_accent_are_timed",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    const auto observation = gameplay();
    const std::array events{shot(observation, 12U, 1000)};

    const auto fired = policy->update(observation, events, at(1000));
    REQUIRE(fired.right.has_value());
    const auto fired_trigger = fired.right.value_or(TriggerIntent{});
    CHECK(fired_trigger.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(fired_trigger.ttl_ms == 38U);
    CHECK(voice_count(fired) == 1U);
    const auto recoil = voice_for(fired, BodyEffect::shot_pistol);
    CHECK(recoil.started_at == at(1000));
    CHECK_FALSE(recoil.wave.audio_envelope.enabled);
    const auto [left_peak, right_peak] = pcm_peaks(recoil);
    CHECK(left_peak > 0.001F);
    CHECK(right_peak > 0.001F);

    auto later = observation;
    later.frame = 2U;
    later.at = at(1010);
    const auto refreshed = policy->update(later, {}, at(1010));
    REQUIRE(refreshed.right.has_value());
    const auto refreshed_trigger = refreshed.right.value_or(TriggerIntent{});
    CHECK(refreshed_trigger.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(refreshed_trigger.ttl_ms == 28U);

    later.frame = 3U;
    later.at = at(1039);
    const auto expired = policy->update(later, {}, at(1039));
    REQUIRE(expired.right.has_value());
    CHECK(expired.right.value_or(TriggerIntent{}).payload.mode ==
          INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
}

TEST_CASE("An interrupted recoil pulse does not resume on a later frame",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    const auto observation = gameplay();
    const std::array events{shot(observation, 12U, 1000)};
    const auto fired = policy->update(observation, events, observation.at);
    REQUIRE(fired.right);
    CHECK(
        fired.right.value_or(TriggerIntent{}).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);

    auto interrupted = observation;
    interrupted.frame = 2U;
    interrupted.at = at(1008);
    SECTION("the weapon is holstered") {
        interrupted.weapon.drawn = false;
    }
    SECTION("a reload begins") {
        interrupted.reloading = true;
    }
    SECTION("attack changes trigger sides") {
        interrupted.attack_trigger = TriggerSide::left;
    }
    const auto cleared = policy->update(interrupted, {}, interrupted.at);
    CHECK_FALSE(cleared.right);

    auto resumed = observation;
    resumed.frame = 3U;
    resumed.at = at(1016);
    const auto frame = policy->update(resumed, {}, resumed.at);
    REQUIRE(frame.right);
    CHECK(
        frame.right.value_or(TriggerIntent{}).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
}

TEST_CASE("Automatic shots use a stronger trigger pulse while retaining body scaling",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto semi_auto_policy = std::make_unique<Policy>(*settings);
    const auto automatic_policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.weapon.kind = WeaponClass::rifle;
    observation.aiming = true;
    auto fired = shot(observation, 12U, 1000);
    const auto semi_auto = semi_auto_policy->update(observation, std::array{fired}, observation.at);
    observation.weapon.automatic = true;
    fired.automatic = true;
    const auto automatic = automatic_policy->update(observation, std::array{fired}, observation.at);

    const auto semi_pulse = semi_auto.right.value_or(TriggerIntent{});
    const auto auto_pulse = automatic.right.value_or(TriggerIntent{});
    REQUIRE(semi_pulse.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    REQUIRE(auto_pulse.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(semi_pulse.payload.strength == Catch::Approx(0.42F * settings->trigger_scale));
    CHECK(auto_pulse.payload.strength == Catch::Approx(0.49F * settings->trigger_scale));
    CHECK(auto_pulse.payload.frequency == semi_pulse.payload.frequency);
    CHECK(auto_pulse.ttl_ms == semi_pulse.ttl_ms);
    REQUIRE(semi_auto.left);
    REQUIRE(automatic.left);
    CHECK(automatic.left.value_or(TriggerIntent{}).payload.strength ==
          semi_auto.left.value_or(TriggerIntent{}).payload.strength);

    const auto single_body = voice_for(semi_auto, BodyEffect::shot_rifle);
    const auto automatic_body = voice_for(automatic, BodyEffect::shot_rifle);
    CHECK(automatic_body.scale == Catch::Approx(single_body.scale * 0.84F));

    observation.at += 50ms;
    ++observation.frame;
    const auto held = automatic_policy->update(observation, {}, observation.at);
    const auto resistance = held.right.value_or(TriggerIntent{});
    REQUIRE(resistance.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    CHECK(resistance.payload.strength == Catch::Approx(0.30F * settings->trigger_scale * 0.82F));
}

TEST_CASE("A confirmed empty magazine releases only the attack resistance",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    const auto loaded = policy->update(observation, {}, observation.at);
    REQUIRE(loaded.right);
    CHECK(
        loaded.right.value_or(TriggerIntent{}).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);

    ++observation.frame;
    observation.at += 10ms;
    observation.weapon.loaded_ammo.reset();
    const auto unknown = policy->update(observation, {}, observation.at);
    CHECK(unknown.right.has_value());

    ++observation.frame;
    observation.at += 10ms;
    observation.weapon.loaded_ammo = 0U;
    const auto empty = policy->update(observation, {}, observation.at);
    CHECK_FALSE(empty.right);
    CHECK(voice_count(empty) == 0U);

    ++observation.frame;
    observation.at += 10ms;
    observation.aiming = true;
    const auto aiming = policy->update(observation, {}, observation.at);
    REQUIRE(aiming.left);
    CHECK(
        aiming.left.value_or(TriggerIntent{}).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    CHECK_FALSE(aiming.right);
}

TEST_CASE("Aiming retains separate firing and aiming trigger resistance",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.aiming = true;

    auto frame = policy->update(observation, {}, observation.at);
    REQUIRE(frame.right);
    REQUIRE(frame.left);
    CHECK(
        frame.right.value_or(TriggerIntent{}).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    CHECK(frame.left.value_or(TriggerIntent{}).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);

    ++observation.frame;
    observation.at += 10ms;
    observation.aim_trigger = TriggerSide::none;
    frame = policy->update(observation, {}, observation.at);
    REQUIRE(frame.right);
    CHECK_FALSE(frame.left);
}

TEST_CASE("A confirmed dry fire plays only the empty weapon cue", "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.weapon.loaded_ammo = 0U;
    auto empty_click = shot(observation, 13U, 1000);
    empty_click.kind = EventKind::dry_fire;

    const auto frame = policy->update(observation, std::array{empty_click}, observation.at);
    REQUIRE(voice_count(frame) == 1U);
    CHECK(voice_for(frame, BodyEffect::dry_fire).started_at == empty_click.at);
    CHECK_FALSE(frame.right);
    CHECK_FALSE(frame.left);
}

TEST_CASE("Aiming takes priority on a shared trigger, including when the magazine is empty",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.weapon.automatic = true;
    observation.aiming = true;
    observation.aim_trigger = observation.attack_trigger;

    auto frame = policy->update(observation, {}, observation.at);
    REQUIRE(frame.right);
    CHECK(
        frame.right.value_or(TriggerIntent{}).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    CHECK(
        frame.right.value_or(TriggerIntent{}).payload.strength == 0.30F * settings->trigger_scale);
    CHECK_FALSE(frame.left);

    ++observation.frame;
    observation.at += 10ms;
    observation.weapon.loaded_ammo = 0U;
    frame = policy->update(observation, {}, observation.at);
    REQUIRE(frame.right);
    CHECK(
        frame.right.value_or(TriggerIntent{}).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    CHECK(
        frame.right.value_or(TriggerIntent{}).payload.strength == 0.30F * settings->trigger_scale);
    CHECK_FALSE(frame.left);

    ++observation.frame;
    observation.at += 10ms;
    const std::array events{shot(observation, 14U, 1020)};
    frame = policy->update(observation, events, observation.at);
    REQUIRE(frame.right);
    CHECK(
        frame.right.value_or(TriggerIntent{}).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);

    ++observation.frame;
    observation.at += 100ms;
    frame = policy->update(observation, {}, observation.at);
    REQUIRE(frame.right);
    CHECK(
        frame.right.value_or(TriggerIntent{}).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
}

TEST_CASE("The last round keeps its body recoil and its shot trigger accent",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.weapon.loaded_ammo = 0U;
    observation.aiming = true;
    const std::array events{shot(observation, 12U, 1000)};

    const auto last_shot = policy->update(observation, events, observation.at);
    REQUIRE(last_shot.left);
    REQUIRE(last_shot.right);
    CHECK(last_shot.left.value_or(TriggerIntent{}).payload.mode ==
          INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    CHECK(last_shot.right.value_or(TriggerIntent{}).payload.mode ==
          INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(voice_for(last_shot, BodyEffect::shot_pistol).started_at == observation.at);

    ++observation.frame;
    observation.at += 40ms;
    const auto after_shot = policy->update(observation, {}, observation.at);
    REQUIRE(after_shot.left);
    CHECK(after_shot.left.value_or(TriggerIntent{}).payload.mode ==
          INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    CHECK_FALSE(after_shot.right);
    CHECK(voice_count(after_shot) == 1U);

    ++observation.frame;
    observation.at += 10ms;
    observation.aiming = false;
    observation.reloading = true;
    const auto reloading = policy->update(observation, {}, observation.at);
    CHECK_FALSE(reloading.right);

    ++observation.frame;
    observation.at += 10ms;
    observation.reloading = false;
    observation.weapon.loaded_ammo = 5U;
    const auto reloaded = policy->update(observation, {}, observation.at);
    REQUIRE(reloaded.right);
    CHECK(reloaded.right.value_or(TriggerIntent{}).payload.mode ==
          INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
}

TEST_CASE("fallout_gamebryo.policy.confirms_reload_completion_from_magazine_increase",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    static_cast<void>(policy->update(observation, {}, observation.at));

    ++observation.frame;
    observation.at = at(1010);
    observation.reloading = true;
    const auto started = policy->update(observation, {}, observation.at);
    CHECK(voice_for(started, BodyEffect::reload_start).started_at == observation.at);

    ++observation.frame;
    observation.at = at(1020);
    observation.reloading = false;
    observation.weapon.loaded_ammo = 6U;
    const auto finished = policy->update(observation, {}, observation.at);
    CHECK(voice_for(finished, BodyEffect::reload_finish).started_at == observation.at);
}

TEST_CASE("fallout_gamebryo.policy.accepts_incremental_reload_progress_observed_mid_reload",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    static_cast<void>(policy->update(observation, {}, observation.at));

    ++observation.frame;
    observation.at = at(1010);
    observation.reloading = true;
    const auto started = policy->update(observation, {}, observation.at);
    CHECK(has_voice(started, BodyEffect::reload_start));

    ++observation.frame;
    observation.at = at(1020);
    observation.weapon.loaded_ammo = 6U;
    const auto inserted_shell = policy->update(observation, {}, observation.at);
    CHECK_FALSE(has_voice(inserted_shell, BodyEffect::reload_finish));

    ++observation.frame;
    observation.at = at(1030);
    observation.reloading = false;
    const auto finished = policy->update(observation, {}, observation.at);
    CHECK(voice_for(finished, BodyEffect::reload_finish).started_at == observation.at);

    ++observation.frame;
    observation.at = at(1040);
    const auto repeated = policy->update(observation, {}, observation.at);
    CHECK(effect_voice_count(repeated, BodyEffect::reload_finish) == 1U);
    CHECK(voice_for(repeated, BodyEffect::reload_finish).started_at == at(1030));
}

TEST_CASE("fallout_gamebryo.policy.confirms_reload_between_known_counts_after_telemetry_returns",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.weapon.loaded_ammo.reset();
    static_cast<void>(policy->update(observation, {}, observation.at));

    ++observation.frame;
    observation.at = at(1010);
    observation.reloading = true;
    observation.weapon.loaded_ammo = 5U;
    const auto started = policy->update(observation, {}, observation.at);
    CHECK(has_voice(started, BodyEffect::reload_start));

    ++observation.frame;
    observation.at = at(1020);
    observation.reloading = false;
    observation.weapon.loaded_ammo = 6U;
    const auto finished = policy->update(observation, {}, observation.at);
    CHECK(voice_for(finished, BodyEffect::reload_finish).started_at == observation.at);
}

TEST_CASE("fallout_gamebryo.policy.does_not_confirm_cancelled_or_unmeasured_reloads",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    SECTION("an unchanged magazine") {
        const auto policy = std::make_unique<Policy>(*settings);
        auto observation = gameplay();
        static_cast<void>(policy->update(observation, {}, observation.at));

        ++observation.frame;
        observation.at = at(1010);
        observation.reloading = true;
        static_cast<void>(policy->update(observation, {}, observation.at));

        ++observation.frame;
        observation.at = at(1020);
        observation.reloading = false;
        const auto cancelled = policy->update(observation, {}, observation.at);
        CHECK_FALSE(has_voice(cancelled, BodyEffect::reload_finish));
    }

    SECTION("ammo is unknown when reloading begins") {
        const auto policy = std::make_unique<Policy>(*settings);
        auto observation = gameplay();
        observation.weapon.loaded_ammo.reset();
        static_cast<void>(policy->update(observation, {}, observation.at));

        ++observation.frame;
        observation.at = at(1010);
        observation.reloading = true;
        const auto started = policy->update(observation, {}, observation.at);
        CHECK(has_voice(started, BodyEffect::reload_start));

        ++observation.frame;
        observation.at = at(1020);
        observation.weapon.loaded_ammo = 6U;
        observation.reloading = false;
        const auto unmeasured = policy->update(observation, {}, observation.at);
        CHECK_FALSE(has_voice(unmeasured, BodyEffect::reload_finish));
    }
}

TEST_CASE("fallout_gamebryo.policy.resets_reload_evidence_across_observation_discontinuities",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto verify_reset = [&settings](auto disrupt, auto restore) {
        const auto policy = std::make_unique<Policy>(*settings);
        auto observation = gameplay();
        static_cast<void>(policy->update(observation, {}, observation.at));

        ++observation.frame;
        observation.at = at(1010);
        observation.reloading = true;
        static_cast<void>(policy->update(observation, {}, observation.at));

        ++observation.frame;
        observation.at = at(1020);
        disrupt(observation);
        static_cast<void>(policy->update(observation, {}, observation.at));

        restore(observation);
        ++observation.frame;
        observation.at = at(1030);
        observation.reloading = false;
        observation.weapon.loaded_ammo = 6U;
        const auto recovered = policy->update(observation, {}, observation.at);
        CHECK_FALSE(has_voice(recovered, BodyEffect::reload_finish));
    };

    SECTION("weapon changed") {
        verify_reset([](Observation& observation) { observation.weapon.id = 43U; },
            [](Observation&) {});
    }

    SECTION("save or player changed") {
        verify_reset([](Observation& observation) { observation.epoch = 2U; }, [](Observation&) {});
        verify_reset([](Observation& observation) { observation.player = 0x15U; },
            [](Observation&) {});
    }

    SECTION("focus lost") {
        verify_reset([](Observation& observation) { observation.focused = false; },
            [](Observation& observation) { observation.focused = true; });
    }

    SECTION("menu opened") {
        verify_reset(
            [](Observation& observation) {
                observation.mode = Mode::menu;
                observation.menu = Menu::other;
            },
            [](Observation& observation) {
                observation.mode = Mode::gameplay;
                observation.menu = Menu::none;
            });
    }

    SECTION("policy reset") {
        const auto policy = std::make_unique<Policy>(*settings);
        auto observation = gameplay();
        static_cast<void>(policy->update(observation, {}, observation.at));

        ++observation.frame;
        observation.at = at(1010);
        observation.reloading = true;
        static_cast<void>(policy->update(observation, {}, observation.at));

        (*policy).reset();
        ++observation.frame;
        observation.at = at(1020);
        observation.reloading = false;
        observation.weapon.loaded_ammo = 6U;
        const auto reset = policy->update(observation, {}, observation.at);
        CHECK_FALSE(has_voice(reset, BodyEffect::reload_finish));
    }

    SECTION("stale observation cleared the prior reload") {
        const auto policy = std::make_unique<Policy>(*settings);
        auto observation = gameplay();
        static_cast<void>(policy->update(observation, {}, observation.at));

        ++observation.frame;
        observation.at = at(1010);
        observation.reloading = true;
        static_cast<void>(policy->update(observation, {}, observation.at));

        const auto stale = policy->update(observation, {}, at(1200));
        CHECK(voice_count(stale) == 0U);

        ++observation.frame;
        observation.at = at(1210);
        observation.reloading = false;
        observation.weapon.loaded_ammo = 6U;
        const auto recovered = policy->update(observation, {}, observation.at);
        CHECK_FALSE(has_voice(recovered, BodyEffect::reload_finish));
    }
}
} // namespace fallout_gamebryo::tests
