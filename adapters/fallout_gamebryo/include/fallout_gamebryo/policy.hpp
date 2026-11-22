#pragma once

#include <fallout_gamebryo/detail/body_effects.hpp>
#include <fallout_gamebryo/detail/trigger_effects.hpp>
#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace fallout_gamebryo {
inline constexpr auto event_max_age = std::chrono::milliseconds{150};

class Policy final {
public:
    explicit Policy(const Settings& settings) noexcept;

    void reset() noexcept;
    void reset_effects() noexcept;
    [[nodiscard]] inputnext::adapters::OutputFrame
    update(const Observation& observation, std::span<const Event> events, MonoTime now) noexcept;

private:
    struct ResourceState {
        float health_ratio{};
        float action_points_ratio{};
        float radiation{};
        std::uint32_t radiation_sources{};
        bool health_valid{};
        bool action_points_valid{};
        bool radiation_valid{};
    };

    void clear_feedback() noexcept;
    [[nodiscard]] bool prepare_observation(const Observation& observation, MonoTime now) noexcept;
    void update_resources(const Observation& observation,
        MonoTime now,
        bool confirmed_incoming_damage) noexcept;
    void update_resource_edges(const ResourceState& previous,
        const ResourceState& current,
        MonoTime now,
        bool confirmed_incoming_damage) noexcept;
    void update_landing(const Observation& observation, MonoTime now) noexcept;
    void update_reload(const Observation& observation, MonoTime now) noexcept;
    void update_reaction(const Observation& observation, MonoTime now) noexcept;
    [[nodiscard]] bool duplicate_action(const Event& event, MonoTime now) noexcept;
    void remember_action(const Event& event, MonoTime now) noexcept;
    void update_gait(const Observation& observation, MonoTime now) noexcept;
    void apply_event(const Event& event, const Observation& observation, MonoTime now) noexcept;
    void apply_menu_event(const Event& event, MonoTime now) noexcept;
    void apply_hit_event(const Event& event, const Observation& observation, MonoTime now) noexcept;
    void
    apply_shot_event(const Event& event, const Observation& observation, MonoTime now) noexcept;
    void
    apply_combat_event(const Event& event, const Observation& observation, MonoTime now) noexcept;
    void add_voice(BodyEffect effect,
        MonoTime started_at,
        MonoTime now,
        float strength = 1.0F,
        std::uint64_t action = 0) noexcept;
    void add_cue(BodyEffect effect, MonoTime now) noexcept;

    struct RecentAction {
        std::uint64_t identity{};
        MonoTime received_at;
        std::uint64_t frame{};
        Mode mode{Mode::unavailable};
        Menu menu{Menu::none};
        EventKind kind{EventKind::shot};
        std::uint32_t source{};
        std::uint32_t target{};
        std::uint32_t weapon{};
    };

    struct ReloadProgress {
        std::optional<std::uint32_t> initial_ammo;
        bool magazine_increased{};
    };

    struct GaitProgress {
        struct Velocity {
            double x;
            double y;
            double speed;

            [[nodiscard]] bool follows(const Velocity& previous) const noexcept;
        };

        [[nodiscard]] std::optional<Velocity> velocity_to(const Movement& next,
            MonoTime sampled_at) const noexcept;

        Movement movement;
        MonoTime at;
        std::uint64_t frame{};
        std::optional<Velocity> previous_velocity{std::nullopt};
        bool matches_previous{};
    };

    Settings settings_;
    inputnext::adapters::LedPolicy led_policy_;
    detail::BodyEffects body_;
    detail::TriggerEffects triggers_;
    std::optional<Observation> previous_observation_;
    std::optional<ReloadProgress> reload_progress_;
    std::optional<ResourceState> resources_;
    bool critical_health_latched_{};
    std::array<RecentAction, event_capacity> recent_actions_{};
    std::size_t recent_action_count_{};
    std::optional<float> airborne_apex_;
    std::optional<MonoTime> last_heartbeat_;
    std::optional<MonoTime> last_gait_cue_;
    std::optional<GaitProgress> gait_progress_;
};
} // namespace fallout_gamebryo
