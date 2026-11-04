#pragma once

#include "skyrim_adapter/model.hpp"

#include <cstdint>
#include <vector>

namespace skyrim_adapter::effect_settings_tests {

MonoTime at(std::int64_t milliseconds);

Observation observation(MonoTime sampled_at);

Action action(EventKind kind, Hand hand, MonoTime observed_at, std::uint64_t sequence);

float peak(const std::vector<float>& samples);

} // namespace skyrim_adapter::effect_settings_tests
