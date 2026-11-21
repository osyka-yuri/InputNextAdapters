#include "effect_model.hpp"
#include "effect_policy.hpp"
#include "fixtures.hpp"
#include "input_next_effects.h"

#include <array>
#include <catch2/catch_test_macros.hpp>

namespace oblivion_adapter::test::effect_policy {

TEST_CASE("oblivion.effect_policy.bow.cancelling_draw_releases_trigger_and_texture",
    "[unit][oblivion][effects][bow]") {
    const auto policy = make_policy();
    auto sample = observation(100, 5);
    sample.weapon = Weapon::bow;
    sample.weapon_id = 0x9001;
    sample.bow = BowPhase::drawing;

    const auto draw = policy->update(sample, {}, at(100));
    REQUIRE(draw.right);
    CHECK(require_value(draw.right, "bow draw applies right trigger feedback").payload.mode ==
          INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    CHECK_FALSE(draw.left);
    REQUIRE(voice_count(draw) == 1);
    CHECK(first_voice(draw).held);

    sample.sampled_at = at(120);
    sample.frame = 6;
    sample.bow = BowPhase::none;
    const auto cancelled = policy->update(sample, {}, at(120));
    CHECK_FALSE(cancelled.right);
    CHECK(voice_count(cancelled) == 0);
}

TEST_CASE("oblivion.effect_policy.bow.onsets_sustain_and_confirmed_projectile",
    "[unit][oblivion][effects][bow]") {
    const auto policy = make_policy();
    auto sample = observation(100, 5);
    sample.weapon = Weapon::bow;
    sample.weapon_id = 0x222;
    sample.bow = BowPhase::drawing;
    auto draw = event(EffectEventKind::bow_draw, 100, 5, 51);
    draw.actor_id = sample.player_id;

    const auto onset = policy->update(sample, std::array{draw}, at(100));
    REQUIRE(onset.right);
    REQUIRE(voice_count(onset) == 1);
    CHECK_FALSE(first_voice(onset).held);

    sample.sampled_at = at(240);
    static_cast<void>(policy->update(sample, {}, at(240)));
    sample.sampled_at = at(275);
    sample.frame = 6;
    const auto sustain = policy->update(sample, {}, at(275));
    CHECK(sustain.right);
    REQUIRE(voice_count(sustain) == 1);
    CHECK(first_voice(sustain).held);

    sample.sampled_at = at(281);
    sample.frame = 7;
    sample.bow = BowPhase::attached;
    auto attached = event(EffectEventKind::bow_attach, 280, 7, 52);
    attached.actor_id = sample.player_id;
    const auto nocked = policy->update(sample, std::array{attached}, at(281));
    REQUIRE(nocked.right);
    const auto& nocked_trigger = require_value(nocked.right, "nocked bow retains trigger force");
    const auto& onset_trigger = require_value(onset.right, "bow draw begins with trigger force");
    CHECK(nocked_trigger.payload.strength > onset_trigger.payload.strength);
    REQUIRE(voice_count(nocked) == 1);
    CHECK(first_voice(nocked).wave.frequency == 54.0F);

    sample.sampled_at = at(295);
    sample.frame = 8;
    sample.bow = BowPhase::none;
    const auto cancelled = policy->update(sample, {}, at(295));
    CHECK_FALSE(cancelled.right);
    CHECK_FALSE(cancelled.rumble);
    CHECK(voice_count(cancelled) == 0);

    auto shot = event(EffectEventKind::bow_shot, 300, 9, 53);
    shot.actor_id = sample.player_id;
    shot.weapon = Weapon::bow;
    sample.sampled_at = at(300);
    sample.frame = 9;
    CHECK(voice_count(policy->update(sample, std::array{shot}, at(300))) == 0);

    shot.source_id = 0xA01;
    const auto fired = policy->update(sample, std::array{shot}, at(300));
    CHECK_FALSE(fired.right);
    REQUIRE(voice_count(fired) == 1);
    CHECK(first_voice(fired).wave.frequency == 52.0F);
}

TEST_CASE("oblivion.effect_policy.bow.arrow_impact_uses_its_own_profile",
    "[unit][oblivion][effects][bow]") {
    const auto policy = make_policy();
    auto sample = observation(300, 9);
    sample.weapon = Weapon::bow;
    auto hit = event(EffectEventKind::hit, 300, 9);
    hit.actor_id = sample.player_id;
    hit.target_id = 0x501;
    hit.source_id = 0xA01;
    hit.weapon = Weapon::bow;

    const auto impact = policy->update(sample, std::array{hit}, at(300));
    REQUIRE(voice_count(impact) == 1);
    CHECK(first_voice(impact).wave.frequency == 51.0F);
}

} // namespace oblivion_adapter::test::effect_policy
