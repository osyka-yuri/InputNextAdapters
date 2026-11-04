#include "support.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <skyrim_adapter/model.hpp>
#include <vector>

namespace skyrim_adapter::effect_settings_tests {

MonoTime at(std::int64_t milliseconds) {
    return MonoTime{} + std::chrono::milliseconds{milliseconds};
}

Observation observation(MonoTime sampled_at) {
    Observation result{};
    result.sampled_at = sampled_at;
    result.player_id = 1;
    result.available = true;
    result.alive = true;
    result.focused = true;
    result.menus_clear = true;
    result.weapon_drawn = true;
    result.health_ratio = 1.0F;
    return result;
}

Action action(EventKind kind, Hand hand, MonoTime observed_at, std::uint64_t sequence) {
    Action result{};
    result.event.kind = kind;
    result.event.hand = hand;
    result.event.actor_id = 1;
    result.event.source_id = 42;
    result.event.sequence = sequence;
    result.event.observed_at = observed_at;
    return result;
}

float peak(const std::vector<float>& samples) {
    float result = 0.0F;
    for (const float sample : samples) {
        result = std::max(result, std::abs(sample));
    }
    return result;
}

} // namespace skyrim_adapter::effect_settings_tests
