#pragma once

#include "contact_surface.hpp"

#include <fallout4_adapter/model.hpp>

#include <RE/B/BSPointerHandle.h>
#include <RE/H/HitData.h>
#include <RE/T/TESHitEvent.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace fallout4_adapter::native {
using HitFlags = REX::TEnumSet<RE::HitData::Flag, std::uint32_t>;

struct HitParticipants {
    std::uintptr_t player{};
    std::uintptr_t target{};
    std::uintptr_t cause{};
};

[[nodiscard]] inline std::optional<PolicyEvent> normalize_hit_event(HitParticipants participants,
    std::optional<HitFlags> flags,
    std::uint64_t weapon_identity,
    std::optional<float> health_damage = std::nullopt) noexcept {
    if (participants.player == 0) {
        return std::nullopt;
    }

    const bool received = participants.target == participants.player;
    const bool dealt = !received && participants.cause == participants.player;
    if (!received && !dealt) {
        return std::nullopt;
    }

    if (flags &&
        flags->any(RE::HitData::Flag::kPredictDamage, RE::HitData::Flag::kPredictBaseDamage)) {
        return std::nullopt;
    }

    PolicyEvent event{};
    event.kind = received ? EventKind::hit_received : EventKind::hit_dealt;
    event.weapon_identity = weapon_identity;
    constexpr double damage_soft_knee = 50.0;
    if (health_damage && std::isfinite(*health_damage) && *health_damage >= 0.0F) {
        event.health_damage_known = true;
        const auto damage = static_cast<double>(*health_damage);
        // This is a soft-knee normalization in health-damage points, not a target-health fraction.
        event.magnitude =
            static_cast<float>(std::clamp(damage / (damage + damage_soft_knee), 0.0, 1.0));
        event.confirmed_positive_health_damage = received && *health_damage > 0.0F;
    } else {
        event.magnitude = 0.5F;
    }
    if (flags) {
        event.critical =
            flags->any(RE::HitData::Flag::kCritical, RE::HitData::Flag::kCriticalOnDeath);
        event.blocked =
            flags->any(RE::HitData::Flag::kBlocked, RE::HitData::Flag::kBlockWithWeapon);
        event.power_attack =
            flags->any(RE::HitData::Flag::kPowerAttack, RE::HitData::Flag::kTimedBash);
        event.bash = flags->any(RE::HitData::Flag::kBash, RE::HitData::Flag::kTimedBash);
        event.explosion = flags->any(RE::HitData::Flag::kExplosion);
        event.crippled_limb = flags->any(RE::HitData::Flag::kCrippleLimb,
            RE::HitData::Flag::kDismemberLimb,
            RE::HitData::Flag::kExplodeLimb);
        // Do not infer this from the current weapon: delayed projectile hits can follow a weapon change.
        event.melee = flags->any(RE::HitData::Flag::kMeleeAttack);
    }
    return event;
}

struct HitPlayer {
    std::uintptr_t address{};
    std::uint32_t handle{};
    std::uint64_t weapon_identity{};
};

[[nodiscard]] inline HitParticipants actor_hit_participants(std::uint32_t player_handle,
    RE::ActorHandle target,
    RE::ActorHandle aggressor) noexcept {
    return {.player = player_handle,
        .target = target.get_handle(),
        .cause = aggressor.get_handle()};
}

[[nodiscard]] inline std::optional<PolicyEvent> copy_hit_event(const RE::TESHitEvent& event,
    const HitPlayer& player) noexcept {
    if (event.usesHitData) {
        // The extended sender leaves the outer target and cause empty.
        return normalize_hit_event(
            actor_hit_participants(player.handle, event.hitData.target, event.hitData.aggressor),
            event.hitData.flags,
            player.weapon_identity,
            event.hitData.healthDamage);
    }
    auto copied =
        normalize_hit_event({.player = player.address,
                                .target = reinterpret_cast<std::uintptr_t>(event.target.get()),
                                .cause = reinterpret_cast<std::uintptr_t>(event.cause.get())},
            std::nullopt,
            player.weapon_identity);
    if (copied && copied->kind == EventKind::hit_dealt) {
        copied->surface = projectile_surface(event.projectileFormID, event.material);
    }
    return copied;
}
} // namespace fallout4_adapter::native
