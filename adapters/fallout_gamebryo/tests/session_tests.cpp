#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/session.hpp>
#include <fallout_gamebryo/settings.hpp>
#include <fallout_gamebryo/worker.hpp>

#include "output_session/fake_output.hpp"

#include <catch2/catch_test_macros.hpp>
#include <input_next_core.h>
#include <input_next_effects.h>
#include <inputnext_adapters/output_session.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace {
using namespace std::chrono_literals;
using namespace fallout_gamebryo;
using inputnext::adapters::TickClock;
using inputnext::test::output_session::FakeOutput;

constexpr std::uint32_t player = 0x14;
constexpr std::uint32_t rifle = 0x100;

std::atomic_size_t cleanup_notifications{};

Settings test_settings() noexcept {
    Settings result{};
    result.led.enabled = false;
    return result;
}

Observation sample(MonoTime sampled_at, std::uint64_t frame, Mode mode = Mode::gameplay) noexcept {
    return {.at = sampled_at,
        .epoch = 1,
        .frame = frame,
        .player = player,
        .mode = mode,
        .weapon = {.id = rifle,
            .kind = WeaponClass::rifle,
            .loaded_ammo = 5,
            .drawn = true,
            .ready = true},
        .alive = true,
        .focused = true,
        .health = LedHealthSample{.current = 100.0F, .maximum = 100.0F},
        .stealth = std::nullopt};
}

Event shot(const Observation& observation) noexcept {
    return {.at = observation.at,
        .epoch = observation.epoch,
        .frame = observation.frame,
        .action = observation.frame,
        .mode = observation.mode,
        .kind = EventKind::shot,
        .source = player,
        .weapon = rifle,
        .weapon_class = WeaponClass::rifle};
}

void tick(Session& session, const Observation& observation, std::span<const Event> events = {}) {
    session.publish(observation, events);
    session.tick(TickClock{.supplied = observation.at});
}

bool has_pulse(const FakeOutput& output) {
    return std::ranges::any_of(output.body_calls, [](const auto& call) {
        return call.request.pcm.frame_count > 0 ||
               call.request.fallback_rumble.low_frequency > 0.0F ||
               call.request.fallback_rumble.high_frequency > 0.0F;
    });
}

TEST_CASE("Fallout delivery requires a sample after opening or replacing a controller",
    "[fallout-gamebryo][session]") {
    FakeOutput output;
    const auto settings = std::make_unique<Settings>(test_settings());
    const auto session = std::make_unique<Session>(output, *settings);
    const MonoTime start{};

    const auto first = sample(start, 1);
    tick(*session, first, std::array{shot(first)});
    CHECK(output.opens.size() == 1);
    CHECK_FALSE(has_pulse(output));

    const auto ready = sample(start + 1ms, 2);
    tick(*session, ready, std::array{shot(ready)});
    REQUIRE(has_pulse(output));

    output.body_calls.clear();
    output.target_status = INPUT_NEXT_STATUS_NO_DATA;
    const auto lost = sample(start + 2ms, 3);
    tick(*session, lost, std::array{shot(lost)});
    tick(*session, sample(start + 203ms, 4));
    CHECK_FALSE(has_pulse(output));

    output.target_status = INPUT_NEXT_STATUS_OK;
    ++output.target.connection_id;
    const auto reconnect = sample(start + 204ms, 5);
    tick(*session, reconnect, std::array{shot(reconnect)});
    CHECK(output.opens.size() == 2);
    CHECK_FALSE(has_pulse(output));
    const auto resumed = sample(start + 205ms, 6);
    tick(*session, resumed, std::array{shot(resumed)});
    CHECK(has_pulse(output));
}

