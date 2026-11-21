#include "effect_model.hpp"
#include "fixtures.hpp"
#include "gait_feedback.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <limits>

namespace oblivion_adapter::test::effect_policy {
namespace {

using namespace std::chrono_literals;

struct Travel {
    float x{};
    float y{};
};

struct Track {
    GaitFeedback feedback;
    EffectObservation sample = observation(0);
    HorizontalPosition position{.mover_id = sample.player_id};

    explicit Track(Gait gait = Gait::walk) noexcept {
        sample.gait = gait;
        const bool mounted = gait == Gait::mounted_walk || gait == Gait::mounted_run;
        sample.mounted = mounted;
        if (mounted) {
            position.mover_id = 0x314U;
        }
    }

    [[nodiscard]] Gait observe() noexcept {
        sample.horizontal_position = position;
        return feedback.observe(sample);
    }

    [[nodiscard]] Gait advance(std::chrono::milliseconds elapsed, Travel delta = {}) noexcept {
        sample.sampled_at += elapsed;
        ++sample.frame;
        position.x += delta.x;
        position.y += delta.y;
        return observe();
    }
};

struct StraightTravel {
    std::chrono::milliseconds period;
    std::chrono::milliseconds duration;
    float units_per_second;
};

int count_cues(Track& track, StraightTravel motion) {
    int cues = 0;
    const float seconds = std::chrono::duration<float>{motion.period}.count();
    const Travel delta{.x = motion.units_per_second * seconds};
    auto last_cue = track.sample.sampled_at - 250ms;
    for (auto elapsed = motion.period; elapsed <= motion.duration; elapsed += motion.period) {
        if (const auto cue = track.advance(motion.period, delta); cue != Gait::none) {
            CHECK(cue == track.sample.gait);
            CHECK(track.sample.sampled_at - last_cue >= 250ms);
            last_cue = track.sample.sampled_at;
            ++cues;
        }
    }
    return cues;
}

} // namespace

TEST_CASE("oblivion.gait_feedback.selects_the_gait_and_bounds_cadence",
    "[unit][oblivion][effects][gait]") {
    struct Example {
        Gait gait;
        float speed;
    };

    constexpr std::array examples{Example{.gait = Gait::walk, .speed = 125.0F},
        Example{.gait = Gait::run, .speed = 260.0F},
        Example{.gait = Gait::sneak, .speed = 70.0F},
        Example{.gait = Gait::mounted_walk, .speed = 200.0F},
        Example{.gait = Gait::mounted_run, .speed = 430.0F}};
    for (const auto& example : examples) {
        Track track{example.gait};
        CHECK(track.observe() == Gait::none);
        CHECK(count_cues(track,
                  {.period = 20ms, .duration = 2500ms, .units_per_second = example.speed}) >= 2);
    }
    Track fast{Gait::walk};
    CHECK(fast.observe() == Gait::none);
    CHECK(count_cues(fast, {.period = 20ms, .duration = 1200ms, .units_per_second = 1000.0F}) == 5);
}

TEST_CASE("oblivion.gait_feedback.accumulates_slow_travel_at_high_frame_rates",
    "[unit][oblivion][effects][gait]") {
    for (const auto period : {8ms, 20ms, 50ms, 100ms}) {
        Track track{Gait::sneak};
        CHECK(track.observe() == Gait::none);
        CHECK(count_cues(track,
                  {.period = period, .duration = 7600ms, .units_per_second = 20.0F}) == 3);
    }
    Track jitter{Gait::sneak};
    CHECK(jitter.observe() == Gait::none);
    for (int frame = 0; frame < 500; ++frame) {
        const float offset = frame % 2 == 0 ? 0.12F : -0.12F;
        CHECK(jitter.advance(8ms, {.x = offset}) == Gait::none);
    }
}

TEST_CASE("oblivion.gait_feedback.requires_grounded_motion_from_the_correct_actor",
    "[unit][oblivion][effects][gait]") {
    Track track;
    CHECK(track.observe() == Gait::none);
    CHECK(count_cues(track, {.period = 20ms, .duration = 500ms, .units_per_second = 0.0F}) == 0);

    SECTION("airborne") {
        track.sample.ground = Ground::airborne;
    }
    SECTION("ground state unavailable") {
        track.sample.ground = Ground::unknown;
    }
    SECTION("swimming") {
        track.sample.swimming = true;
    }
    SECTION("another mover") {
        ++track.position.mover_id;
    }
    SECTION("a menu") {
        track.sample.mode = EffectMode::menu;
    }
    SECTION("invalid position") {
        track.position.x = std::numeric_limits<float>::quiet_NaN();
    }
    CHECK(count_cues(track, {.period = 20ms, .duration = 500ms, .units_per_second = 150.0F}) == 0);
}

TEST_CASE("oblivion.gait_feedback_restarts_progress_on_motion_discontinuities",
    "[unit][oblivion][effects][gait]") {
    Track track;
    CHECK(track.observe() == Gait::none);
    for (int frame = 0; frame < 3; ++frame) {
        CHECK(track.advance(20ms, {.x = 20.0F}) == Gait::none);
    }
    SECTION("a hard turn") {
        CHECK(track.advance(20ms, {.y = 20.0F}) == Gait::none);
        CHECK(track.advance(20ms, {.y = 20.0F}) == Gait::none);
        CHECK(track.advance(20ms, {.y = 20.0F}) == Gait::none);
        CHECK(track.advance(20ms, {.y = 20.0F}) == Gait::walk);
    }
    SECTION("consecutive teleports") {
        CHECK(count_cues(track, {.period = 20ms, .duration = 200ms, .units_per_second = 2500.0F}) ==
              0);
        CHECK(track.advance(20ms, {.x = 20.0F}) == Gait::none);
    }
    SECTION("a sharp speed change within the speed ceiling") {
        CHECK(track.advance(100ms, {.x = 10.0F}) == Gait::none);
        CHECK(track.advance(100ms, {.x = 10.0F}) == Gait::none);
    }
    SECTION("a long gap") {
        CHECK(track.advance(151ms, {.x = 20.0F}) == Gait::none);
    }
    SECTION("a new cell") {
        ++track.sample.cell_id;
        CHECK(track.advance(20ms, {.x = 20.0F}) == Gait::none);
    }
    SECTION("a new save") {
        ++track.sample.epoch;
        CHECK(track.advance(20ms, {.x = 20.0F}) == Gait::none);
    }
    SECTION("a gait change") {
        track.sample.gait = Gait::sneak;
        CHECK(track.advance(20ms, {.x = 20.0F}) == Gait::none);
    }
}

TEST_CASE("oblivion.gait_feedback_ignores_repeated_and_out_of_order_frames",
    "[unit][oblivion][effects][gait]") {
    Track track;
    CHECK(track.observe() == Gait::none);
    const auto old = track.sample;
    REQUIRE(
        count_cues(track, {.period = 20ms, .duration = 80ms, .units_per_second = 1000.0F}) == 1);
    for (int repeat = 0; repeat < 4; ++repeat) {
        CHECK(track.observe() == Gait::none);
    }
    CHECK(track.feedback.observe(old) == Gait::none);
    auto later_frame_with_old_time = old;
    later_frame_with_old_time.frame = track.sample.frame + 1U;
    CHECK(track.feedback.observe(later_frame_with_old_time) == Gait::none);
    REQUIRE(
        count_cues(track, {.period = 20ms, .duration = 480ms, .units_per_second = 1000.0F}) == 1);
}

} // namespace oblivion_adapter::test::effect_policy
