#pragma once

#include "effect_recipes.hpp"
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>

#include <inputnext_adapters/body_program.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>

namespace fallout4_adapter::policy_tests {

template <typename T>
[[nodiscard]] const T& checked_optional(const std::optional<T>& value) {
    if (!value.has_value()) {
        throw std::logic_error("expected an engaged optional in a Fallout 4 policy test");
    }
    return *value;
}

[[nodiscard]] MonoTime at(std::int64_t milliseconds) noexcept;
[[nodiscard]] Observation gameplay_at(std::int64_t milliseconds) noexcept;
[[nodiscard]] PolicyEvent
event(EventKind kind, MonoTime arrived_at, std::uint64_t sequence) noexcept;

[[nodiscard]] std::unique_ptr<OutputFrame> update_on_heap(Policy& policy,
    const Observation& observation,
    std::span<const PolicyEvent> events,
    MonoTime now);

[[nodiscard]] std::size_t voice_count(const OutputFrame& frame) noexcept;
[[nodiscard]] std::size_t cue_voice_count(const OutputFrame& frame, detail::Cue cue) noexcept;
[[nodiscard]] std::uint64_t cue_sequence(const OutputFrame& frame, detail::Cue cue) noexcept;
[[nodiscard]] const inputnext::adapters::BodyVoice* find_voice(const OutputFrame& frame,
    detail::Cue cue) noexcept;

[[nodiscard]] int quantized_field(float value, int maximum) noexcept;

} // namespace fallout4_adapter::policy_tests
