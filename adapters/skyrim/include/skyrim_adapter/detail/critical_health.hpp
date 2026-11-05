#pragma once

#include "skyrim_adapter/model.hpp"

#include <optional>

namespace skyrim_adapter::detail {

class CriticalHealthHeartbeat {
public:
    void reset() noexcept;
    [[nodiscard]] std::optional<BodyCue> update(const Observation& observation,
        MonoTime now,
        bool body_observation_fresh,
        const EffectSettings& settings,
        float rumble_scale) noexcept;

private:
    bool baseline_set_{};
    bool active_{};
    MonoTime next_heartbeat_at_;
};

} // namespace skyrim_adapter::detail
