#include "native_combat.hpp"

#include "event_reducer.hpp"
#include "nvse_sdk.hpp"

#include <fallout_gamebryo/model.hpp>

#include <nvse/GameProcess.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace fallout_new_vegas {
namespace {
using fallout_gamebryo::HitOutcome;
using fallout_gamebryo::PlayerReaction;
using fallout_gamebryo::WeaponClass;

// ActorHitData flags from the JIP LN NVSE game headers.
constexpr std::uint32_t hit_flag_target_is_blocking = 0x1U;
constexpr std::uint32_t hit_flag_explosion = 0x2000U;
constexpr std::uint32_t fists_form_id = 0x000001F4U;

[[nodiscard]] HitOutcome outgoing_hit(std::uint32_t flags) noexcept {
    constexpr auto sneak_critical =
        static_cast<std::uint32_t>(ActorHitData::kHitData_IsSneakAttackCritical);
    constexpr auto critical = static_cast<std::uint32_t>(ActorHitData::kHitData_IsCritical);
    if ((flags & sneak_critical) != 0U) {
        return HitOutcome::sneak_critical;
    }
    return (flags & critical) != 0U ? HitOutcome::critical : HitOutcome::ordinary;
}

[[nodiscard]] std::uintptr_t
hit_identity(const ActorHitData& hit, const HitForms& forms, bool explosion) noexcept {
    // Copy the identity bytes because the engine can store different pointer types here.
    std::uintptr_t reference{};
    static_assert(sizeof(reference) == sizeof(void*));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    std::memcpy(&reference, static_cast<const void*>(&hit.projectile), sizeof(reference));
    // The union can contain the attacking actor for the stock Fists weapon.
    if (!explosion && forms.weapon == fists_form_id && forms.weapon_class == WeaponClass::unarmed &&
        reference == reinterpret_cast<std::uintptr_t>(hit.source)) {
        return 0U;
    }
    return reference;
}

[[nodiscard]] HitOutcome incoming_hit(const ActorHitData& hit,
    const HitForms& forms,
    bool explosion,
    std::uintptr_t reference) noexcept {
    constexpr auto cripple_part = static_cast<std::uint32_t>(ActorHitData::kHitData_CripplePart);
    if ((hit.flags & cripple_part) != 0U) {
        return HitOutcome::crippled;
    }
    const bool melee_contact =
        hit.weapon != nullptr && forms.weapon != 0U &&
        (forms.weapon_class == WeaponClass::melee || forms.weapon_class == WeaponClass::unarmed);
    const bool blocked = (hit.flags & hit_flag_target_is_blocking) != 0U;
    return blocked && !explosion && reference == 0U && hit.source != nullptr &&
                   forms.source != 0U && forms.source != forms.target && melee_contact
               ? HitOutcome::blocked
               : HitOutcome::ordinary;
}
} // namespace

std::optional<NativeSignal>
read_hit_signal(const ActorHitData& hit, const HitForms& forms, bool incoming) noexcept {
    if (!std::isfinite(hit.healthDmg)) {
        return std::nullopt;
    }
    const bool explosion = (hit.flags & hit_flag_explosion) != 0U;
    const auto reference = hit_identity(hit, forms, explosion);
    return NativeSignal{.kind = NativeSignalKind::hit_received,
        .source = forms.source,
        .target = forms.target,
        .form = incoming ? 0U : forms.weapon,
        .hit_reference = reference,
        .weapon_class = forms.weapon_class,
        .hit_outcome =
            incoming ? incoming_hit(hit, forms, explosion, reference) : outgoing_hit(hit.flags),
        .confirmed_damage = hit.healthDmg > 0.0F,
        .explosion_damage = explosion};
}

std::optional<PlayerReaction> read_player_reaction(const ReactionState& state) noexcept {
    // MiddleHighProcess::KnockedState in the JIP LN NVSE game headers.
    enum class KnockedState : std::uint8_t { steady = 0, down = 1, ragdolled = 2, unconscious = 3 };
    switch (static_cast<KnockedState>(state.knocked_state)) {
    case KnockedState::steady:
        return state.animation_action == HighProcess::kAnimAction_Stagger
                   ? PlayerReaction::staggered
                   : PlayerReaction::steady;
    case KnockedState::down:
    case KnockedState::ragdolled:
    case KnockedState::unconscious:
        return PlayerReaction::knocked_down;
    default:
        return std::nullopt;
    }
}
} // namespace fallout_new_vegas
