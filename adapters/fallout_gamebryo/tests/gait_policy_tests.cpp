#include "policy_test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/policy.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <inputnext_adapters/output_frame.hpp>

#include <array>
#include <chrono>
#include <memory>

namespace fallout_gamebryo::tests {
using namespace std::chrono_literals;

TEST_CASE("Gait cues require fresh ground movement in one cell", "[fallout-gamebryo][policy]") {
    Settings settings{};
    settings.gait_cues = true;
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.in_combat = false;
    observation.movement = Movement{.cell = 2U, .gait = Gait::sneak};
    CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);

    inputnext::adapters::OutputFrame frame{};
    bool should_play = false;
    SECTION("slow crouch movement on the ground") {
        observation.at += 16ms;
        ++observation.frame;
        observation.movement->x = 0.125F;
        CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);
        observation.at += 16ms;
        ++observation.frame;
        observation.movement->x = 0.25F;
        CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);
        observation.at += 16ms;
        ++observation.frame;
        observation.movement->x = 0.375F;
        observation.movement->y = 0.125F;
        frame = policy->update(observation, {}, observation.at);
        should_play = true;
    }
    SECTION("forward against a wall") {
        observation.at += 16ms;
        ++observation.frame;
        frame = policy->update(observation, {}, observation.at);
    }
    SECTION("position jitter against an obstacle") {
        observation.at += 16ms;
        ++observation.frame;
        observation.movement->x = 0.01F;
        frame = policy->update(observation, {}, observation.at);
    }
    SECTION("airborne movement") {
        observation.at += 16ms;
        ++observation.frame;
        observation.movement->airborne = true;
        observation.movement->x = 4.0F;
        frame = policy->update(observation, {}, observation.at);
    }
    SECTION("a new cell") {
        observation.at += 16ms;
        ++observation.frame;
        ++observation.movement->cell;
        observation.movement->x = 1024.0F;
        frame = policy->update(observation, {}, observation.at);
    }
    SECTION("a new save") {
        observation.at += 16ms;
        ++observation.frame;
        ++observation.epoch;
        observation.movement->x = 1024.0F;
        frame = policy->update(observation, {}, observation.at);
    }
    SECTION("a gap between observations") {
        observation.at += snapshot_timeout + 1ms;
        ++observation.frame;
        observation.movement->x = 4.0F;
        frame = policy->update(observation, {}, observation.at);
    }

    CHECK(has_voice(frame, BodyEffect::sneak_step) == should_play);
}

TEST_CASE("Gait cues count each observation frame once", "[fallout-gamebryo][policy]") {
    Settings settings{};
    settings.gait_cues = true;
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.in_combat = false;
    observation.movement = Movement{.cell = 2U, .gait = Gait::walk};
    CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);

    observation.at += 16ms;
    ++observation.frame;
    observation.movement->x = 0.125F;
    CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);

    auto duplicate = observation;
    duplicate.at += 4ms;
    duplicate.movement->x = 1024.0F;
    CHECK(voice_count(policy->update(duplicate, {}, duplicate.at)) == 0U);

    observation.at += 20ms;
    ++observation.frame;
    observation.movement->x = 0.25F;
    CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);

    observation.at += 16ms;
    ++observation.frame;
    observation.movement->x = 0.375F;
    const auto walking = policy->update(observation, {}, observation.at);
    CHECK(has_voice(walking, BodyEffect::ordinary_step));
}

TEST_CASE("Gait cues ignore isolated jumps in position", "[fallout-gamebryo][policy]") {
    Settings settings{};
    settings.gait_cues = true;
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.in_combat = false;
    observation.movement = Movement{.cell = 2U, .gait = Gait::walk};
    CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);

    const auto advance = [&](float position_x) {
        observation.at += 16ms;
        ++observation.frame;
        observation.movement->x = position_x;
        return policy->update(observation, {}, observation.at);
    };
    SECTION("position settles at its new value") {
        CHECK(voice_count(advance(1024.0F)) == 0U);
        CHECK(voice_count(advance(1024.0F)) == 0U);
        CHECK(voice_count(advance(1024.0F)) == 0U);
    }
    SECTION("position returns immediately") {
        CHECK(voice_count(advance(1024.0F)) == 0U);
        CHECK(voice_count(advance(0.0F)) == 0U);
        CHECK(voice_count(advance(0.0F)) == 0U);
    }
    SECTION("one ordinary movement precedes the outlier") {
        CHECK(voice_count(advance(1.0F)) == 0U);
        CHECK(voice_count(advance(1024.0F)) == 0U);
        CHECK(voice_count(advance(2.0F)) == 0U);
        CHECK(voice_count(advance(3.0F)) == 0U);
        CHECK(voice_count(advance(4.0F)) == 0U);
        CHECK(has_voice(advance(5.0F), BodyEffect::ordinary_step));
    }
}

