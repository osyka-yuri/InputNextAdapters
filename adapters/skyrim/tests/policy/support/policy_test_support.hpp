#pragma once

#include "skyrim_adapter/model.hpp"

#include <cstdint>
#include <string_view>

namespace skyrim_adapter::policy_test {

[[nodiscard]] MonoTime at(std::int64_t milliseconds);
[[nodiscard]] Observation observation(MonoTime sampled_at);
[[nodiscard]] Action
action(EventKind kind, Hand hand, MonoTime observed_at, std::uint64_t sequence);
[[nodiscard]] bool finite_legal(const EffectFrame& frame);
[[nodiscard]] Observation world_observation(MonoTime sampled_at);
[[nodiscard]] WorldObservation& world(Observation& obs);

} // namespace skyrim_adapter::policy_test

namespace skyrim_adapter {
using policy_test::action;
using policy_test::at;
using policy_test::finite_legal;
using policy_test::observation;
using policy_test::world;
using policy_test::world_observation;
} // namespace skyrim_adapter
