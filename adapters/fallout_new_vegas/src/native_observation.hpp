#pragma once

#include <fallout_gamebryo/model.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace fallout_new_vegas {
namespace actor_movement {
// ActorMover::MovementFlags in the JIP LN NVSE SDK for Fallout New Vegas.
inline constexpr auto direction_mask = 0x0FU;
inline constexpr auto walking = 0x100U;
inline constexpr auto running = 0x200U;
inline constexpr auto sneaking = 0x400U;
inline constexpr auto swimming = 0x800U;
} // namespace actor_movement

[[nodiscard]] constexpr bool is_sneaking(std::uint32_t flags) noexcept {
    return (flags & (actor_movement::sneaking | actor_movement::swimming)) ==
           actor_movement::sneaking;
}

[[nodiscard]] constexpr fallout_gamebryo::Gait gait_from_movement_flags(std::uint32_t flags,
    bool airborne) noexcept {
    using fallout_gamebryo::Gait;
    if (airborne || (flags & actor_movement::direction_mask) == 0U ||
        (flags & actor_movement::swimming) != 0U) {
        return Gait::none;
    }
    if (is_sneaking(flags)) {
        return Gait::sneak;
    }
    if ((flags & actor_movement::running) != 0U) {
        return Gait::run;
    }
    return (flags & actor_movement::walking) != 0U ? Gait::walk : Gait::none;
}

struct ObservationFacts {
    fallout_gamebryo::MonoTime at;
    std::uint64_t epoch{};
    std::uint64_t frame{};
    std::uint32_t player{};
    fallout_gamebryo::Mode mode{fallout_gamebryo::Mode::unavailable};
    fallout_gamebryo::Menu menu{fallout_gamebryo::Menu::none};
    fallout_gamebryo::Weapon weapon{};
    fallout_gamebryo::TriggerSide attack_trigger{fallout_gamebryo::TriggerSide::none};
    fallout_gamebryo::TriggerSide aim_trigger{fallout_gamebryo::TriggerSide::none};
    std::optional<float> health_current{std::nullopt};
    std::optional<float> health_maximum{std::nullopt};
    std::optional<float> action_points_current{std::nullopt};
    std::optional<float> action_points_maximum{std::nullopt};
    std::optional<float> radiation{std::nullopt};
    std::optional<std::uint32_t> radiation_sources{std::nullopt};
    std::optional<fallout_gamebryo::Movement> movement{std::nullopt};
    std::optional<fallout_gamebryo::PlayerReaction> reaction{std::nullopt};
    bool focused{};
    bool in_combat{};
    bool aiming{};
    bool reloading{};
};

[[nodiscard]] inline std::optional<float> normalized_value(const std::optional<float>& current,
    const std::optional<float>& maximum) noexcept {
    if (!current || !maximum || !std::isfinite(*current) || !std::isfinite(*maximum) ||
        *maximum <= 0.0F) {
        return std::nullopt;
    }
    return std::clamp(*current / *maximum, 0.0F, 1.0F);
}

[[nodiscard]] inline fallout_gamebryo::Observation make_observation(
    const ObservationFacts& facts) noexcept {
    fallout_gamebryo::Observation observation{};
    observation.at = facts.at;
    observation.epoch = facts.epoch;
    observation.frame = facts.frame;
    observation.player = facts.player;
    observation.mode = facts.player == 0 ? fallout_gamebryo::Mode::unavailable : facts.mode;
    observation.menu = facts.menu;
    observation.weapon = facts.weapon;
    observation.attack_trigger = facts.attack_trigger;
    observation.aim_trigger = facts.aim_trigger;
    observation.focused = facts.focused;
    observation.in_combat = facts.in_combat;
    observation.aiming = facts.aiming;
    observation.reloading = facts.reloading;

    if (facts.player != 0 && facts.health_current && facts.health_maximum &&
        normalized_value(facts.health_current, facts.health_maximum)) {
        observation.health =
            fallout_gamebryo::LedHealthSample{.current = std::max(0.0F, *facts.health_current),
                .maximum = *facts.health_maximum};
        observation.alive = *facts.health_current > 0.0F;
    }
    observation.action_points_ratio =
        normalized_value(facts.action_points_current, facts.action_points_maximum);
    if (facts.radiation && std::isfinite(*facts.radiation) && *facts.radiation >= 0.0F &&
        *facts.radiation <= 1.0F) {
        observation.radiation = *facts.radiation;
        observation.radiation_sources = facts.radiation_sources;
    }
    if (facts.movement && std::isfinite(facts.movement->height)) {
        observation.movement = facts.movement;
    }
    observation.reaction = facts.player != 0U ? facts.reaction : std::nullopt;
    return observation;
}