TEST_CASE("Fallout restores the stealth lightbar from a fresh observation",
    "[fallout-gamebryo][session][led]") {
    FakeOutput output;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_LIGHTBAR;
    Settings settings;
    settings.body_enabled = false;
    settings.triggers_enabled = false;
    const auto session = std::make_unique<Session>(output, settings);
    const MonoTime start{};
    tick(*session, sample(start, 1));

    auto crouched = sample(start + 1ms, 2);
    crouched.stealth = LedStealthSample{.sneaking = true, .detection_score = 0};
    tick(*session, crouched);
    REQUIRE(output.leds.size() == 1U);
    CHECK(output.leds.front().leds.lightbar_rgb == 0x0000FFU);
    output.leds.clear();

    auto resumed = crouched;
    SECTION("a terminal withdraws the lightbar until gameplay resumes") {
        auto menu = sample(start + 2ms, 3, Mode::menu);
        menu.menu = Menu::terminal;
        tick(*session, menu);
        REQUIRE(output.led_clear_requests.size() == 1U);
        resumed.at = start + 3ms;
        resumed.frame = 4;
    }
    SECTION("a new controller waits for a sample captured after binding") {
        output.target_status = INPUT_NEXT_STATUS_NO_DATA;
        tick(*session, sample(start + 2ms, 3));
        tick(*session, sample(start + 203ms, 4));
        output.target_status = INPUT_NEXT_STATUS_OK;
        ++output.target.connection_id;
        auto reconnect = crouched;
        reconnect.at = start + 204ms;
        reconnect.frame = 5;
        tick(*session, reconnect);
        REQUIRE(output.opens.size() == 2U);
        resumed.at = start + 205ms;
        resumed.frame = 6;
    }
    CHECK(output.leds.empty());
    resumed.stealth->detection_score = 2;
    tick(*session, resumed);
    REQUIRE(output.leds.size() == 1U);
    CHECK(output.leds.front().leds.lightbar_rgb == 0xAA00FFU);
    CHECK(output.leds.front().target.connection_id == output.target.connection_id);
}

TEST_CASE("Fallout menu transitions clear gameplay and retain confirmed menu outcomes",
    "[fallout-gamebryo][session]") {
    FakeOutput output;
    const Settings settings = test_settings();
    const auto session = std::make_unique<Session>(output, settings);
    const MonoTime start{};

    tick(*session, sample(start, 1));
    const auto gameplay = sample(start + 1ms, 2);
    tick(*session, gameplay, std::array{shot(gameplay)});
    REQUIRE(has_pulse(output));

    output.body_calls.clear();
    auto menu = sample(start + 2ms, 3, Mode::menu);
    menu.menu = Menu::lockpick;
    tick(*session, menu, std::array{shot(menu)});
    CHECK_FALSE(has_pulse(output));
    CHECK_FALSE(output.body_clears.empty());

    const auto closed = sample(start + 3ms, 4);
    Event result{.at = closed.at,
        .epoch = closed.epoch,
        .frame = closed.frame,
        .action = 1,
        .mode = Mode::menu,
        .menu = Menu::lockpick,
        .kind = EventKind::pick_lock,
        .source = player};
    auto destination = closed;
    SECTION("opening the lock closes the menu") {
    }
    SECTION("breaking a pin keeps the menu open") {
        result.kind = EventKind::break_lockpick;
        destination.mode = Mode::menu;
        destination.menu = Menu::lockpick;
    }
    SECTION("the terminal closes before the lockout is delivered") {
        menu.at = closed.at;
        menu.frame = closed.frame;
        menu.menu = Menu::terminal;
        tick(*session, menu);
        result.at = menu.at + 1ms;
        result.menu = Menu::terminal;
        result.kind = EventKind::terminal_failure;
        destination = sample(menu.at + 2ms, menu.frame + 1U);
    }
    tick(*session, destination, std::array{result});
    CHECK(has_pulse(output));
}

TEST_CASE("A magazine change during delivery withdraws a stale attack hold and preserves aim",
    "[fallout-gamebryo][session]") {
    FakeOutput output;
    const Settings settings = test_settings();
    const auto session = std::make_unique<Session>(output, settings);
    const MonoTime start{};
    tick(*session, sample(start, 1));
    auto loaded = sample(start + 1ms, 2);
    loaded.attack_trigger = TriggerSide::right;
    loaded.aim_trigger = TriggerSide::left;
    SECTION("attack resistance") {
    }
    SECTION("aim resistance") {
        loaded.aiming = true;
    }
    auto empty = loaded;
    empty.at += 1ms;
    ++empty.frame;
    empty.weapon.loaded_ammo = 0U;
    output.after_trigger_submit = [&] {
        session->publish(empty, {});
    };

    session->publish(loaded, {});
    session->tick(TickClock{.supplied = empty.at});

    REQUIRE(output.triggers.size() == 1U);
    if (loaded.aiming) {
        CHECK(output.triggers.front().channel_mask ==
              (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER));
        REQUIRE(output.trigger_clear_requests.size() == 1U);
        CHECK(output.trigger_clear_requests.front().channel_mask ==
              (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER));
    } else {
        CHECK(output.triggers.front().channel_mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER);
        REQUIRE(output.trigger_clear_requests.size() == 1U);
        CHECK(
            output.trigger_clear_requests.front().channel_mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER);
    }

    output.after_trigger_submit = {};
    output.trigger_clear_requests.clear();
    ++empty.frame;
    empty.at += 1ms;
    session->publish(empty, {});
    session->tick(TickClock{.supplied = empty.at});
    session->tick(TickClock{.supplied = empty.at});

    if (loaded.aiming) {
        REQUIRE(output.triggers.size() == 2U);
        CHECK(output.triggers.back().channel_mask == INPUT_NEXT_EFFECTS_LEFT_TRIGGER);
    } else {
        CHECK(output.triggers.size() == 1U);
    }
}

