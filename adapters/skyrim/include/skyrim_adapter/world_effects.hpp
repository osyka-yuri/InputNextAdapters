#pragma once

#include "model.hpp"

#include <optional>

namespace skyrim_adapter {
enum class WorldRumbleShape : std::uint8_t { steady, fade, double_pulse, double_tap };

struct WorldRumble {
    WorldRumbleShape shape{WorldRumbleShape::fade};
    float low{};
    float high{};
    float second_low{};
    float second_high{};
    std::uint32_t duration_ms{};
    std::uint32_t peak_ms{};
    std::uint32_t gap_ms{};
    std::uint32_t second_duration_ms{};
    int priority{};
};

struct WorldEffect {
    BodyCue body{};
    WorldRumble rumble{};
};

class WorldEffects {
public:
    void reset() noexcept;
    [[nodiscard]] std::optional<WorldEffect> step(const Observation& observation,
        float body_scale,
        MonoTime now,
        const EffectSettings& settings = {}) noexcept;

private:
    void remember(const WorldObservation& world, MonoTime sampled_at, FormId player_id) noexcept;
    void start_fall_if_airborne(const WorldObservation& world) noexcept;
    void establish_baseline(const Observation& observation, const WorldObservation& world) noexcept;
    void offer_state_edges(const Observation& observation,
        const WorldObservation& current,
        float body_scale,
        MonoTime now,
        const EffectSettings& settings,
        std::optional<WorldEffect>& selected) const noexcept;
    void update_stamina(const Observation& observation,
        const WorldObservation& current,
        float body_scale,
        const EffectSettings& settings,
        MonoTime now,
        std::optional<WorldEffect>& selected) noexcept;
    void update_landing(const Observation& observation,
        const WorldObservation& current,
        float body_scale,
        const EffectSettings& settings,
        MonoTime now,
        std::optional<WorldEffect>& selected) noexcept;

    WorldObservation previous_{};
    MonoTime previous_at_;
    FormId player_id_{};
    float fall_apex_z_{};
    bool has_previous_{};
    bool stamina_armed_{};
    bool fall_tracking_{};
};
} // namespace skyrim_adapter
