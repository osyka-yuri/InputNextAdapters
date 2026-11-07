#pragma once

#include "native/translators.hpp"

#include <skyrim_adapter/model.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace skyrim_adapter::native {
namespace detail {
enum class VibrationPreference : std::uint8_t { missing, invalid, disabled, enabled };

struct MenuReadDiagnostics {
    bool clear{true};
    MenuBlockReason reason{MenuBlockReason::clear};
    std::array<char, menu_name_capacity> name{};
    std::size_t name_length{};
    std::uint32_t flags{};

    friend bool operator==(const MenuReadDiagnostics&, const MenuReadDiagnostics&) = default;
};

struct ObservationDiagnostics {
    MenuReadDiagnostics menu{};
    VibrationPreference vibration{VibrationPreference::missing};
};
} // namespace detail

// Call only from an SKSE game task. The menu snapshot contains copied names and no game pointers.
[[nodiscard]] Observation read_observation(MonoTime sampled_at,
    bool focused,
    const detail::MenuSnapshot& menu_snapshot,
    detail::ObservationDiagnostics* diagnostics = nullptr) noexcept;

// Resolves source IDs only on the game task and preserves the callback's scalar event envelope.
[[nodiscard]] std::optional<Action>
resolve_action(const RawEvent& event, const Observation& observation, MonoTime now) noexcept;
} // namespace skyrim_adapter::native
