#include "effect_model.hpp"
#include "effect_policy.hpp"
#include "effect_settings.hpp"
#include "fixtures.hpp"
#include "model.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>

namespace oblivion_adapter::test::effect_policy {

TEST_CASE("oblivion.effect_policy.admission.event_age_preserves_original_cue_time",
    "[unit][oblivion][effects][actions]") {
    const auto policy = make_policy();
    auto sample = observation(151, 20);
    auto damage = event(EffectEventKind::damage, 0, 20);
    damage.target_id = sample.player_id;
    damage.actor_id = 0x201;
    damage.amount = 10.0F;

    const auto expired = policy->update(sample, std::array{damage}, at(151));
    CHECK(voice_count(expired) == 0);
    CHECK_FALSE(expired.rumble);

    sample = observation(350, 21);
    damage.at = at(200);
    damage.sequence = 2;
    const auto delayed = policy->update(sample, std::array{damage}, at(350));
    REQUIRE(voice_count(delayed) == 1);
    CHECK(first_voice(delayed).started_at == at(200));
}

TEST_CASE("oblivion.effect_policy.admission.stale_state_clears_active_output",
    "[unit][oblivion][effects][actions]") {
    const auto policy = make_policy();
    auto sample = observation(100, 5);
    sample.weapon = Weapon::bow;
    sample.weapon_id = 0x9001;
    sample.bow = BowPhase::attached;
    static_cast<void>(policy->update(sample, {}, at(100)));

    sample.sampled_at = at(120);
    sample.frame = 6;
    sample.health = calculate_health(72.0F, 100.0F);
    const auto active = policy->update(sample, {}, at(120));
    REQUIRE(active.right);
    REQUIRE(active.leds);
    REQUIRE(active.rumble);
    REQUIRE(voice_count(active) == 1);
    CHECK(first_voice(active).held);

    const auto stale = policy->update(sample, {}, at(271));
    CHECK(voice_count(stale) == 0);
    CHECK_FALSE(stale.rumble);
    CHECK_FALSE(stale.right);
    CHECK_FALSE(stale.leds);

    const auto resumed = policy->update(observation(272, 7), {}, at(272));
    CHECK(voice_count(resumed) == 0);
    CHECK_FALSE(resumed.right);
    CHECK_FALSE(resumed.leds);
}

TEST_CASE("oblivion.effect_policy.menu.inventory_replaces_active_gameplay_feedback",
    "[unit][oblivion][effects][actions][menu]") {
    const auto policy = make_policy();
    auto sample = observation(180, 9);
    sample.weapon = Weapon::bow;
    sample.weapon_id = 0x9001;
    sample.bow = BowPhase::attached;
    auto hit = event(EffectEventKind::hit, 180, 9);
    hit.actor_id = sample.player_id;
    hit.target_id = 0x44;

    const auto gameplay = policy->update(sample, std::array{hit}, at(180));
    REQUIRE(gameplay.right);
    REQUIRE(gameplay.rumble);
    REQUIRE(voice_count(gameplay) == 2);

    sample.mode = EffectMode::inventory;
    sample.sampled_at = at(200);
    sample.frame = 10;
    hit.at = at(200);
    hit.frame = 10;
    hit.sequence = 2;
    const auto menu = policy->update(sample, std::array{hit}, at(200));
    CHECK(voice_count(menu) == 0);
    CHECK_FALSE(menu.rumble);
    CHECK_FALSE(menu.left);
    CHECK_FALSE(menu.right);

    auto equip = event(EffectEventKind::equip, 220, 11, 3);
    equip.mode = EffectMode::inventory;
    equip.actor_id = sample.player_id;
    equip.source_id = 0xA001;
    sample.sampled_at = at(220);
    sample.frame = 11;

    SECTION("An inventory action produces a short body cue") {
        const auto confirmed = policy->update(sample, std::array{equip}, at(220));
        REQUIRE(voice_count(confirmed) == 1);
        CHECK_FALSE(first_voice(confirmed).held);
        CHECK_FALSE(confirmed.left);
        CHECK_FALSE(confirmed.right);
    }
    SECTION("An action from the just-closed menu remains admissible") {
        sample.mode = EffectMode::gameplay;
        sample.bow = BowPhase::none;
        const auto closed = policy->update(sample, std::array{equip}, at(220));
        REQUIRE(voice_count(closed) == 1);
        CHECK_FALSE(first_voice(closed).held);
    }
}

TEST_CASE("oblivion.effect_policy.actions.actor_filter_and_profile_selection",
    "[unit][oblivion][effects][actions]") {
    struct Example {
        EffectEventKind kind{};
        EffectId effect{};
        EffectMode mode{EffectMode::gameplay};
        bool opt_in{};
    };

    using Kind = EffectEventKind;
    using Id = EffectId;
    using Mode = EffectMode;
    constexpr std::array examples{
        Example{.kind = Kind::recoil, .effect = Id::recoil},
        Example{.kind = Kind::stagger, .effect = Id::stagger},
        Example{.kind = Kind::dodge, .effect = Id::dodge},
        Example{.kind = Kind::knockout, .effect = Id::knockout},
        Example{.kind = Kind::water_dive, .effect = Id::water_dive},
        Example{.kind = Kind::water_surface, .effect = Id::water_surface},
        Example{.kind = Kind::drink_potion, .effect = Id::drink_potion, .mode = Mode::inventory},
        Example{.kind = Kind::eat_ingredient, .effect = Id::eat_ingredient, .mode = Mode::magic},
        Example{.kind = Kind::equip, .effect = Id::equip, .mode = Mode::inventory},
        Example{.kind = Kind::unequip, .effect = Id::unequip, .mode = Mode::inventory},
        Example{.kind = Kind::drop_item, .effect = Id::drop_item, .mode = Mode::inventory},
        Example{.kind = Kind::create_potion, .effect = Id::create_potion, .mode = Mode::alchemy},
        Example{.kind = Kind::enchant, .effect = Id::enchant, .mode = Mode::enchantment},
        Example{.kind = Kind::create_spell, .effect = Id::create_spell, .mode = Mode::spellmaking},
        Example{.kind = Kind::lock_opened, .effect = Id::lock_opened, .mode = Mode::lockpick},
        Example{.kind = Kind::pick_broken, .effect = Id::pick_broken, .mode = Mode::lockpick},
        Example{.kind = Kind::pickpocketed, .effect = Id::pickpocketed, .mode = Mode::container},
        Example{.kind = Kind::trained,
            .effect = Id::trained,
            .mode = Mode::training,
            .opt_in = true},
        Example{.kind = Kind::gate_closed, .effect = Id::gate_closed, .opt_in = true},
        Example{.kind = Kind::vampire_feed, .effect = Id::vampire_feed},
        Example{.kind = Kind::soul_trap, .effect = Id::soul_trap},
        Example{.kind = Kind::skill_up,
            .effect = Id::skill_up,
            .mode = Mode::gameplay,
            .opt_in = true},
        Example{.kind = Kind::quest_complete,
            .effect = Id::quest_complete,
            .mode = Mode::gameplay,
            .opt_in = true},
        Example{.kind = Kind::discovered,
            .effect = Id::discovered,
            .mode = Mode::gameplay,
            .opt_in = true},
    };

    for (const auto& example : examples) {
        DYNAMIC_SECTION(profile_name(example.effect)) {
            auto sample = observation(100, 5);
            sample.mode = example.mode;
            auto action = event(example.kind, 100, 5);
            action.actor_id = sample.player_id;
            action.mode = example.mode;
            const std::array events{action};

            if (example.opt_in) {
                const auto defaults = make_policy();
                CHECK(voice_count(defaults->update(sample, events, at(100))) == 0);
            }

            EffectSettings settings{};
            mark_profile(settings, example.effect).enabled = true;
            const auto player = make_policy(settings);
            check_profile(player->update(sample, events, at(100)), example.effect);

            const auto npc = make_policy(settings);
            action.actor_id = 0x2000;
            CHECK(voice_count(npc->update(sample, std::array{action}, at(100))) == 0);

            if (example.mode != Mode::gameplay) {
                settings.menu_effects_enabled = false;
                const auto muted = make_policy(settings);
                CHECK(voice_count(muted->update(sample, events, at(100))) == 0);
            }
        }
    }
}

} // namespace oblivion_adapter::test::effect_policy
