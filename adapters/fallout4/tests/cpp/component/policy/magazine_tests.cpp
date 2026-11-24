#include "effect_recipes.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fallout4_adapter/detail/magazine_cue_tracker.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>
#include <input_next_effects.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>

namespace fallout4_adapter::policy_tests {
TEST_CASE("fallout4.policy.last_round_keeps_recoil_before_releasing_attack_resistance",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(1000);
    observation.weapon.family = WeaponFamily::pistol;
    observation.weapon.loaded_rounds = 1U;
    observation.aiming = true;

    const auto loaded = update_on_heap(*policy, observation, {}, at(1000));
    REQUIRE(loaded->right.has_value());
    CHECK(checked_optional(loaded->right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);

    observation.sampled_at = at(1020);
    observation.weapon.loaded_rounds = 0U;
    const std::array shot{event(EventKind::shot, at(1018), 1)};
    const auto last_round = update_on_heap(*policy, observation, shot, at(1020));
    REQUIRE(last_round->right.has_value());
    CHECK(checked_optional(last_round->right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    REQUIRE(last_round->left.has_value());
    CHECK(checked_optional(last_round->left).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    const auto* accent = find_voice(*last_round, detail::Cue::magazine_empty);
    REQUIRE(accent != nullptr);
    CHECK(accent->started_at == at(1018));
    CHECK(cue_voice_count(*last_round, detail::Cue::pistol_shot) == 1);

    observation.sampled_at = at(1100);
    const auto empty = update_on_heap(*policy, observation, {}, at(1100));
    CHECK_FALSE(empty->right.has_value());
    CHECK(empty->left.has_value());
    CHECK(cue_voice_count(*empty, detail::Cue::magazine_empty) == 0);

    observation.sampled_at = at(1130);
    observation.weapon.loaded_rounds = 1U;
    const auto reloaded = update_on_heap(*policy, observation, {}, at(1130));
    REQUIRE(reloaded->right.has_value());
    CHECK(checked_optional(reloaded->right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
}

TEST_CASE("fallout4.policy.magazine_cue_matches_a_shot_across_worker_ticks",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(2000);
    observation.weapon.loaded_rounds = 1U;
    static_cast<void>(update_on_heap(*policy, observation, {}, at(2000)));

    const std::array shot{event(EventKind::shot, at(2010), 1)};
    SECTION("shot arrives first") {
        const auto firing = update_on_heap(*policy, observation, shot, at(2010));
        CHECK(cue_voice_count(*firing, detail::Cue::magazine_empty) == 0);
        observation.sampled_at = at(2020);
        observation.weapon.loaded_rounds = 0U;
        const auto depleted = update_on_heap(*policy, observation, {}, at(2020));
        REQUIRE(cue_voice_count(*depleted, detail::Cue::magazine_empty) == 1);
    }
    SECTION("empty sample arrives first") {
        observation.sampled_at = at(2015);
        observation.weapon.loaded_rounds = 0U;
        const auto depleted = update_on_heap(*policy, observation, {}, at(2015));
        CHECK(cue_voice_count(*depleted, detail::Cue::magazine_empty) == 0);
        const auto firing = update_on_heap(*policy, observation, shot, at(2020));
        REQUIRE(cue_voice_count(*firing, detail::Cue::magazine_empty) == 1);
    }
    const auto unchanged = update_on_heap(*policy, observation, {}, at(2021));
    const auto* accent = find_voice(*unchanged, detail::Cue::magazine_empty);
    REQUIRE(accent != nullptr);
    CHECK(accent->started_at == at(2010));

    observation.sampled_at = at(2080);
    const std::array repeated{event(EventKind::shot, at(2080), 2)};
    const auto still_empty = update_on_heap(*policy, observation, repeated, at(2080));
    CHECK(cue_voice_count(*still_empty, detail::Cue::magazine_empty) == 0);
}

TEST_CASE("fallout4.policy.magazine_accent_requires_an_admitted_transition",
    "[component][fallout4][policy]") {
    const auto config = std::make_unique<PolicyConfig>();
    auto observation = gameplay_at(3000);
    observation.weapon.loaded_rounds = 1U;
    auto shot = event(EventKind::shot, at(3010), 1);
    SECTION("unknown ammo does not establish a loaded baseline") {
        observation.weapon.loaded_rounds.reset();
    }
    SECTION("the event belongs to a different weapon") {
        shot.weapon_identity = 43;
    }
    SECTION("an earlier shot predates the most recent loaded sample") {
        shot.arrived_at = at(2999);
    }
    SECTION("the accent can be disabled separately") {
        config->effects.body[static_cast<std::size_t>(detail::Cue::magazine_empty)].enabled = false;
    }
    const auto policy = std::make_unique<Policy>(*config);
    static_cast<void>(update_on_heap(*policy, observation, {}, at(3000)));
    observation.sampled_at = at(3020);
    observation.weapon.loaded_rounds = 0U;
    const auto empty = update_on_heap(*policy, observation, std::array{shot}, at(3020));
    CHECK(cue_voice_count(*empty, detail::Cue::magazine_empty) == 0);
}

TEST_CASE("fallout4.policy.a_confirmed_shot_can_close_only_one_magazine_transition",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(1000);
    observation.weapon.loaded_rounds = 1U;
    static_cast<void>(update_on_heap(*policy, observation, {}, at(1000)));

    observation.sampled_at = at(1015);
    observation.weapon.loaded_rounds = 0U;
    const std::array shot{event(EventKind::shot, at(1023), 1)};
    const auto first = update_on_heap(*policy, observation, shot, at(1025));
    REQUIRE(cue_voice_count(*first, detail::Cue::magazine_empty) == 1);
    const auto first_sequence = cue_sequence(*first, detail::Cue::magazine_empty);

    observation.sampled_at = at(1020);
    observation.weapon.loaded_rounds = 1U;
    static_cast<void>(update_on_heap(*policy, observation, {}, at(1026)));
    observation.sampled_at = at(1030);
    observation.weapon.loaded_rounds = 0U;
    const auto second = update_on_heap(*policy, observation, {}, at(1030));
    CHECK(cue_voice_count(*second, detail::Cue::magazine_empty) == 1);
    CHECK(cue_sequence(*second, detail::Cue::magazine_empty) == first_sequence);
}

TEST_CASE("fallout4.policy.magazine_accent_selects_the_latest_shot_inside_its_window",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(2000);
    observation.weapon.loaded_rounds = 1U;
    static_cast<void>(update_on_heap(*policy, observation, {}, at(2000)));

    observation.sampled_at = at(2020);
    observation.weapon.loaded_rounds = 0U;
    const std::array shots{event(EventKind::shot, at(2062), 1),
        event(EventKind::shot, at(2015), 2),
        event(EventKind::shot, at(2018), 3)};
    const auto frame = update_on_heap(*policy, observation, shots, at(2062));
    const auto* accent = find_voice(*frame, detail::Cue::magazine_empty);
    REQUIRE(accent != nullptr);
    CHECK(cue_voice_count(*frame, detail::Cue::magazine_empty) == 1);
    CHECK(accent->started_at == at(2018));
}

TEST_CASE("fallout4.policy.an_intermediate_loaded_sample_tracks_whether_a_shot_spent_a_round",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(1000);
    observation.weapon.loaded_rounds = 1U;
    bool observed_debit{};
    SECTION("the shot notification precedes the ammunition debit") {
    }
    SECTION("the shot has already reduced the observed rounds from two to one") {
        observation.weapon.loaded_rounds = 2U;
        observed_debit = true;
    }
    static_cast<void>(update_on_heap(*policy, observation, {}, at(1000)));

    const std::array shot{event(EventKind::shot, at(1010), 1)};
    observation.sampled_at = at(1012);
    observation.weapon.loaded_rounds = 1U;
    const auto loaded = update_on_heap(*policy, observation, shot, at(1012));
    CHECK(cue_voice_count(*loaded, detail::Cue::magazine_empty) == 0);

    observation.sampled_at = at(1020);
    observation.weapon.loaded_rounds = 0U;
    const auto empty = update_on_heap(*policy, observation, {}, at(1020));
    CHECK(cue_voice_count(*empty, detail::Cue::magazine_empty) == (observed_debit ? 0U : 1U));
    if (!observed_debit) {
        const auto* accent = find_voice(*empty, detail::Cue::magazine_empty);
        REQUIRE(accent != nullptr);
        CHECK(accent->started_at == at(1010));
    }
}

TEST_CASE("fallout4.policy.a_later_empty_sample_does_not_extend_the_magazine_window",
    "[component][fallout4][policy]") {
    const auto maximum_age = std::chrono::milliseconds{150};
    detail::MagazineCueTracker tracker;
    CHECK_FALSE(tracker.update(1U, at(1000), at(1000), maximum_age));
    CHECK_FALSE(tracker.update(0U, at(1010), at(1010), maximum_age));
    tracker.record_shot(at(1050));
    auto now = at(1160);
    bool expired{};
    SECTION("the window includes both time boundaries") {
    }
    SECTION("one millisecond beyond the lifetime expires the window") {
        now = at(1161);
        expired = true;
    }
    const auto match = tracker.update(0U, now, now, maximum_age);
    if (expired) {
        CHECK_FALSE(match);
    } else {
        CHECK(match == at(1050));
    }
}

TEST_CASE("fallout4.policy.pending_magazine_evidence_expires_or_clears_at_context_boundaries",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(3000);
    observation.weapon.loaded_rounds = 1U;
    static_cast<void>(update_on_heap(*policy, observation, {}, at(3000)));
    observation.sampled_at = at(3010);
    observation.weapon.loaded_rounds = 0U;
    static_cast<void>(update_on_heap(*policy, observation, {}, at(3010)));

    auto now = at(3040);
    auto matching_shot_at = at(3030);
    SECTION("the first empty sample bounds late shots") {
        matching_shot_at = at(3051);
        now = matching_shot_at;
        observation.sampled_at = at(3050);
    }
    SECTION("a weapon switch clears the pending transition") {
        observation.sampled_at = at(3020);
        observation.weapon.identity = 43;
        static_cast<void>(update_on_heap(*policy, observation, {}, at(3020)));
        observation.weapon.identity = 42;
    }
    SECTION("opening a menu clears the pending transition") {
        observation.sampled_at = at(3020);
        observation.mode = Mode::menu;
        static_cast<void>(update_on_heap(*policy, observation, {}, at(3020)));
        observation.mode = Mode::gameplay;
    }
    SECTION("resetting the session clears the pending transition") {
        (*policy).reset();
    }
    const std::array shot{event(EventKind::shot, matching_shot_at, 1)};
    const auto frame = update_on_heap(*policy, observation, shot, now);
    CHECK(cue_voice_count(*frame, detail::Cue::magazine_empty) == 0);
}

TEST_CASE("fallout4.policy.immediate_reload_keeps_magazine_body_cue_and_withdraws_triggers",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(6100);
    observation.weapon.loaded_rounds = 1U;
    static_cast<void>(update_on_heap(*policy, observation, {}, at(6100)));

    observation.sampled_at = at(6120);
    observation.weapon.loaded_rounds = 0U;
    observation.weapon.ready = false;
    observation.reloading = true;
    const std::array shot{event(EventKind::shot, at(6118), 1)};
    const auto empty = update_on_heap(*policy, observation, shot, at(6120));
    CHECK(cue_voice_count(*empty, detail::Cue::magazine_empty) == 1);
    CHECK_FALSE(empty->right.has_value());
    CHECK_FALSE(empty->left.has_value());
}
} // namespace fallout4_adapter::policy_tests