TEST_CASE("Fallout stale snapshots, save changes, focus and overflow revoke output",
    "[fallout-gamebryo][session]") {
    FakeOutput output;
    const auto settings = std::make_unique<Settings>(test_settings());
    const auto session = std::make_unique<Session>(output, *settings);
    const MonoTime start{};
    tick(*session, sample(start, 1));
    const auto ready = sample(start + 1ms, 2);
    tick(*session, ready, std::array{shot(ready)});
    REQUIRE(has_pulse(output));
    output.body_calls.clear();

    SECTION("watchdog expiry") {
        session->tick(TickClock{.supplied = ready.at + snapshot_timeout + 1ms});
    }
    SECTION("focus loss") {
        const auto now = start + 2ms;
        session->set_foreground(false, now);
        const auto ignored = sample(now, 3);
        tick(*session, ignored, std::array{shot(ignored)});
    }
    SECTION("save changes") {
        auto next = sample(start + 2ms, 3);
        ++next.epoch;
        tick(*session, next, std::array{shot(next)});
    }
    SECTION("queue overflow") {
        std::vector<Event> events(event_capacity + 1);
        const auto next = sample(start + 2ms, 3);
        std::ranges::fill(events, shot(next));
        tick(*session, next, events);
    }
    CHECK_FALSE(has_pulse(output));
    CHECK_FALSE(output.closes.empty());
}

TEST_CASE("Fallout waits for a pending context close before reopening",
    "[fallout-gamebryo][session]") {
    FakeOutput output;
    const Settings settings = test_settings();
    const auto session = std::make_unique<Session>(output, settings);
    const MonoTime start{};
    tick(*session, sample(start, 1));
    REQUIRE(output.opens.size() == 1);

    output.close_status = INPUT_NEXT_STATUS_BUSY;
    const auto reset_at = start + 2ms;
    session->invalidate(reset_at);
    tick(*session, sample(reset_at, 2));
    tick(*session, sample(reset_at + 20ms, 3));
    CHECK(output.close_calls >= 2);
    CHECK(output.opens.size() == 1);

    output.close_status = INPUT_NEXT_STATUS_REVOKED;
    tick(*session, sample(reset_at + 40ms, 4));
    CHECK(output.opens.size() == 2);
}

TEST_CASE("Fallout worker bounds context cleanup after repeated stop requests",
    "[fallout-gamebryo][worker]") {
    FakeOutput output;
    output.close_status = INPUT_NEXT_STATUS_BUSY;
    bool recover{};
    SECTION("the next close attempt succeeds") {
        recover = true;
    }
    SECTION("the runtime remains busy") {
    }
    SECTION("the runtime always reports an I/O error") {
        output.close_status = INPUT_NEXT_STATUS_IO_FAILED;
    }

    std::promise<void> queried;
    const auto ready = queried.get_future();
    bool reported_query{};
    output.after_context_query = [&] {
        if (!reported_query) {
            reported_query = true;
            queried.set_value();
        }
        if (recover && output.close_calls != 0U) {
            output.close_status = INPUT_NEXT_STATUS_REVOKED;
        }
    };
    cleanup_notifications.store(0, std::memory_order_relaxed);
    MonoTime stopped_at;
    {
        const Settings settings = test_settings();
        const WorkerCallbacks callbacks{.cleanup_incomplete = [] noexcept {
            cleanup_notifications.fetch_add(1U, std::memory_order_relaxed);
        }};
        const auto worker = std::make_unique<EffectWorker>(output, settings, callbacks);
        REQUIRE(worker->start());
        worker->publish(sample(std::chrono::steady_clock::now(), 1));
        REQUIRE(ready.wait_for(2s) == std::future_status::ready);
        stopped_at = std::chrono::steady_clock::now();
        worker->request_stop();
        worker->request_stop();
    }
    CHECK(output.close_calls > 1U);
    CHECK(output.ack_close_calls == (recover ? 1U : 0U));
    CHECK(cleanup_notifications.load(std::memory_order_relaxed) == (recover ? 0U : 1U));
    CHECK(std::chrono::steady_clock::now() - stopped_at < 2s);
}
} // namespace
