#pragma once

#include <cstdint>
#include <optional>

namespace skyrim_adapter {
enum class BeastForm : std::uint8_t { none, werewolf, vampire_lord };

struct ContentObservation {
    std::optional<BeastForm> beast;
    bool survival_enabled{};
    std::optional<unsigned> cold_stage;
    std::optional<unsigned> hunger_stage;
    std::optional<unsigned> exhaustion_stage;
};
} // namespace skyrim_adapter
