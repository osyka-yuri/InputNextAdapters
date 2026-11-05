#pragma once

#include <skyrim_adapter/model.hpp>

namespace skyrim_adapter::coordinator_detail {
inline bool fresh(MonoTime observed, MonoTime now) noexcept {
    return observed <= now && now - observed <= observation_max_age;
}

inline bool hard_observation_valid(const Observation& observation) noexcept {
    return observation.available && observation.alive && observation.focused &&
           observation.player_id != 0;
}
} // namespace skyrim_adapter::coordinator_detail
