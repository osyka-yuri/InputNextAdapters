#include "diagnostics.hpp"

#include "native_combat.hpp"

#include <fallout_gamebryo/model.hpp>

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <mutex>
#include <optional>
#include <string_view>

namespace fallout_new_vegas {
namespace {
std::mutex log_mutex;
std::filesystem::path log_path;
} // namespace

void initialize_diagnostics(const std::filesystem::path& directory) {
    const std::scoped_lock lock(log_mutex);
    log_path = directory / "InputNextFalloutNewVegas.log";
}

void log(std::string_view message) noexcept {
    try {
        const std::scoped_lock lock(log_mutex);
        if (log_path.empty()) {
            return;
        }
        std::ofstream stream(log_path, std::ios::app);
        if (!stream) {
            return;
        }
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        stream << seconds << " " << message << '\n';
    } catch (...) {
        OutputDebugStringA("InputNext Fallout New Vegas: diagnostic logging failed.\n");
    }
}

void log_native_event(fallout_gamebryo::EventKind kind) noexcept {
    using fallout_gamebryo::EventKind;
    switch (kind) {
    case EventKind::shot:
        log("native event delivered: shot");
        break;
    case EventKind::dry_fire:
        log("native event delivered: dry-fire");
        break;
    case EventKind::melee_swing:
        log("native event delivered: melee-swing");
        break;
    case EventKind::hit:
        log("native event delivered: hit");
        break;
    case EventKind::explosion:
        log("native event delivered: explosion");
        break;
    case EventKind::explosion_dealt:
        log("native event delivered: explosion-dealt");
        break;
    case EventKind::thrown:
        log("native event delivered: thrown");
        break;
    case EventKind::vats_select:
        log("native event delivered: vats-select");
        break;
    case EventKind::vats_accept:
        log("native event delivered: vats-accept");
        break;
    case EventKind::aid:
        log("native event delivered: aid");
        break;
    case EventKind::equip:
        log("native event delivered: equip");
        break;
    case EventKind::repair:
        log("native event delivered: repair");
        break;
    case EventKind::craft:
        log("native event delivered: craft");
        break;
    case EventKind::pick_lock:
        log("native event delivered: lockpick-open");
        break;
    case EventKind::break_lockpick:
        log("native event delivered: lockpick-broken");
        break;
    case EventKind::terminal_success:
        log("native event delivered: terminal-hacked");
        break;
    case EventKind::terminal_failure:
        log("native event delivered: terminal-lockout");
        break;
    case EventKind::progress:
        log("native event delivered: progress");
        break;
    }
}

void log_player_reaction(const ReactionState& state,
    const fallout_gamebryo::Observation& observation) noexcept {
    try {
        const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(
            observation.at.time_since_epoch());
        log(std::format("reaction frame={} ms={} knocked-state={} animation-action={} decoded={}",
            observation.frame,
            time.count(),
            state.knocked_state,
            state.animation_action,
            observation.reaction ? static_cast<std::int32_t>(*observation.reaction) : -1));
    } catch (...) {
        log("reaction diagnostics could not be formatted");
    }
}

void log_stealth_change(const fallout_gamebryo::Observation& observation,
    const std::optional<fallout_gamebryo::Observation>& previous) noexcept {
    const auto score = observation.stealth ? observation.stealth->detection_score : -1;
    const auto previous_score =
        previous && previous->stealth ? previous->stealth->detection_score : -1;
    if (score == previous_score) {
        return;
    }
    try {
        log(std::format("player detection frame={} state={}", observation.frame, score));
    } catch (...) {
        log("player detection diagnostics could not be formatted");
    }
}
} // namespace fallout_new_vegas
