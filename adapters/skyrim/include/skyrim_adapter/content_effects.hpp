#pragma once

#include "content_observation.hpp"
#include "model.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace skyrim_adapter {
enum class ContentCueKind : std::uint8_t { cold, hunger, exhaustion, transformation };

struct ContentPulse {
    ContentCueKind kind{ContentCueKind::cold};
    BeastForm beast{BeastForm::none};
    unsigned stage{};
    MonoTime started_at;
    std::uint32_t duration_ms{};
    int priority{};
    float low{};
    float high{};
};

class ContentEffects {
public:
    void reset() noexcept;
    [[nodiscard]] std::array<std::optional<ContentPulse>, 4> step(
        const ContentObservation& observation,
        FormId player,
        MonoTime sampled_at,
        MonoTime now) noexcept;

private:
    struct NeedState {
        std::optional<unsigned> stage;
    };

    std::array<NeedState, 3> needs_{};
    std::optional<BeastForm> previous_beast_;
    MonoTime previous_sampled_at_;
    FormId player_{};
    bool has_sample_{};
};
} // namespace skyrim_adapter
