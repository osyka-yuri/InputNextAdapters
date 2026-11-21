#include "effect_model.hpp"
#include "effect_policy.hpp"
#include "effect_settings.hpp"
#include "fixtures.hpp"
#include "model.hpp"

#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <limits>

namespace oblivion_adapter::test::effect_policy {

TEST_CASE("oblivion.effect_policy.player.heartbeat_hysteresis_and_cadence",
    "[unit][oblivion][effects][player]") {
    const auto policy = make_policy();
    auto sample = observation(100, 5);
    sample.health = calculate_health(19.0F, 100.0F);

    const auto first = policy->update(sample, {}, at(100));
    REQUIRE(voice_count(first) == 1);
    const auto& pulse = first_voice(first);
    CHECK(pulse.wave.frequency == 46.0F);
    CHECK(pulse.duration_ms == 183U);
    REQUIRE(first.rumble);
    CHECK(require_value(first.rumble, "heartbeat starts with a rumble pulse").ttl_ms == 38U);

    sample.health = calculate_health(23.0F, 100.0F);
    const auto beat_gap = policy->update(sample, {}, at(160));
    CHECK_FALSE(beat_gap.rumble);
    CHECK(voice_count(beat_gap) == 1);
    sample.sampled_at = at(240);
    const auto second_beat = policy->update(sample, {}, at(240));
    REQUIRE(second_beat.rumble);
    CHECK(require_value(second_beat.rumble, "heartbeat repeats with a rumble pulse").ttl_ms == 43U);

    sample.health = {};
    CHECK(voice_count(policy->update(sample, {}, at(245))) == 0);

    sample.health = calculate_health(19.0F, 100.0F);
    static_cast<void>(policy->update(sample, {}, at(250)));
    sample.sampled_at = at(1200);
    sample.frame = 6;
    const auto repeated = policy->update(sample, {}, at(1200));
    REQUIRE(voice_count(repeated) == 1);
    CHECK(first_voice(repeated).started_at == at(1200));

    sample.sampled_at = at(1220);
    sample.frame = 7;
    sample.health = calculate_health(25.0F, 100.0F);
    CHECK(voice_count(policy->update(sample, {}, at(1220))) == 0);
}

TEST_CASE("oblivion.effect_policy.player.magicka_and_fatigue_deplete_independently",
    "[unit][oblivion][effects][player]") {
    const auto policy = make_policy();
    auto sample = observation(200, 20);
    sample.magicka_ratio = 0.4F;
    sample.fatigue_ratio = 0.7F;
    static_cast<void>(policy->update(sample, {}, at(200)));

    sample.sampled_at = at(220);
    sample.frame = 21;
    sample.magicka_ratio = 0.0F;
    const auto depleted = policy->update(sample, {}, at(220));
    REQUIRE(voice_count(depleted) == 1);
    CHECK(std::abs(first_voice(depleted).wave.frequency - 38.0F) < 0.001F);

    sample.sampled_at = at(240);
    sample.frame = 22;
    const auto held_zero = policy->update(sample, {}, at(240));
    REQUIRE(voice_count(held_zero) == 1);
    CHECK(first_voice(held_zero).started_at == at(220));

    sample.sampled_at = at(260);
    sample.frame = 23;
    sample.fatigue_ratio = 0.05F;
    const auto fatigued = policy->update(sample, {}, at(260));
    REQUIRE(voice_count(fatigued) == 2);
    CHECK(first_voice(fatigued).wave.frequency == 31.0F);
}

TEST_CASE("oblivion.effect_policy.player.landing_uses_the_airborne_peak",
    "[unit][oblivion][effects][player]") {
    const auto policy = make_policy();
    auto sample = observation(100, 1);
    sample.ground = Ground::airborne;
    sample.position_z = 100.0F;
    CHECK(voice_count(policy->update(sample, {}, at(100))) == 0);

    sample.sampled_at = at(120);
    sample.frame = 2;
    sample.position_z = 180.0F;
    CHECK(voice_count(policy->update(sample, {}, at(120))) == 0);
    sample.sampled_at = at(140);
    sample.frame = 3;
    sample.position_z = 150.0F;
    CHECK(voice_count(policy->update(sample, {}, at(140))) == 0);

    sample.sampled_at = at(160);
    sample.frame = 4;
    sample.ground = Ground::grounded;
    sample.position_z = 110.0F;
    const auto landed = policy->update(sample, {}, at(160));
    REQUIRE(voice_count(landed) == 1);
    CHECK(std::abs(first_voice(landed).wave.frequency - 36.0F) < 0.001F);
}

TEST_CASE("oblivion.effect_policy.player.fall_damage_and_npc_strike",
    "[unit][oblivion][effects][player]") {
    const auto policy = make_policy();
    const auto sample = observation(300, 10);
    auto damage = event(EffectEventKind::damage, 300, 10, 91);
    damage.target_id = sample.player_id;
    damage.amount = 15.0F;
    auto fall = event(EffectEventKind::fall_impact, 300, 10, 92);
    fall.target_id = sample.player_id;
    fall.actor_id = sample.player_id;

    SECTION("Fall damage replaces the landing cue") {
        damage.actor_id = sample.player_id;
        const auto impact = policy->update(sample, std::array{damage, fall}, at(300));
        REQUIRE(voice_count(impact) == 1);
        CHECK(std::abs(first_voice(impact).wave.frequency - 42.0F) < 0.001F);
    }
    SECTION("An independent strike preserves the landing cue") {
        damage.actor_id = 0x202;
        CHECK(voice_count(policy->update(sample, std::array{damage, fall}, at(300))) == 2);
    }
}

TEST_CASE("oblivion.effect_policy.player.malformed_damage_preserves_a_valid_hit",
    "[unit][oblivion][effects][player]") {
    const auto policy = make_policy();
    const auto sample = observation(300, 10);
    auto damage = event(EffectEventKind::damage, 300, 10, 91);
    damage.actor_id = 0x202;
    damage.target_id = sample.player_id;
    damage.amount = std::numeric_limits<float>::quiet_NaN();
    auto hit = damage;
    hit.kind = EffectEventKind::hit;
    hit.sequence = 94;
    hit.amount = 15.0F;

    const auto impact = policy->update(sample, std::array{hit, damage}, at(300));
    REQUIRE(voice_count(impact) == 1);
    CHECK(first_voice(impact).wave.frequency == 42.0F);
}

TEST_CASE("oblivion.effect_policy.player.mount_and_dismount_select_distinct_profiles",
    "[unit][oblivion][effects][player]") {
    EffectSettings settings{};
    mark_profile(settings, EffectId::mount);
    mark_profile(settings, EffectId::dismount);
    const auto policy = make_policy(settings);
    auto sample = observation(100, 1);
    static_cast<void>(policy->update(sample, {}, at(100)));

    sample.mounted = true;
    sample.sampled_at = at(120);
    sample.frame = 2;
    check_profile(policy->update(sample, {}, at(120)), EffectId::mount);
    sample.mounted = false;
    sample.sampled_at = at(320);
    sample.frame = 3;
    check_profile(policy->update(sample, {}, at(320)), EffectId::dismount);
}

TEST_CASE("oblivion.effect_policy.player.opt_in_gait_plays_short_footfalls",
    "[unit][oblivion][effects][player]") {
    struct Example {
        Gait gait;
        EffectId effect;
    };

    constexpr std::array examples{
        Example{.gait = Gait::walk, .effect = EffectId::walk},
        Example{.gait = Gait::run, .effect = EffectId::run},
        Example{.gait = Gait::sneak, .effect = EffectId::sneak},
        Example{.gait = Gait::mounted_walk, .effect = EffectId::mounted_walk},
        Example{.gait = Gait::mounted_run, .effect = EffectId::mounted_run},
    };
    for (const auto& example : examples) {
        DYNAMIC_SECTION(profile_name(example.effect)) {
            auto sample = observation(100, 1);
            sample.gait = example.gait;
            const bool mounted =
                example.gait == Gait::mounted_walk || example.gait == Gait::mounted_run;
            sample.mounted = mounted;
            HorizontalPosition location{.mover_id = mounted ? 0x210U : sample.player_id};

            EffectSettings settings{};
            mark_profile(settings, example.effect).enabled = true;
            const auto moving = make_policy(settings);
            const auto defaults = make_policy();
            bool footfall = false;
            for (int milliseconds = 100; milliseconds <= 700; milliseconds += 20) {
                sample.sampled_at = at(milliseconds);
                sample.horizontal_position = location;
                const auto active = moving->update(sample, {}, sample.sampled_at);
                CHECK(voice_count(defaults->update(sample, {}, sample.sampled_at)) == 0);
                if (voice_count(active) != 0) {
                    check_profile(active, example.effect);
                    const auto& voice = first_voice(active);
                    CHECK_FALSE(voice.held);
                    CHECK(voice.started_at == sample.sampled_at);
                    footfall = true;
                    break;
                }
                ++sample.frame;
                location.x += 12.0F;
            }
            REQUIRE(footfall);

            // Standing against a wall can leave the movement flag set.
            for (int index = 0; index < 15; ++index) {
                ++sample.frame;
                sample.sampled_at += std::chrono::milliseconds{20};
                const auto still = moving->update(sample, {}, sample.sampled_at);
                if (index >= 2) {
                    CHECK(voice_count(still) == 0);
                }
            }
        }
    }
}

TEST_CASE("oblivion.effect_policy.player.health_light_uses_original_deadline",
    "[unit][oblivion][effects][player]") {
    const auto policy = make_policy();
    auto sample = observation(100, 5);
    sample.in_combat = false;
    CHECK_FALSE(policy->update(sample, {}, at(100)).leds);

    sample.sampled_at = at(120);
    sample.health = calculate_health(72.0F, 100.0F);
    REQUIRE(policy->update(sample, {}, at(120)).leds);
    for (int milliseconds = 220; milliseconds < 4120; milliseconds += 100) {
        CAPTURE(milliseconds);
        sample.sampled_at = at(milliseconds);
        CHECK(policy->update(sample, {}, at(milliseconds)).leds);
    }
    sample.sampled_at = at(4120);
    CHECK_FALSE(policy->update(sample, {}, at(4120)).leds);
}

TEST_CASE("oblivion.effect_policy.player.stealth_light_follows_detection_while_stationary",
    "[unit][oblivion][effects][player]") {
    EffectSettings settings{};
    auto sample = observation(100, 1);
    sample.sneak_detected = false;

    SECTION("Detection changes the light and unavailable detection clears it") {
        const auto policy = make_policy(settings);
        const auto hidden = policy->update(sample, {}, at(100));
        CHECK(require_value(hidden.leds, "hidden light").payload.lightbar_rgb == 0x0000FFU);

        sample.sneak_detected = true;
        sample.sampled_at = at(120);
        sample.frame = 2;
        const auto detected = policy->update(sample, {}, at(120));
        CHECK(require_value(detected.leds, "detected light").payload.lightbar_rgb == 0xFF00FFU);

        sample.sneak_detected.reset();
        sample.sampled_at = at(140);
        sample.frame = 3;
        CHECK_FALSE(policy->update(sample, {}, at(140)).leds);
    }
    SECTION("Stealth can be disabled independently") {
        settings.led.stealth_enabled = false;
        const auto policy = make_policy(settings);
        CHECK_FALSE(policy->update(sample, {}, at(100)).leds);
        sample.sampled_at = at(120);
        sample.frame = 2;
        sample.health = calculate_health(68.0F, 100.0F);
        CHECK(policy->update(sample, {}, at(120)).leds);
    }
}

} // namespace oblivion_adapter::test::effect_policy