TEST_CASE("Ground speed rejects repeated jumps and tolerates uneven sampling",
    "[fallout-gamebryo][policy][gait]") {
    Settings settings{};
    settings.gait_cues = true;
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.in_combat = false;
    observation.movement = Movement{.cell = 2U, .gait = Gait::run};
    CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);

    const auto advance = [&](float distance, std::chrono::milliseconds interval) {
        observation.at += interval;
        ++observation.frame;
        observation.movement->x += distance;
        return policy->update(observation, {}, observation.at);
    };
    SECTION("same-cell jumps all go forward") {
        for (const auto interval : std::array{16ms, 33ms, 120ms, 16ms}) {
            CHECK(voice_count(advance(180.0F, interval)) == 0U);
        }
    }
    SECTION("forward speed repeatedly changes by a factor of six") {
        for (const auto distance : std::array{2.0F, 12.0F, 2.0F, 12.0F, 2.0F}) {
            CHECK(voice_count(advance(distance, 16ms)) == 0U);
        }
    }
    SECTION("normal run speed with different sampling intervals") {
        CHECK(voice_count(advance(4.8F, 16ms)) == 0U);
        CHECK(voice_count(advance(19.2F, 64ms)) == 0U);
        CHECK(has_voice(advance(1.2F, 4ms), BodyEffect::run_step));
    }
}

TEST_CASE("Gait cues reestablish motion after a right-angle turn",
    "[fallout-gamebryo][policy][gait]") {
    Settings settings{};
    settings.gait_cues = true;
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.in_combat = false;
    observation.movement = Movement{.cell = 2U, .gait = Gait::walk};
    CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);

    const auto advance = [&](float delta_x, float delta_y) {
        observation.at += 16ms;
        ++observation.frame;
        observation.movement->x += delta_x;
        observation.movement->y += delta_y;
        return policy->update(observation, {}, observation.at);
    };
    CHECK(voice_count(advance(2.0F, 0.0F)) == 0U);
    CHECK(voice_count(advance(2.0F, 0.0F)) == 0U);
    CHECK(voice_count(advance(0.0F, 2.0F)) == 0U);
    CHECK(voice_count(advance(0.0F, 2.0F)) == 0U);
    CHECK(has_voice(advance(0.0F, 2.0F), BodyEffect::ordinary_step));
}

TEST_CASE("Gait cues preserve their start and minimum interval",
    "[fallout-gamebryo][policy][gait]") {
    Settings settings{};
    settings.gait_cues = true;
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.in_combat = false;
    observation.movement = Movement{.cell = 2U, .gait = Gait::walk};
    CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);

    const auto advance = [&] {
        observation.at += 20ms;
        ++observation.frame;
        observation.movement->x += 1.5F;
        return policy->update(observation, {}, observation.at);
    };
    constexpr auto step = BodyEffect::ordinary_step;
    CHECK_FALSE(has_voice(advance(), step));
    CHECK_FALSE(has_voice(advance(), step));
    const auto first = voice_for(advance(), step);
    CHECK(first.started_at == observation.at);

    bool pause_gait = false;
    SECTION("continuous movement") {
    }
    SECTION("movement flags briefly drop out") {
        pause_gait = true;
    }
    for (auto age = 20ms; age < 620ms; age += 20ms) {
        observation.movement->gait = pause_gait && age == 200ms ? Gait::none : Gait::walk;
        const auto frame = advance();
        if (age < std::chrono::milliseconds{first.duration_ms}) {
            const auto held = voice_for(frame, step);
            CHECK(held.started_at == first.started_at);
            CHECK(held.stable_id == first.stable_id);
            CHECK(held.sequence == first.sequence);
        } else {
            CHECK_FALSE(has_voice(frame, step));
        }
    }
    const auto second = voice_for(advance(), step);
    CHECK(second.started_at == first.started_at + 620ms);
    CHECK(second.stable_id != first.stable_id);
    CHECK(second.sequence != first.sequence);
}

