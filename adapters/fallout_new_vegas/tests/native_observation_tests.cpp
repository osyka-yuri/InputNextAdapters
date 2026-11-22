#include "../src/native_observation.hpp"

#include <fallout_gamebryo/model.hpp>
#include <inputnext_adapters/body_program.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>
#include <optional>

namespace {
using namespace std::chrono_literals;
using fallout_gamebryo::Gait;
using fallout_gamebryo::Menu;
using fallout_gamebryo::Mode;
using fallout_gamebryo::MonoTime;
using fallout_gamebryo::Observation;
using fallout_gamebryo::TriggerSide;
using fallout_new_vegas::gait_from_movement_flags;
using fallout_new_vegas::LandingTriggerReadiness;
using fallout_new_vegas::make_observation;
using fallout_new_vegas::ObservationFacts;
using fallout_new_vegas::recently_in_terminal;
using fallout_new_vegas::should_cancel_pending_shots;

TEST_CASE("Native actor values map to finite normalized player observations",
    "[new-vegas][observation]") {
    const ObservationFacts facts{.at = {},
        .epoch = 3,
        .frame = 44,
        .player = 0x14,
        .mode = Mode::gameplay,
        .menu = Menu::none,
        .attack_trigger = TriggerSide::right,
        .aim_trigger = TriggerSide::left,
        .health_current = 40.0F,
        .health_maximum = 80.0F,
        .action_points_current = 10.0F,
        .action_points_maximum = 20.0F,
        .radiation = 0.5F,
        .radiation_sources = 2U,
        .focused = true,
        .in_combat = true};

    const auto observation = make_observation(facts);
    const auto health = observation.health.value_or(fallout_gamebryo::LedHealthSample{});
    REQUIRE(observation.health.has_value());
    CHECK(health.current == 40.0F);
    CHECK(health.maximum == 80.0F);
    const auto action_points_ratio = observation.action_points_ratio.value_or(-1.0F);
    REQUIRE(observation.action_points_ratio.has_value());
    CHECK(action_points_ratio == 0.5F);
    const auto radiation_sources = observation.radiation_sources.value_or(0U);
    REQUIRE(observation.radiation_sources.has_value());
    CHECK(radiation_sources == 2U);
    CHECK(observation.radiation.value_or(-1.0F) == 0.5F);
    CHECK(observation.attack_trigger == TriggerSide::right);
    CHECK(observation.aim_trigger == TriggerSide::left);
    CHECK(observation.mode == Mode::gameplay);
    CHECK(observation.alive);
}

TEST_CASE("Missing or invalid actor values do not invent health or AP",
    "[new-vegas][observation]") {
    ObservationFacts facts{};
    facts.player = 0x14;
    facts.mode = Mode::gameplay;
    facts.health_current = 5.0F;
    facts.health_maximum = 0.0F;
    facts.action_points_current = std::numeric_limits<float>::quiet_NaN();
    facts.action_points_maximum = 10.0F;

    const auto observation = make_observation(facts);
    CHECK_FALSE(observation.health);
    CHECK_FALSE(observation.alive);
    CHECK_FALSE(observation.action_points_ratio);
}

TEST_CASE("An already closed terminal requires a fresh terminal snapshot",
    "[new-vegas][observation]") {
    std::optional observation{Observation{.at = MonoTime{} + 10ms,
        .player = 0x14U,
        .mode = Mode::menu,
        .menu = Menu::terminal,
        .alive = true,
        .focused = true}};
    const auto callback_at = MonoTime{} + 14ms;
    REQUIRE(recently_in_terminal(observation, callback_at));

    SECTION("a different menu") {
        observation->menu = Menu::lockpick;
    }
    SECTION("gameplay was already observed") {
        observation->mode = Mode::gameplay;
    }
    SECTION("the last terminal snapshot expired") {
        observation->at = callback_at - fallout_gamebryo::snapshot_timeout - 1ms;
    }
    SECTION("the game lost focus") {
        observation->focused = false;
    }
    SECTION("the world context was invalidated") {
        observation.reset();
    }
    CHECK_FALSE(recently_in_terminal(observation, callback_at));
}

TEST_CASE("Snapshot without the player is unavailable and carries no player health",
    "[new-vegas][observation]") {
    ObservationFacts facts{};
    facts.mode = Mode::gameplay;
    facts.health_current = 100.0F;
    facts.health_maximum = 100.0F;

    const auto observation = make_observation(facts);
    CHECK(observation.mode == Mode::unavailable);
    CHECK_FALSE(observation.health);
    CHECK_FALSE(observation.alive);
}

TEST_CASE("Nonfinite movement, radiation and zero health are handled safely",
    "[new-vegas][observation]") {
    ObservationFacts facts{};
    facts.player = 0x14;
    facts.mode = Mode::gameplay;
    facts.health_current = 0.0F;
    facts.health_maximum = 100.0F;
    facts.radiation = std::numeric_limits<float>::infinity();
    facts.movement = fallout_gamebryo::Movement{.cell = 0x11,
        .height = std::numeric_limits<float>::quiet_NaN(),
        .gait = Gait::run};

    const auto observation = make_observation(facts);
    const auto health = observation.health.value_or(fallout_gamebryo::LedHealthSample{});
    REQUIRE(observation.health.has_value());
    CHECK(health.current == 0.0F);
    CHECK_FALSE(observation.alive);
    CHECK_FALSE(observation.radiation);
    CHECK_FALSE(observation.radiation_sources);
    CHECK_FALSE(observation.movement);
}

TEST_CASE("Verified airborne state is preserved in movement observations",
    "[new-vegas][observation]") {
    ObservationFacts facts{};
    facts.player = 0x14;
    facts.mode = Mode::gameplay;
    facts.movement = fallout_gamebryo::Movement{.cell = 0x11,
        .height = 25.0F,
        .airborne = true,
        .gait = Gait::run};

    auto observation = make_observation(facts);
    const auto airborne = observation.movement.value_or(fallout_gamebryo::Movement{});
    REQUIRE(observation.movement.has_value());
    CHECK(airborne.airborne);

    facts.movement = fallout_gamebryo::Movement{.cell = 0x11,
        .height = 25.0F,
        .airborne = false,
        .gait = Gait::walk};
    observation = make_observation(facts);
    const auto grounded = observation.movement.value_or(fallout_gamebryo::Movement{});
    REQUIRE(observation.movement.has_value());
    CHECK_FALSE(grounded.airborne);
}

TEST_CASE("New Vegas movement flags select grounded gait cues", "[new-vegas][observation]") {
    CHECK(gait_from_movement_flags(0x101U, false) == Gait::walk);
    CHECK(gait_from_movement_flags(0x202U, false) == Gait::run);
    CHECK(gait_from_movement_flags(0x408U, false) == Gait::sneak);
    CHECK(gait_from_movement_flags(0x604U, false) == Gait::sneak);

    CHECK(gait_from_movement_flags(0x400U, false) == Gait::none);
    CHECK(gait_from_movement_flags(0x410U, false) == Gait::none);
    CHECK(gait_from_movement_flags(0xC01U, false) == Gait::none);
    CHECK(gait_from_movement_flags(0x401U, true) == Gait::none);

    CHECK(fallout_new_vegas::is_sneaking(0x400U));
    CHECK_FALSE(fallout_new_vegas::is_sneaking(0x202U));
    CHECK_FALSE(fallout_new_vegas::is_sneaking(0xC01U));
}

TEST_CASE("Combat reactions require a player observation", "[new-vegas][observation]") {
    ObservationFacts facts{};
    facts.reaction = fallout_gamebryo::PlayerReaction::knocked_down;
    CHECK_FALSE(make_observation(facts).reaction);

    facts.player = 0x14U;
    CHECK(make_observation(facts).reaction == fallout_gamebryo::PlayerReaction::knocked_down);
    facts.reaction.reset();
    CHECK_FALSE(make_observation(facts).reaction);
}

TEST_CASE("Landing recovery preserves readiness across the delayed animation",
    "[new-vegas][observation]") {
    LandingTriggerReadiness readiness;
    const Observation airborne{.at = MonoTime{} + 10ms,
        .epoch = 1U,
        .player = 0x14U,
        .mode = Mode::gameplay,
        .weapon = {.id = 0x202U, .drawn = true, .ready = true},
        .alive = true,
        .focused = true,
        .movement = fallout_gamebryo::Movement{.cell = 0x11U, .airborne = true}};

    auto grounded = airborne;
    grounded.at += 16ms;
    grounded.movement = fallout_gamebryo::Movement{.cell = 0x11U};
    readiness.apply(grounded, airborne, false);
    CHECK(grounded.weapon.ready);

    auto recovering = grounded;
    recovering.at += 16ms;
    recovering.weapon.ready = false;
    readiness.apply(recovering, grounded, true);
    REQUIRE(recovering.weapon.ready);

    auto next = recovering;
    for (int frame = 0; frame < 4; ++frame) {
        next.at += 90ms;
        next.weapon.ready = false;
        readiness.apply(next, recovering, true);
        CHECK(next.weapon.ready);
        recovering = next;
    }
    next.at += 16ms;
    next.movement->airborne = true;
    next.weapon.ready = false;
    readiness.apply(next, recovering, true);
    REQUIRE(next.weapon.ready);
    recovering = next;

    next.at += 16ms;
    next.weapon.ready = false;
    readiness.apply(next, recovering, false);
    CHECK_FALSE(next.weapon.ready);
}

TEST_CASE("Landing readiness requires a fresh snapshot of the same drawn weapon",
    "[new-vegas][observation]") {
    LandingTriggerReadiness readiness;
    Observation previous{.at = MonoTime{} + 10ms,
        .epoch = 1U,
        .player = 0x14U,
        .mode = Mode::gameplay,
        .weapon = {.id = 0x202U, .drawn = true, .ready = true},
        .alive = true,
        .focused = true,
        .movement = fallout_gamebryo::Movement{.cell = 0x11U, .airborne = true}};
    auto current = previous;
    current.at += 16ms;
    current.movement = fallout_gamebryo::Movement{.cell = 0x11U};
    current.weapon.ready = false;

    SECTION("an idle without a landing") {
        previous.movement = current.movement;
    }
    SECTION("an idle before landing") {
        current.movement->airborne = true;
    }
    SECTION("a new weapon") {
        ++current.weapon.id;
    }
    SECTION("an unequipped weapon") {
        current.weapon.drawn = false;
    }
    SECTION("a reload") {
        current.reloading = true;
    }
    SECTION("a stale sample") {
        current.at += 200ms;
    }
    SECTION("a cell change") {
        current.movement = fallout_gamebryo::Movement{.cell = 0x12U};
    }
    SECTION("a focus loss") {
        current.focused = false;
    }
    readiness.apply(current, previous, true);
    CHECK_FALSE(current.weapon.ready);
}

TEST_CASE("Gameplay interruptions cancel pending shots", "[new-vegas][observation][signals]") {
    ObservationFacts facts{.at = {},
        .epoch = 4,
        .frame = 10,
        .player = 0x14,
        .mode = Mode::gameplay,
        .health_current = 50.0F,
        .health_maximum = 50.0F,
        .focused = true};
    facts.weapon.id = 0x202;
    const auto previous = make_observation(facts);

    CHECK_FALSE(should_cancel_pending_shots(previous, std::nullopt));

    auto next_facts = facts;
    next_facts.frame++;
    next_facts.reloading = true;
    CHECK(should_cancel_pending_shots(make_observation(next_facts), previous));

    next_facts = facts;
    next_facts.player = 0x15;
    CHECK(should_cancel_pending_shots(make_observation(next_facts), previous));

    next_facts = facts;
    next_facts.weapon.id++;
    CHECK(should_cancel_pending_shots(make_observation(next_facts), previous));

    next_facts = facts;
    next_facts.mode = Mode::menu;
    CHECK(should_cancel_pending_shots(make_observation(next_facts), previous));

    next_facts.mode = Mode::vats_playback;
    CHECK(should_cancel_pending_shots(make_observation(next_facts), previous));

    next_facts = facts;
    next_facts.focused = false;
    const auto unfocused = make_observation(next_facts);
    CHECK(should_cancel_pending_shots(unfocused, previous));
    CHECK(should_cancel_pending_shots(previous, unfocused));

    next_facts = facts;
    next_facts.health_current = 0.0F;
    const auto dead = make_observation(next_facts);
    CHECK(should_cancel_pending_shots(dead, previous));
    CHECK(should_cancel_pending_shots(previous, dead));
}
} // namespace
