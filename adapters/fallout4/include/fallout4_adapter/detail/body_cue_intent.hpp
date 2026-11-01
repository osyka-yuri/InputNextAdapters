#pragma once

#include "fallout4_adapter/model.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace fallout4_adapter::detail {

struct BodyCueIntent {
    BodyEffect effect{};
    MonoTime started_at;
    float intensity{1.0F};
    std::optional<int> priority_override;
    float duration_multiplier{1.0F};
};

struct BodyCueBatch {
    std::array<BodyCueIntent, 3> values{};
    std::size_t count{};

    void append(BodyCueIntent cue) noexcept {
        if (count < values.size()) {
            values[count++] = cue;
        }
    }
};

} // namespace fallout4_adapter::detail