TEST_CASE("Gait cues restart their movement run after a stale sample",
    "[fallout-gamebryo][policy]") {
    Settings settings{};
    settings.gait_cues = true;
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.in_combat = false;
    observation.movement = Movement{.cell = 2U, .gait = Gait::run};
    static_cast<void>(policy->update(observation, {}, observation.at));

    observation.at += 16ms;
    ++observation.frame;
    observation.movement->x = 0.125F;
    CHECK_FALSE(has_voice(policy->update(observation, {}, observation.at), BodyEffect::run_step));

    observation.at += snapshot_timeout + 1ms;
    ++observation.frame;
    observation.movement->x = 0.25F;
    CHECK_FALSE(has_voice(policy->update(observation, {}, observation.at), BodyEffect::run_step));

    observation.at += 16ms;
    ++observation.frame;
    observation.movement->x = 0.375F;
    CHECK_FALSE(has_voice(policy->update(observation, {}, observation.at), BodyEffect::run_step));

    observation.at += 16ms;
    ++observation.frame;
    observation.movement->x = 0.5F;
    CHECK_FALSE(has_voice(policy->update(observation, {}, observation.at), BodyEffect::run_step));

    observation.at += 16ms;
    ++observation.frame;
    observation.movement->x = 0.625F;
    CHECK(has_voice(policy->update(observation, {}, observation.at), BodyEffect::run_step));
}

TEST_CASE("Combat and airborne gaps keep the gameplay gait cooldown",
    "[fallout-gamebryo][policy][gait]") {
    Settings settings{};
    settings.gait_cues = true;
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.in_combat = false;
    observation.movement = Movement{.cell = 2U, .gait = Gait::walk};
    static_cast<void>(policy->update(observation, {}, observation.at));

    const auto advance = [&](float distance = 1.5F) {
        observation.at += 20ms;
        ++observation.frame;
        observation.movement->x += distance;
        return policy->update(observation, {}, observation.at);
    };
    CHECK_FALSE(has_voice(advance(), BodyEffect::ordinary_step));
    CHECK_FALSE(has_voice(advance(), BodyEffect::ordinary_step));
    const auto first = voice_for(advance(), BodyEffect::ordinary_step);
    const auto check_no_new_step = [&](const auto& frame) {
        if (has_voice(frame, BodyEffect::ordinary_step)) {
            CHECK(voice_for(frame, BodyEffect::ordinary_step).started_at == first.started_at);
        }
    };

    bool combat_gap = false;
    SECTION("combat interrupts movement evidence") {
        combat_gap = true;
    }
    SECTION("airborne interrupts movement evidence") {
        combat_gap = false;
    }

    if (combat_gap) {
        observation.in_combat = true;
        check_no_new_step(advance());
        check_no_new_step(advance());
        observation.in_combat = false;
    } else {
        observation.movement->airborne = true;
        check_no_new_step(advance());
        check_no_new_step(advance());
        observation.movement->airborne = false;
    }

    for (auto since_first = observation.at - first.started_at + 20ms; since_first < 620ms;
        since_first += 20ms) {
        check_no_new_step(advance());
    }
    const auto resumed = voice_for(advance(), BodyEffect::ordinary_step);
    CHECK(resumed.started_at == first.started_at + 620ms);
    CHECK(resumed.stable_id != first.stable_id);
}

TEST_CASE("A gameplay menu transition starts a fresh gait cooldown",
    "[fallout-gamebryo][policy][gait]") {
    Settings settings{};
    settings.gait_cues = true;
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.in_combat = false;
    observation.movement = Movement{.cell = 2U, .gait = Gait::walk};
    static_cast<void>(policy->update(observation, {}, observation.at));

    const auto advance = [&] {
        observation.at += 20ms;
        ++observation.frame;
        observation.movement->x += 1.5F;
        return policy->update(observation, {}, observation.at);
    };
    CHECK_FALSE(has_voice(advance(), BodyEffect::ordinary_step));
    CHECK_FALSE(has_voice(advance(), BodyEffect::ordinary_step));
    const auto first = voice_for(advance(), BodyEffect::ordinary_step);

    observation.at += 20ms;
    ++observation.frame;
    observation.mode = Mode::menu;
    observation.menu = Menu::other;
    CHECK_FALSE(
        has_voice(policy->update(observation, {}, observation.at), BodyEffect::ordinary_step));

    observation.at += 20ms;
    ++observation.frame;
    observation.mode = Mode::gameplay;
    observation.menu = Menu::none;
    static_cast<void>(policy->update(observation, {}, observation.at));
    CHECK_FALSE(has_voice(advance(), BodyEffect::ordinary_step));
    CHECK_FALSE(has_voice(advance(), BodyEffect::ordinary_step));
    const auto after_menu = voice_for(advance(), BodyEffect::ordinary_step);
    CHECK(after_menu.started_at == observation.at);
    CHECK(after_menu.started_at < first.started_at + 620ms);
    CHECK(after_menu.stable_id != first.stable_id);
}

} // namespace fallout_gamebryo::tests
