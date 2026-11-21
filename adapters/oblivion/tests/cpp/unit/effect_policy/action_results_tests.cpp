#include "action_results.hpp"

#include "effect_model.hpp"
#include "fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>

namespace oblivion_adapter::test::effect_policy {

TEST_CASE("oblivion.action_results.matching_menu_counter_or_immediate_return",
    "[unit][oblivion][actions]") {
    struct Example {
        const char* action;
        EffectMode menu;
        EffectEventKind result;
    };

    constexpr std::array examples{Example{.action = "Training",
                                      .menu = EffectMode::training,
                                      .result = EffectEventKind::trained},
        Example{.action = "Pickpocketing",
            .menu = EffectMode::container,
            .result = EffectEventKind::pickpocketed},
        Example{.action = "Lockpicking",
            .menu = EffectMode::lockpick,
            .result = EffectEventKind::lock_opened}};
    for (const auto& example : examples) {
        DYNAMIC_SECTION(example.action) {
            auto previous = observation(100, 10);
            previous.mode = example.menu;
            previous.action_stats = ActionStats{};
            auto sample = previous;
            ++sample.frame;
            sample.sampled_at = at(115);
            sample.action_stats =
                ActionStats{.locks_picked = 1, .training_sessions = 1, .items_pickpocketed = 3};
            for (const auto mode : {example.menu, EffectMode::gameplay}) {
                sample.mode = mode;
                CHECK(menu_results(previous, sample) == MenuResults{example.result});
                auto repeated = sample;
                repeated.sampled_at = at(130);
                ++repeated.frame;
                CHECK(menu_results(sample, repeated) == MenuResults{});
            }
        }
    }
}

TEST_CASE("oblivion.action_results.deltas_cannot_cross_save_or_unrelated_menu",
    "[unit][oblivion][actions]") {
    auto previous = observation(100, 10);
    previous.mode = EffectMode::training;
    previous.action_stats = ActionStats{.training_sessions = 3};
    auto sample = previous;
    sample.frame++;
    sample.sampled_at = at(115);
    sample.action_stats = ActionStats{.training_sessions = 4};
    SECTION("save epoch changes") {
        ++sample.epoch;
    }
    SECTION("the same game frame is sampled twice") {
        sample.frame = previous.frame;
    }
    SECTION("an unrelated menu appears") {
        sample.mode = EffectMode::container;
    }
    SECTION("the first sample is stale") {
        sample.sampled_at = at(251);
    }
    SECTION("the counter decreases") {
        sample.action_stats = ActionStats{.training_sessions = 2};
    }
    CHECK(menu_results(previous, sample) == MenuResults{});
}

TEST_CASE("oblivion.action_results.gate_counter_requires_a_confirmed_transition",
    "[unit][oblivion][actions]") {
    GateCounter counter;
    auto sample = observation(100, 10);
    sample.action_stats = ActionStats{.gates_closed = 4};
    CHECK_FALSE(counter.observe(sample));

    sample.frame = 30;
    sample.sampled_at = at(2900);
    sample.action_stats = ActionStats{.gates_closed = 5};
    bool expect_result = true;
    SECTION("confirmed cell loading") {
        counter.note_loading();
    }
    SECTION("the main loop skipped the loading screen but the cell changed") {
        ++sample.cell_id;
    }
    SECTION("consecutive gameplay samples") {
        sample.sampled_at = at(116);
    }
    SECTION("save changed") {
        counter.note_loading();
        ++sample.epoch;
        expect_result = false;
    }
    SECTION("the prior context was discarded") {
        counter.note_loading();
        counter.reset();
        expect_result = false;
    }
    SECTION("long gap in the same cell without a loading screen") {
        expect_result = false;
    }
    CHECK(counter.observe(sample) == expect_result);

    CHECK_FALSE(counter.observe(sample));
}

} // namespace oblivion_adapter::test::effect_policy
