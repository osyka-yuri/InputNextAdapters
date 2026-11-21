#include "effect_model.hpp"
#include "effect_policy.hpp"
#include "effect_settings.hpp"
#include "fixtures.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

namespace oblivion_adapter::test::effect_policy {

TEST_CASE("oblivion.effect_policy.magic.mixed_cast_uses_neutral_profile",
    "[unit][oblivion][effects][magic]") {
    const auto policy = make_policy();
    const auto sample = observation(100, 5);
    auto mixed = event(EffectEventKind::cast, 100, 5);
    mixed.actor_id = sample.player_id;
    mixed.source_id = 0xA1;
    mixed.magic = MagicKind::generic;

    const auto neutral = policy->update(sample, std::array{mixed}, at(100));
    REQUIRE(voice_count(neutral) == 1);
    CHECK(std::abs(first_voice(neutral).wave.frequency - 58.0F) < 0.001F);
}

TEST_CASE("oblivion.effect_policy.magic.applied_forms_distinguish_shock_and_unknown",
    "[unit][oblivion][effects][magic]") {
    const auto sample = observation(120, 6);
    auto applied = event(EffectEventKind::magic_applied, 120, 6);
    applied.actor_id = 0xC1;
    applied.target_id = sample.player_id;
    applied.source_id = 0xC1;
    const auto policy = make_policy();

    SECTION("An applied shock form retains its element") {
        applied.magic = MagicKind::shock;
        const auto shock = policy->update(sample, std::array{applied}, at(120));
        REQUIRE(voice_count(shock) == 1);
        CHECK(std::abs(first_voice(shock).wave.frequency - 102.0F) < 0.001F);
    }
    SECTION("An unclassified applied form uses the neutral application profile") {
        const auto neutral = policy->update(sample, std::array{applied}, at(120));
        REQUIRE(voice_count(neutral) == 1);
        CHECK(first_voice(neutral).wave.frequency == 64.0F);
    }
}

TEST_CASE("oblivion.effect_policy.magic.distinct_frames_and_repeated_applications",
    "[unit][oblivion][effects][magic]") {
    const auto policy = make_policy();
    const auto sample = observation(160, 8);
    auto frost = event(EffectEventKind::magic_applied, 120, 6, 104);
    frost.actor_id = 0xC1;
    frost.target_id = sample.player_id;
    frost.source_id = 0xC1;
    frost.magic = MagicKind::frost;
    auto repeated_frost = frost;
    repeated_frost.at = at(121);
    repeated_frost.sequence = 105;
    auto fire = event(EffectEventKind::cast, 150, 7, 106);
    fire.actor_id = sample.player_id;
    fire.source_id = 0xD1;
    fire.magic = MagicKind::fire;
    const std::array separate_frames{frost, repeated_frost, fire};

    CHECK(voice_count(policy->update(sample, separate_frames, at(160))) == 2);
    CHECK(voice_count(policy->update(sample, separate_frames, at(161))) == 2);
}

TEST_CASE("oblivion.effect_policy.magic.elemental_contact_correlates_by_source",
    "[unit][oblivion][effects][magic]") {
    const auto sample = observation(100, 5);
    auto damage = event(EffectEventKind::damage, 100, 5, 1);
    damage.actor_id = 0x505;
    damage.target_id = sample.player_id;
    damage.amount = 15.0F;
    auto spell = event(EffectEventKind::magic_applied, 100, 5, 2);
    spell.actor_id = damage.actor_id;
    spell.target_id = sample.player_id;
    spell.source_id = 0x404;
    spell.magic = MagicKind::fire;
    EffectSettings settings{};

    SECTION("Matching application colors the impact without duplicating it") {
        const auto policy = make_policy(settings);
        const auto elemental_hit = policy->update(sample, std::array{damage, spell}, at(100));
        REQUIRE(voice_count(elemental_hit) == 1);
        const auto& voice = first_voice(elemental_hit);
        CHECK(voice.wave.frequency == 35.0F);
        CHECK(voice.priority >= 110);
    }
    SECTION("Disabled fire preserves incoming damage") {
        settings.profiles[profile_index(EffectId::fire)].enabled = false;
        const auto policy = make_policy(settings);
        const auto impact = policy->update(sample, std::array{damage, spell}, at(100));
        REQUIRE(voice_count(impact) == 1);
        CHECK(first_voice(impact).wave.frequency == 42.0F);
    }
    SECTION("Muting both fire routes preserves incoming damage") {
        auto& profile = settings.profiles[profile_index(EffectId::fire)];
        profile.hd_scale = 0.0F;
        profile.classic_scale = 0.0F;
        const auto policy = make_policy(settings);
        const auto impact = policy->update(sample, std::array{damage, spell}, at(100));
        REQUIRE(voice_count(impact) == 1);
        CHECK(first_voice(impact).wave.frequency == 42.0F);
    }
    SECTION("Different sources produce separate cues") {
        damage.source_id = 0x406;
        const auto policy = make_policy(settings);
        CHECK(voice_count(policy->update(sample, std::array{damage, spell}, at(100))) == 2);
    }
}

TEST_CASE("oblivion.effect_policy.magic.potion_application_is_deduplicated",
    "[unit][oblivion][effects][magic]") {
    const auto policy = make_policy();
    const auto sample = observation(100, 5);
    auto spell = event(EffectEventKind::magic_applied, 100, 5, 2);
    spell.target_id = sample.player_id;
    spell.source_id = 0x404;
    spell.magic = MagicKind::healing;
    auto potion = event(EffectEventKind::drink_potion, 100, 5, 3);
    potion.actor_id = sample.player_id;
    potion.source_id = spell.source_id;

    const auto consumed = policy->update(sample, std::array{spell, potion}, at(100));
    REQUIRE(voice_count(consumed) == 1);
    CHECK(first_voice(consumed).wave.frequency == 66.0F);
}

TEST_CASE("oblivion.effect_policy.magic.animation_and_successful_cast_profiles",
    "[unit][oblivion][effects][magic]") {
    struct Example {
        MagicKind kind;
        EffectId effect;
    };

    constexpr std::array examples{
        Example{.kind = MagicKind::fire, .effect = EffectId::fire},
        Example{.kind = MagicKind::frost, .effect = EffectId::frost},
        Example{.kind = MagicKind::shock, .effect = EffectId::shock},
        Example{.kind = MagicKind::healing, .effect = EffectId::healing},
        Example{.kind = MagicKind::shield, .effect = EffectId::shield},
        Example{.kind = MagicKind::generic, .effect = EffectId::generic_magic},
    };
    for (const auto& example : examples) {
        DYNAMIC_SECTION(profile_name(example.effect)) {
            EffectSettings settings{};
            mark_profile(settings, example.effect);
            const auto policy = make_policy(settings);
            auto sample = observation(100, 5);
            sample.casting = true;
            const auto animation = policy->update(sample, {}, at(100));
            REQUIRE(voice_count(animation) == 1);
            CHECK(first_voice(animation).held);

            sample.casting = false;
            sample.sampled_at = at(120);
            sample.frame = 6;
            CHECK(voice_count(policy->update(sample, {}, at(120))) == 0);

            auto cast = event(EffectEventKind::cast, 120, 6);
            cast.actor_id = sample.player_id;
            cast.source_id = 0x500;
            cast.magic = example.kind;
            check_profile(policy->update(sample, std::array{cast}, at(120)), example.effect);
        }
    }
}

} // namespace oblivion_adapter::test::effect_policy
