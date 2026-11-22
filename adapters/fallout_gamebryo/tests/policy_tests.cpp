#include "policy_test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/policy.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <input_next_effects.h>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace fallout_gamebryo::tests {
using namespace std::chrono_literals;
using inputnext::adapters::TriggerIntent;

TEST_CASE("A gameplay context change drops the previous body effects",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    settings->body[static_cast<std::size_t>(BodyEffect::shot_pistol)].duration_scale = 2.0F;
    const auto policy = std::make_unique<Policy>(*settings);
    const auto observation = gameplay();
    const std::array events{shot(observation, 12U, 1000)};
    const auto fired = policy->update(observation, events, observation.at);
    REQUIRE(voice_count(fired) == 1U);
    REQUIRE(fired.rumble);

    auto next = observation;
    next.frame = 2U;
    next.at = at(1016);
    SECTION("a saved game changes") {
        ++next.epoch;
    }
    SECTION("the observed player changes") {
        ++next.player;
    }
    SECTION("the previous observation becomes stale") {
        next.at = at(1151);
    }
    const auto cleared = policy->update(next, {}, next.at);
    CHECK(voice_count(cleared) == 0U);
    CHECK_FALSE(cleared.rumble);
}

TEST_CASE("A full body program keeps the strongest incoming cues", "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    const std::array shots{shot(observation, 1U, 1000),
        shot(observation, 2U, 1000),
        shot(observation, 3U, 1000),
        shot(observation, 4U, 1000),
        shot(observation, 5U, 1000)};
    const auto filled = policy->update(observation, shots, observation.at);
    REQUIRE(effect_voice_count(filled, BodyEffect::shot_pistol) == shots.size());

    ++observation.frame;
    observation.at += 10ms;
    const std::array events{Event{.at = observation.at,
                                .epoch = observation.epoch,
                                .frame = observation.frame,
                                .action = 6U,
                                .mode = observation.mode,
                                .kind = EventKind::melee_swing,
                                .source = observation.player,
                                .weapon_class = WeaponClass::melee},
        Event{.at = observation.at,
            .epoch = observation.epoch,
            .frame = observation.frame,
            .action = 7U,
            .mode = observation.mode,
            .kind = EventKind::hit,
            .source = 0x99U,
            .target = observation.player,
            .confirmed_damage = true}};
    const auto mixed = policy->update(observation, events, observation.at);
    CHECK(voice_count(mixed) == shots.size());
    CHECK(effect_voice_count(mixed, BodyEffect::shot_pistol) == shots.size() - 1U);
    CHECK_FALSE(has_voice(mixed, BodyEffect::melee_swing));
    CHECK(has_voice(mixed, BodyEffect::hit_taken));
}

