#include "effect_model.hpp"
#include "effect_policy.hpp"
#include "effect_settings.hpp"
#include "fixtures.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

namespace oblivion_adapter::test::effect_policy {
namespace {

EffectEvent block_impact(const EffectObservation& sample) noexcept {
    return {.kind = EffectEventKind::block_impact,
        .at = sample.sampled_at,
        .epoch = sample.epoch,
        .frame = sample.frame,
        .mode = sample.mode,
        .actor_id = sample.player_id,
        .target_id = sample.player_id};
}

} // namespace

TEST_CASE("oblivion.effect_policy.melee.sampled_swing_and_power_attack",
    "[unit][oblivion][effects][melee]") {
    auto sample = observation(120, 11);
    sample.weapon = Weapon::blade_one_hand;
    sample.weapon_id = 0x8001;
    sample.melee_attack = true;
    auto attack = event(EffectEventKind::attack, 100, 10);
    attack.actor_id = sample.player_id;
    attack.source_id = sample.weapon_id;
    attack.weapon = sample.weapon;

    const auto normal = make_policy();
    const auto swing = normal->update(sample, std::array{attack}, at(120));
    REQUIRE(voice_count(swing) == 1);
    const auto& normal_voice = first_voice(swing);
    CHECK(std::abs(normal_voice.wave.frequency - 112.0F) < 0.001F);
    CHECK(normal_voice.started_at == at(100));

    sample.power_attack = true;
    const auto power = make_policy();
    const auto power_swing = power->update(sample, std::array{attack}, at(120));
    const auto& power_voice = first_voice(power_swing);
    CHECK(power_voice.wave.level > normal_voice.wave.level);
    CHECK(power_voice.wave.level <= 0.75F);
}

TEST_CASE("oblivion.effect_policy.melee.paired_hit_keeps_one_original_cue",
    "[unit][oblivion][effects][melee]") {
    const auto policy = make_policy();
    auto sample = observation(200, 20);
    sample.weapon = Weapon::blade_one_hand;
    sample.weapon_id = 0x8001;
    auto hit = event(EffectEventKind::hit, 200, 20, 201);
    hit.actor_id = sample.player_id;
    hit.target_id = 0x501;
    hit.source_id = sample.weapon_id;
    hit.weapon = sample.weapon;
    auto hit_with = hit;
    hit_with.kind = EffectEventKind::hit_with;
    hit_with.actor_id = 0;
    hit_with.sequence = 202;
    const std::array contact{hit, hit_with};

    const auto impact = policy->update(sample, contact, at(200));
    REQUIRE(voice_count(impact) == 1);
    CHECK(std::abs(first_voice(impact).wave.frequency - 78.0F) < 0.001F);
    REQUIRE(impact.rumble);

    sample.sampled_at = at(280);
    const auto duplicate = policy->update(sample, contact, at(280));
    REQUIRE(voice_count(duplicate) == 1);
    CHECK(first_voice(duplicate).started_at == at(200));
    REQUIRE(duplicate.rumble);
    const auto& duplicate_rumble =
        require_value(duplicate.rumble, "duplicate contact retains rumble");
    const auto& impact_rumble = require_value(impact.rumble, "initial contact produces rumble");
    CHECK(duplicate_rumble.ttl_ms < impact_rumble.ttl_ms);
}

TEST_CASE("oblivion.effect_policy.melee.unarmed_impact_without_a_weapon_form",
    "[unit][oblivion][effects][melee]") {
    const auto policy = make_policy();
    auto sample = observation(320, 30);
    sample.weapon = Weapon::unarmed;
    sample.melee_attack = true;
    auto hit = event(EffectEventKind::hit, 320, 30);
    hit.actor_id = sample.player_id;
    hit.target_id = 0x501;
    hit.weapon = Weapon::unarmed;
    const auto impact = policy->update(sample, std::array{hit}, at(320));
    REQUIRE(voice_count(impact) == 1);
    CHECK(first_voice(impact).wave.frequency == 66.0F);
}

TEST_CASE("oblivion.effect_policy.melee.confirmed_unarmed_hit_survives_a_newer_sample",
    "[unit][oblivion][effects][melee]") {
    const auto policy = make_policy();
    auto sample = observation(340, 31);
    sample.weapon = Weapon::blade_one_hand;
    auto hit = event(EffectEventKind::hit, 320, 30);
    hit.actor_id = sample.player_id;
    hit.target_id = 0x501;
    hit.weapon = Weapon::unarmed;
    hit.melee_confirmed = true;

    const auto impact = policy->update(sample, std::array{hit}, at(340));
    REQUIRE(voice_count(impact) == 1);
    CHECK(first_voice(impact).wave.frequency == 66.0F);
    CHECK(first_voice(impact).started_at == at(320));
}

TEST_CASE("oblivion.effect_policy.melee.confirmed_attack_survives_skipped_frames",
    "[unit][oblivion][effects][melee]") {
    const auto policy = make_policy();
    const auto sample = observation(360, 35);
    auto attack = event(EffectEventKind::attack, 340, 31);
    attack.actor_id = sample.player_id;
    attack.source_id = 0x8001;
    attack.weapon = Weapon::blade_one_hand;
    attack.melee_confirmed = true;
    attack.power_attack = true;

    const auto swing = policy->update(sample, std::array{attack}, at(360));
    REQUIRE(voice_count(swing) == 1);
    CHECK(first_voice(swing).started_at == at(340));
}

TEST_CASE("oblivion.effect_policy.melee.unpaired_player_hit_and_npc_hit",
    "[unit][oblivion][effects][melee]") {
    const auto policy = make_policy();
    const auto sample = observation(100, 5);
    auto hit = event(EffectEventKind::hit, 100, 5);
    hit.target_id = 0x701;

    SECTION("Player hit without a matching weapon event uses the neutral profile") {
        hit.actor_id = sample.player_id;
        const auto neutral = policy->update(sample, std::array{hit}, at(100));
        REQUIRE(voice_count(neutral) == 1);
        CHECK(std::abs(first_voice(neutral).wave.frequency - 58.0F) < 0.001F);
    }
    SECTION("NPC hitting another NPC is silent") {
        hit.actor_id = 0x200;
        const auto ignored = policy->update(sample, std::array{hit}, at(100));
        CHECK(voice_count(ignored) == 0);
        CHECK_FALSE(ignored.rumble);
    }
}

TEST_CASE("oblivion.effect_policy.melee.swing_and_hit_profiles",
    "[unit][oblivion][effects][melee]") {
    struct Example {
        Weapon weapon;
        EffectId swing;
        EffectId hit;
    };

    constexpr std::array examples{
        Example{.weapon = Weapon::unarmed,
            .swing = EffectId::unarmed_swing,
            .hit = EffectId::unarmed_hit},
        Example{.weapon = Weapon::blade_one_hand,
            .swing = EffectId::blade_one_hand_swing,
            .hit = EffectId::blade_one_hand_hit},
        Example{.weapon = Weapon::blade_two_hand,
            .swing = EffectId::blade_two_hand_swing,
            .hit = EffectId::blade_two_hand_hit},
        Example{.weapon = Weapon::blunt_one_hand,
            .swing = EffectId::blunt_one_hand_swing,
            .hit = EffectId::blunt_one_hand_hit},
        Example{.weapon = Weapon::blunt_two_hand,
            .swing = EffectId::blunt_two_hand_swing,
            .hit = EffectId::blunt_two_hand_hit},
    };

    for (const auto& example : examples) {
        DYNAMIC_SECTION(profile_name(example.swing) << " and " << profile_name(example.hit)) {
            EffectSettings settings{};
            mark_profile(settings, example.swing);
            mark_profile(settings, example.hit);
            auto sample = observation(100);
            sample.weapon = example.weapon;
            sample.weapon_id = example.weapon == Weapon::unarmed ? 0U : 0x90U;
            sample.melee_attack = true;
            auto attack = event(EffectEventKind::attack, 100, sample.frame);
            attack.actor_id = sample.player_id;
            attack.source_id = sample.weapon_id;
            attack.weapon = sample.weapon;

            const auto swing_policy = make_policy(settings);
            check_profile(swing_policy->update(sample, std::array{attack}, at(100)), example.swing);

            auto contact = attack;
            contact.kind = EffectEventKind::hit;
            contact.target_id = 0x91;
            auto with = contact;
            with.kind = EffectEventKind::hit_with;
            with.actor_id = 0;
            with.sequence = 2;
            const auto hit_policy = make_policy(settings);
            check_profile(hit_policy->update(sample, std::array{contact, with}, at(100)),
                example.hit);
        }
    }
}

TEST_CASE("oblivion.effect_policy.block.shield_resistance_and_contact_frame",
    "[unit][oblivion][effects][block]") {
    const auto policy = make_policy();
    auto sample = observation(100, 5);
    sample.blocking = true;
    const auto weapon_block = policy->update(sample, {}, at(100));
    REQUIRE(weapon_block.left);

    sample.sampled_at = at(120);
    sample.frame = 6;
    sample.shield = true;
    const auto block_event = block_impact(sample);
    auto damage = event(EffectEventKind::damage, 119, 6, 81);
    damage.actor_id = 0x202;
    damage.target_id = sample.player_id;
    damage.amount = 15.0F;
    auto hit = damage;
    hit.kind = EffectEventKind::hit;
    hit.sequence = 82;

    const auto shield_block = policy->update(sample, std::array{hit, damage, block_event}, at(120));
    REQUIRE(shield_block.left);
    const auto& shield_feedback =
        require_value(shield_block.left, "shield block retains resistance");
    const auto& weapon_feedback =
        require_value(weapon_block.left, "weapon block applies resistance");
    CHECK(shield_feedback.payload.strength > weapon_feedback.payload.strength);
    REQUIRE(voice_count(shield_block) == 1);
    CHECK(first_voice(shield_block).wave.frequency == 52.0F);

    damage.frame = 5;
    const auto prior_hit = make_policy();
    // The previous frame's strike and current block contribute separate cues.
    CHECK(voice_count(prior_hit->update(sample, std::array{damage, block_event}, at(120))) == 2);
}

TEST_CASE("oblivion.effect_policy.block.unique_contact_uses_one_original_impact",
    "[unit][oblivion][effects][block]") {
    EffectSettings settings{};
    mark_profile(settings, EffectId::block_hit);
    mark_profile(settings, EffectId::incoming_damage);
    auto sample = observation(120, 6);
    const auto block_event = block_impact(observation(119, 5));

    auto damage = event(EffectEventKind::damage, 119, 5, 81);
    damage.actor_id = 0x202;
    damage.target_id = sample.player_id;
    damage.amount = 15.0F;
    auto hit = damage;
    hit.kind = EffectEventKind::hit;
    hit.sequence = 82;

    const auto blocked = make_policy(settings);
    const auto block = blocked->update(sample, std::array{hit, damage, block_event}, at(120));
    REQUIRE(voice_count(block) == 1);
    CHECK(first_voice(block).wave.frequency == profile_frequency(EffectId::block_hit));
    CHECK(first_voice(block).started_at == at(119));

    sample.sampled_at = at(121);
    const auto repeated = blocked->update(sample, std::array{hit, damage, block_event}, at(121));
    REQUIRE(voice_count(repeated) == 1);
    CHECK(first_voice(repeated).started_at == at(119));

    const auto ordinary = make_policy(settings);
    const auto damage_only = ordinary->update(sample, std::array{damage}, at(121));
    REQUIRE(voice_count(damage_only) == 1);
    CHECK(first_voice(damage_only).wave.frequency == profile_frequency(EffectId::incoming_damage));
}

TEST_CASE("oblivion.effect_policy.block.ambiguous_contacts_keep_damage_and_one_animation_cue",
    "[unit][oblivion][effects][block]") {
    EffectSettings settings{};
    mark_profile(settings, EffectId::block_hit);
    mark_profile(settings, EffectId::incoming_damage);
    const auto policy = make_policy(settings);
    auto sample = observation(140, 7);
    const auto block_event = block_impact(observation(120, 5));

    auto first = event(EffectEventKind::damage, 118, 5, 1);
    first.actor_id = 0x202;
    first.target_id = sample.player_id;
    first.source_id = 0x901;
    first.amount = 12.0F;
    auto second = first;
    second.at = at(119);
    second.sequence = 2;
    SECTION("separate attackers") {
        second.actor_id = 0x203;
    }
    SECTION("one attacker with separate sources") {
        second.source_id = 0x902;
    }
    const auto count_profile = [](const OutputFrame& frame, EffectId effect) {
        return std::ranges::count_if(frame.body.voices, [&](const auto& voice) {
            return voice && voice->wave.frequency == profile_frequency(effect);
        });
    };
    const auto cues = policy->update(sample, std::array{first, second, block_event}, at(140));
    CHECK(count_profile(cues, EffectId::block_hit) == 1);
    CHECK(count_profile(cues, EffectId::incoming_damage) == 2);

    sample.sampled_at = at(141);
    const auto repeated = policy->update(sample, std::array{block_event}, at(141));
    CHECK(count_profile(repeated, EffectId::block_hit) == 1);
    sample.sampled_at = at(500);
    sample.frame = 8;
    CHECK(voice_count(policy->update(sample, std::array{block_event}, at(500))) == 0);
}

} // namespace oblivion_adapter::test::effect_policy
