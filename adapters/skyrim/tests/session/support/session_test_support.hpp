#pragma once

#include "session/support/fake_output.hpp"

#include <skyrim_adapter/coordinator.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace skyrim_adapter::session_test {

using namespace std::chrono_literals;

[[noreturn]] void fail(const char* message);

inline void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

inline NativeTicket require_ticket(const std::optional<NativeTicket>& ticket,
    const char* message = "native ticket unavailable") {
    if (!ticket.has_value()) {
        fail(message);
    }
    return *ticket;
}

inline NativeCapture require_capture(std::optional<NativeCapture> capture,
    const char* message = "native capture unavailable") {
    if (!capture.has_value()) {
        fail(message);
    }
    return *capture;
}

[[nodiscard]] Observation idle();
[[nodiscard]] Observation casting();

RawEvent raw_event(EventKind kind, FormId actor, MonoTime observed_at);

struct Combatants {
    FormId actor{};
    FormId target{};
};

RawEvent raw_event(EventKind kind, Combatants combatants, MonoTime observed_at);

Action spell_release(Hand hand, MonoTime observed_at, std::uint64_t sequence, SpellKind spell);

float channel_peak(const std::vector<float>& samples, std::size_t channel);

struct Fixture {
    FakeOutput output;
    Coordinator coordinator;
    MonoTime time{1s};

    Fixture() : coordinator{output, CoordinatorConfig()} { initialize(); }

    explicit Fixture(const PolicyConfig& policy)
        : coordinator{output, CoordinatorConfig(), policy} {
        initialize();
    }

private:
    void initialize() {
        coordinator.control(Control::data_loaded, time);
        coordinator.control(Control::load_succeeded, time);
    }

public:
    NativeCapture capture() {
        const auto ticket = require_ticket(coordinator.native_ticket());
        return require_capture(coordinator.begin_read(ticket, time));
    }

    void observe(Observation observation, std::span<const Action> actions = {}) {
        const auto read = capture();
        coordinator.publish(read, observation, actions, time);
    }

    void settle(Observation observation) {
        observe(observation);
        coordinator.tick(time); // Bind only; this sample predates controller selection.
        time += 50ms;
        observe(observation);
        coordinator.tick(time);
    }

    void advance(Observation observation, std::chrono::milliseconds delta = 50ms) {
        time += delta;
        observe(observation);
        coordinator.tick(time);
    }
};

} // namespace skyrim_adapter::session_test

namespace skyrim_adapter {
using session_test::casting;
using session_test::channel_peak;
using session_test::Combatants;
using session_test::fail;
using session_test::Fixture;
using session_test::idle;
using session_test::raw_event;
using session_test::require;
using session_test::require_capture;
using session_test::require_ticket;
using session_test::spell_release;
using namespace std::chrono_literals;
} // namespace skyrim_adapter
