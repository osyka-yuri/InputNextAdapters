#include "../src/stealth_detection.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fallout_gamebryo/model.hpp>
#include <inputnext_adapters/led_policy.hpp>

#include <chrono>
#include <optional>

namespace {
using namespace std::chrono_literals;
using fallout_gamebryo::Mode;
using fallout_gamebryo::Observation;
using fallout_new_vegas::classify_detection;
using fallout_new_vegas::DetectionState;
using fallout_new_vegas::StealthDetection;
using inputnext::adapters::LedStealthSample;

struct DetectionSource {
    std::optional<DetectionState> state{DetectionState::hidden};
    unsigned int reads{};
};

DetectionSource& source() noexcept {
    static DetectionSource state;
    return state;
}

std::optional<DetectionState> read_detection() noexcept {
    ++source().reads;
    return source().state;
}

[[nodiscard]] constexpr Observation gameplay() noexcept {
    return {.epoch = 1U, .player = 0x14U, .mode = Mode::gameplay, .alive = true, .focused = true};
}

TEST_CASE("Game detection inputs distinguish all four stealth states", "[new-vegas][observation]") {
    CHECK(classify_detection(false, false, -1) == DetectionState::hidden);
    CHECK(classify_detection(false, false, 0) == DetectionState::hidden);
    CHECK(classify_detection(false, false, 1) == DetectionState::detected);
    CHECK(classify_detection(false, true, 0) == DetectionState::danger);
    CHECK(classify_detection(true, false, 1) == DetectionState::caution);
    CHECK(classify_detection(true, true, 1) == DetectionState::caution);
}

TEST_CASE("Stealth detection reads while crouched in gameplay and refreshes after boundaries",
    "[new-vegas][observation]") {
    source() = {};
    StealthDetection detection;
    REQUIRE(detection.bind(read_detection));
    auto observation = gameplay();
    constexpr LedStealthSample unavailable{.detection_score = -1};
    const auto hidden = detection.sample(observation, true).value_or(unavailable);
    CHECK(hidden.sneaking);
    CHECK(hidden.detection_score == 0);
    CHECK(source().reads == 1U);

    source().state = DetectionState::caution;
    observation.at += 16ms;
    const auto cached = detection.sample(observation, true).value_or(unavailable);
    CHECK(cached.detection_score == 0);
    CHECK(source().reads == 1U);

    SECTION("a later frame crosses the sampling interval while the player is at rest") {
        observation.at += 34ms;
    }
    SECTION("a menu interrupts the observation") {
        auto menu = observation;
        menu.mode = Mode::menu;
        CHECK_FALSE(detection.sample(menu, true));
    }
    SECTION("the player stands up") {
        CHECK_FALSE(detection.sample(observation, false));
    }
    SECTION("the game loses focus") {
        auto background = observation;
        background.focused = false;
        CHECK_FALSE(detection.sample(background, true));
    }
    SECTION("the player is no longer alive") {
        auto dead = observation;
        dead.alive = false;
        CHECK_FALSE(detection.sample(dead, true));
    }
    SECTION("a different save supplies a new epoch") {
        ++observation.epoch;
    }

    const auto caution = detection.sample(observation, true).value_or(unavailable);
    CHECK(caution.detection_score == 1);
    CHECK(source().reads == 2U);
}

TEST_CASE("An unavailable game detection source withdraws the previous sample",
    "[new-vegas][observation]") {
    source() = {.state = DetectionState::danger};
    StealthDetection detection;
    REQUIRE(detection.bind(read_detection));
    auto observation = gameplay();
    REQUIRE(detection.sample(observation, true));

    SECTION("the player is no longer available to the engine") {
        source().state.reset();
        observation.at += 50ms;
        CHECK_FALSE(detection.sample(observation, true));
        CHECK(source().reads == 2U);
    }
    SECTION("the engine reader is unavailable") {
        CHECK_FALSE(detection.bind(nullptr));
        CHECK_FALSE(detection.sample(observation, true));
        CHECK(source().reads == 1U);
    }
}
} // namespace
