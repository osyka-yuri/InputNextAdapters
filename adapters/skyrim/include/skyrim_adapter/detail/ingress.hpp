#pragma once

#include <skyrim_adapter/model.hpp>

namespace skyrim_adapter::detail {
struct FootstepAttribution {
    FormId actor_id{};
    FormId mount_id{};
    MovementGait gait{MovementGait::unknown};
    bool mounted{};
};

[[nodiscard]] inline bool observation_usable_at(const Observation& observation,
    MonoTime now) noexcept {
    return observation.available && observation.alive && observation.focused &&
           observation.menus_clear && observation.player_id != 0 && observation.sampled_at <= now &&
           now - observation.sampled_at <= observation_max_age;
}

[[nodiscard]] inline bool event_fresh_at(MonoTime observed_at, MonoTime now) noexcept {
    return observed_at <= now && now - observed_at <= event_max_age;
}

// Enqueue uses the latest observation that predates the native callback.
[[nodiscard]] inline bool footstep_enqueue_attributable(const Observation& observation,
    FormId actor_id,
    MonoTime callback_at) noexcept {
    if (actor_id == 0 || observation.player_id == 0 || !observation.available ||
        observation.sampled_at > callback_at ||
        callback_at - observation.sampled_at > observation_max_age) {
        return false;
    }
    if (actor_id == observation.player_id) {
        return observation.world && !observation.world->mounted;
    }
    return observation.world && observation.world->mounted && observation.world->mount_id != 0 &&
           actor_id == observation.world->mount_id;
}

[[nodiscard]] inline std::optional<FootstepAttribution> current_footstep_attribution(
    const Observation& observation,
    FormId actor_id,
    MonoTime now) noexcept {
    if (actor_id == 0 || !observation_usable_at(observation, now) || !observation.world) {
        return std::nullopt;
    }

    const WorldObservation& world = *observation.world;
    if (actor_id == observation.player_id) {
        if (world.mounted || world.gait == MovementGait::unknown || world.swimming ||
            world.airborne) {
            return std::nullopt;
        }
        return FootstepAttribution{
            .actor_id = actor_id,
            .mount_id = 0,
            .gait = world.gait,
            .mounted = false,
        };
    }
    if (!world.mounted || world.mount_id == 0 || actor_id != world.mount_id ||
        world.mount_gait == MovementGait::unknown || world.mount_swimming || world.mount_airborne) {
        return std::nullopt;
    }
    return FootstepAttribution{
        .actor_id = actor_id,
        .mount_id = world.mount_id,
        .gait = world.mount_gait,
        .mounted = true,
    };
}

[[nodiscard]] inline bool source_less_empty_hand_swing(const RawEvent& event,
    const Observation& observation,
    MonoTime now) noexcept {
    if (event.kind != EventKind::weapon_swing || event.source_id != 0 || event.actor_id == 0 ||
        event.actor_id != observation.player_id ||
        (event.hand != Hand::left && event.hand != Hand::right) ||
        !observation_usable_at(observation, now) || !event_fresh_at(event.observed_at, now)) {
        return false;
    }

    const std::size_t hand_index = event.hand == Hand::left ? 0U : 1U;
    const HandObservation& hand = observation.hands[hand_index];
    return hand.equipment == EquipmentKind::none && hand.equipped_id == 0 &&
           hand.caster_source_id == 0 && hand.weapon == WeaponClass::none;
}

[[nodiscard]] inline bool action_attribution_matches(const Action& action,
    const Observation& observation,
    MonoTime now) noexcept {
    // Hand-built policy fixtures carry no native attribution snapshot.
    if (action.attributed_player_id == 0) {
        return true;
    }
    if (action.attributed_player_id != observation.player_id ||
        !observation_usable_at(observation, now) ||
        !event_fresh_at(action.event.observed_at, now)) {
        return false;
    }

    if (action.event.kind == EventKind::hit && action.incoming) {
        return action.event.target_id == observation.player_id;
    }
    if (action.event.kind == EventKind::footstep) {
        const auto current = current_footstep_attribution(observation, action.event.actor_id, now);
        return current && current->gait == action.gait &&
               current->mounted == action.mount_attributed &&
               (!current->mounted || current->mount_id == action.attributed_mount_id);
    }
    if (action.event.actor_id != observation.player_id) {
        return false;
    }
    if (action.event.kind == EventKind::weapon_swing && action.event.source_id == 0 &&
        action.source_known && action.weapon == WeaponClass::unarmed) {
        return source_less_empty_hand_swing(action.event, observation, now);
    }
    return true;
}
} // namespace skyrim_adapter::detail