[[nodiscard]] inline bool recently_in_terminal(
    const std::optional<fallout_gamebryo::Observation>& observation,
    fallout_gamebryo::MonoTime event_at) noexcept {
    using fallout_gamebryo::Menu;
    using fallout_gamebryo::Mode;
    return observation && observation->mode == Mode::menu && observation->menu == Menu::terminal &&
           observation->focused && observation->alive && observation->player != 0U &&
           event_at >= observation->at &&
           event_at - observation->at <= fallout_gamebryo::snapshot_timeout;
}

[[nodiscard]] inline bool should_cancel_pending_shots(const fallout_gamebryo::Observation& current,
    const std::optional<fallout_gamebryo::Observation>& previous) noexcept {
    const bool combat_mode = current.mode == fallout_gamebryo::Mode::gameplay ||
                             current.mode == fallout_gamebryo::Mode::vats_playback;
    if (!combat_mode || !current.focused || !current.alive || current.reloading) {
        return true;
    }
    return previous &&
           (!previous->focused || !previous->alive || previous->epoch != current.epoch ||
               previous->player != current.player || previous->weapon.id != current.weapon.id ||
               previous->mode != current.mode);
}

class LandingTriggerReadiness final {
public:
    void apply(fallout_gamebryo::Observation& current,
        const std::optional<fallout_gamebryo::Observation>& previous,
        bool waiting_for_special_idle) noexcept {
        if (!previous || !same_drawn_weapon(current, *previous) || !current.movement ||
            !previous->movement || current.movement->cell != previous->movement->cell) {
            reset();
            return;
        }
        if (current.movement->airborne) {
            grounded_at_.reset();
        } else if (previous->movement->airborne && previous->weapon.ready) {
            grounded_at_ = current.at;
        }
        if (current.weapon.ready) {
            if (preserving_) {
                reset();
            }
            return;
        }
        if (!waiting_for_special_idle) {
            reset();
            return;
        }
        // The recovery idle can start a frame after ground contact.
        const bool just_landed =
            grounded_at_ && current.at - *grounded_at_ <= fallout_gamebryo::snapshot_timeout;
        preserving_ = preserving_ || (just_landed && previous->weapon.ready);
        current.weapon.ready = preserving_;
    }

private:
    [[nodiscard]] static bool same_drawn_weapon(const fallout_gamebryo::Observation& current,
        const fallout_gamebryo::Observation& previous) noexcept {
        using fallout_gamebryo::Mode;
        return current.mode == Mode::gameplay && previous.mode == Mode::gameplay &&
               current.focused && previous.focused && current.alive && previous.alive &&
               current.epoch == previous.epoch && current.player == previous.player &&
               current.weapon.id != 0U && current.weapon.id == previous.weapon.id &&
               current.weapon.drawn && previous.weapon.drawn && !current.reloading &&
               !previous.reloading && current.at >= previous.at &&
               current.at - previous.at <= fallout_gamebryo::snapshot_timeout;
    }

    void reset() noexcept {
        grounded_at_.reset();
        preserving_ = false;
    }

    std::optional<fallout_gamebryo::MonoTime> grounded_at_;
    bool preserving_{};
};
} // namespace fallout_new_vegas