TEST_CASE("fallout_gamebryo.policy.deduplicates_repeated_shots_and_rejects_npc_actions",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    const auto observation = gameplay();
    auto duplicate = shot(observation, 22U, 1000);
    duplicate.weapon_class = WeaponClass::rifle;
    const std::array events{shot(observation, 22U, 1000), duplicate};

    const auto frame = policy->update(observation, events, at(1000));
    CHECK(voice_count(frame) == 1U);
    CHECK(voice_for(frame, BodyEffect::shot_pistol).source_id ==
          static_cast<std::uint32_t>(BodyEffect::shot_pistol) + 1U);

    (*policy).reset();
    auto npc = shot(observation, 23U, 1000);
    npc.source = 0x99U;
    const std::array npc_events{npc};
    const auto ignored = policy->update(observation, npc_events, at(1000));
    CHECK(voice_count(ignored) == 0U);
    REQUIRE(ignored.right.has_value());
    CHECK(ignored.right.value_or(TriggerIntent{}).payload.mode ==
          INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
}

TEST_CASE("fallout_gamebryo.policy.deduplicates_action_republished_in_a_later_frame",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    const auto initial = gameplay();
    const auto first = shot(initial, 70U, 1000);
    const std::array first_events{first};
    static_cast<void>(policy->update(initial, first_events, at(1000)));

    auto next = initial;
    next.frame = 2U;
    next.at = at(1010);
    auto replayed = shot(next, 70U, 1010);
    replayed.weapon_class = WeaponClass::rifle;
    const std::array replayed_events{replayed};
    const auto frame = policy->update(next, replayed_events, next.at);
    REQUIRE(voice_count(frame) == 1U);
    CHECK(voice_for(frame, BodyEffect::shot_pistol).started_at == at(1000));
}

TEST_CASE("fallout_gamebryo.policy.confirms_incoming_damage_and_emits_health_led",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    static_cast<void>(policy->update(observation, {}, observation.at));
    observation.frame = 2U;
    observation.at = at(1010);
    observation.health = inputnext::adapters::LedHealthSample{.current = 70.0F, .maximum = 100.0F};
    Event incoming{
        .at = observation.at,
        .epoch = observation.epoch,
        .frame = observation.frame,
        .action = 40U,
        .mode = Mode::gameplay,
        .kind = EventKind::hit,
        .source = 0x99U,
        .target = observation.player,
        .weapon_class = WeaponClass::rifle,
        .confirmed_damage = true,
    };
    const std::array events{incoming};

    const auto frame = policy->update(observation, events, observation.at);
    REQUIRE(voice_count(frame) == 1U);
    CHECK(voice_for(frame, BodyEffect::hit_taken).priority > 50);
    CHECK(frame.leds.has_value());

    (*policy).reset();
    incoming.confirmed_damage = false;
    const std::array unconfirmed{incoming};
    const auto ignored = policy->update(observation, unconfirmed, observation.at);
    CHECK(voice_count(ignored) == 0U);
}

TEST_CASE("fallout_gamebryo.policy.incoming_damage_suppresses_duplicate_health_warning",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.health = inputnext::adapters::LedHealthSample{.current = 40.0F, .maximum = 100.0F};
    static_cast<void>(policy->update(observation, {}, observation.at));

    observation.frame = 2U;
    observation.at = at(1010);
    observation.health = inputnext::adapters::LedHealthSample{.current = 30.0F, .maximum = 100.0F};
    Event incoming{};
    incoming.at = observation.at;
    incoming.epoch = observation.epoch;
    incoming.frame = observation.frame;
    incoming.action = 71U;
    incoming.mode = observation.mode;
    incoming.kind = EventKind::hit;
    incoming.source = 0x99U;
    incoming.target = observation.player;
    incoming.confirmed_damage = true;
    const std::array events{incoming};
    const auto frame = policy->update(observation, events, observation.at);
    REQUIRE(voice_count(frame) == 1U);
    CHECK(voice_for(frame, BodyEffect::hit_taken).started_at == observation.at);
}

TEST_CASE("fallout_gamebryo.policy.accepts_confirmed_blast_damage_without_an_attacker",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.health = inputnext::adapters::LedHealthSample{.current = 40.0F, .maximum = 100.0F};
    static_cast<void>(policy->update(observation, {}, observation.at));

    observation.frame = 2U;
    observation.at = at(1010);
    observation.health = inputnext::adapters::LedHealthSample{.current = 30.0F, .maximum = 100.0F};
    const std::array events{Event{.at = observation.at,
        .epoch = observation.epoch,
        .frame = observation.frame,
        .action = 72U,
        .mode = observation.mode,
        .kind = EventKind::explosion,
        .target = observation.player,
        .confirmed_damage = true}};
    const auto frame = policy->update(observation, events, observation.at);
    REQUIRE(voice_count(frame) == 1U);
    CHECK(voice_for(frame, BodyEffect::explosion).started_at == observation.at);
}

TEST_CASE("fallout_gamebryo.policy.keeps_confirmed_menu_and_vats_transition_results",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.mode = Mode::menu;
    observation.menu = Menu::lockpick;
    observation.weapon = {};
    static_cast<void>(policy->update(observation, {}, observation.at));

    const auto menu_result = Event{.at = at(1010),
        .epoch = 1U,
        .frame = 2U,
        .action = 45U,
        .mode = Mode::menu,
        .menu = Menu::lockpick,
        .kind = EventKind::pick_lock,
        .source = observation.player,
        .target = 0x200U};
    observation.mode = Mode::gameplay;
    observation.menu = Menu::none;
    observation.frame = 2U;
    observation.at = at(1010);
    const std::array closed_menu{menu_result};
    const auto opened = policy->update(observation, closed_menu, observation.at);
    CHECK(voice_count(opened) == 1U);
    CHECK(voice_for(opened, BodyEffect::pick_lock).started_at == at(1010));

    (*policy).reset();
    observation = gameplay();
    observation.mode = Mode::vats_selection;
    const auto accepted = Event{.at = at(1000),
        .epoch = 1U,
        .frame = 1U,
        .action = 46U,
        .mode = Mode::vats_selection,
        .kind = EventKind::vats_accept,
        .source = observation.player};
    observation.mode = Mode::vats_playback;
    const std::array vats_accept{accepted};
    const auto vats_frame = policy->update(observation, vats_accept, observation.at);
    CHECK(voice_count(vats_frame) == 1U);
    CHECK(voice_for(vats_frame, BodyEffect::vats_accept).started_at == at(1000));
}

TEST_CASE("Terminal results play the matching menu profile", "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    auto observation = gameplay();
    observation.mode = Mode::menu;
    observation.menu = Menu::terminal;
    Event event{.at = observation.at,
        .epoch = observation.epoch,
        .frame = observation.frame,
        .action = 1U,
        .mode = Mode::menu,
        .menu = Menu::terminal,
        .kind = EventKind::terminal_success,
        .source = observation.player,
        .target = 0x372U};
    BodyEffect effect = BodyEffect::terminal_success;
    bool should_play = true;

    SECTION("success in the terminal menu") {
    }
    SECTION("lockout as the terminal closes") {
        event.kind = EventKind::terminal_failure;
        effect = BodyEffect::terminal_failure;
        observation.mode = Mode::gameplay;
        observation.menu = Menu::none;
    }
    SECTION("another menu") {
        observation.menu = Menu::inventory;
        should_play = false;
    }
    SECTION("menu effects disabled") {
        settings->menu_effects = false;
        should_play = false;
    }
    SECTION("individual profile disabled") {
        settings->body[static_cast<std::size_t>(effect)].enabled = false;
        should_play = false;
    }

    const auto policy = std::make_unique<Policy>(*settings);
    const auto frame = policy->update(observation, std::span{&event, 1U}, observation.at);
    CHECK(voice_count(frame) == (should_play ? 1U : 0U));
    CHECK(has_voice(frame, effect) == should_play);
}

TEST_CASE("Stealth colors follow confirmed detection and clear in menus",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    settings->led.health_enabled = false;
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    constexpr std::array colors{0x0000FFU, 0x5500FFU, 0xAA00FFU, 0xFF00FFU};
    for (std::size_t state = 0; state < colors.size(); ++state) {
        observation.stealth =
            LedStealthSample{.sneaking = true, .detection_score = static_cast<std::int32_t>(state)};
        const auto frame = policy->update(observation, {}, observation.at);
        const auto led = frame.leds.value_or(inputnext::adapters::LedIntent{});
        CHECK(led.payload.lightbar_rgb == colors[state]);
        observation.at += 16ms;
        ++observation.frame;
    }
    observation.mode = Mode::menu;
    observation.menu = Menu::terminal;
    CHECK_FALSE(policy->update(observation, {}, observation.at).leds);

    observation.at += 16ms;
    ++observation.frame;
    observation.mode = Mode::gameplay;
    observation.menu = Menu::none;
    observation.stealth.reset();
    CHECK_FALSE(policy->update(observation, {}, observation.at).leds);
}

TEST_CASE("fallout_gamebryo.policy.gates_optional_movement_and_progress_cues",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.in_combat = false;
    observation.movement =
        Movement{.cell = 2U, .height = 128.0F, .airborne = false, .gait = Gait::walk};
    settings->gait_cues = true;
    const auto gait_policy = std::make_unique<Policy>(*settings);
    static_cast<void>(policy->update(observation, {}, observation.at));
    static_cast<void>(gait_policy->update(observation, {}, observation.at));

    observation.at += 16ms;
    ++observation.frame;
    observation.movement->x = 2.0F;
    const auto quiet = policy->update(observation, {}, observation.at);
    CHECK(voice_count(quiet) == 0U);
    CHECK(voice_count(gait_policy->update(observation, {}, observation.at)) == 0U);

    observation.at += 16ms;
    ++observation.frame;
    observation.movement->x = 4.0F;
    CHECK(voice_count(gait_policy->update(observation, {}, observation.at)) == 0U);

    observation.at += 16ms;
    ++observation.frame;
    observation.movement->x = 6.0F;
    const auto walking = gait_policy->update(observation, {}, observation.at);
    CHECK(voice_count(walking) == 1U);
    CHECK(voice_for(walking, BodyEffect::ordinary_step).source_id ==
          static_cast<std::uint32_t>(BodyEffect::ordinary_step) + 1U);

    (*policy).reset();
    observation = gameplay();
    const auto progress = Event{.at = observation.at,
        .epoch = 1U,
        .frame = 1U,
        .action = 51U,
        .mode = Mode::gameplay,
        .kind = EventKind::progress,
        .source = observation.player};
    const std::array events{progress};
    const auto disabled = policy->update(observation, events, observation.at);
    CHECK(voice_count(disabled) == 0U);
    settings->body[static_cast<std::size_t>(BodyEffect::progress)].enabled = true;
    const auto enabled_progress = std::make_unique<Policy>(*settings);
    const auto enabled = enabled_progress->update(observation, events, observation.at);
    CHECK(voice_count(enabled) == 1U);
    CHECK(voice_for(enabled, BodyEffect::progress).source_id ==
          static_cast<std::uint32_t>(BodyEffect::progress) + 1U);
}

TEST_CASE("fallout_gamebryo.policy_tracks_fall_apex_across_small_frame_drops",
    "[fallout-gamebryo][policy]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.movement = Movement{.cell = 3U, .height = 200.0F, .airborne = true};
    static_cast<void>(policy->update(observation, {}, observation.at));

    constexpr std::array heights{210.0F, 224.0F, 217.0F};
    for (std::size_t index{}; index < heights.size(); ++index) {
        ++observation.frame;
        observation.at += std::chrono::milliseconds{16};
        observation.movement->height = heights[index];
        const auto frame = policy->update(observation, {}, observation.at);
        CHECK(voice_count(frame) == 0U);
    }

    ++observation.frame;
    observation.at += std::chrono::milliseconds{16};
    observation.movement->height = 170.0F;
    observation.movement->airborne = false;
    const auto landed = policy->update(observation, {}, observation.at);
    CHECK(voice_for(landed, BodyEffect::landing).started_at == observation.at);
}
} // namespace fallout_gamebryo::tests
