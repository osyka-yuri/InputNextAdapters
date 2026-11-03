#pragma once

#include <F4SE/Impl/PCH.h>
#include <RE/B/BSTEvent.h>

#include <cstdint>
#include <optional>

namespace RE {
class Actor;
class CurrentRadiationSourceCount;
} // namespace RE

namespace fallout4_adapter::native {
struct StealthFacts {
    bool sneaking{};
    std::int16_t detection_score{};
};

// Optional sources use public Address Library IDs within the checked executable.
// Call only on the admitted native observation/sink-install path.
[[nodiscard]] std::optional<StealthFacts> read_stealth_facts(RE::Actor* actor) noexcept;
[[nodiscard]] bool register_radiation_count_sink(
    RE::BSTEventSink<RE::CurrentRadiationSourceCount>* sink) noexcept;
} // namespace fallout4_adapter::native
