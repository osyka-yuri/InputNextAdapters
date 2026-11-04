#pragma once

#include "skyrim_adapter/body_waveform.hpp"

#include <chrono>
#include <cstdint>
#include <string_view>

namespace skyrim_adapter::body_waveform_tests {

void check(bool condition, std::string_view message);

[[nodiscard]] int failure_count() noexcept;

MonoTime at(std::int64_t milliseconds);

BodyCue
cue(BodyCueKind kind, std::uint64_t id, std::chrono::milliseconds duration, float scale = 1.0F);

} // namespace skyrim_adapter::body_waveform_tests
