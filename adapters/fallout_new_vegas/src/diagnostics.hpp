#pragma once

#include <fallout_gamebryo/model.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

namespace fallout_new_vegas {
struct ReactionState;

void initialize_diagnostics(const std::filesystem::path& directory);
void log(std::string_view message) noexcept;
void log_native_event(fallout_gamebryo::EventKind kind) noexcept;
void log_player_reaction(const ReactionState& state,
    const fallout_gamebryo::Observation& observation) noexcept;
void log_stealth_change(const fallout_gamebryo::Observation& observation,
    const std::optional<fallout_gamebryo::Observation>& previous) noexcept;
} // namespace fallout_new_vegas
