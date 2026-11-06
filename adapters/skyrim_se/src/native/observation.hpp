#pragma once

#include "skyrim_adapter/model.hpp"
#include <inputnext_adapters/led_policy.hpp>

#include <RE/A/ActorState.h>
#include <RE/A/ActorValues.h>
#include <RE/I/INIPrefSettingCollection.h>
#include <RE/M/MagicCaster.h>
#include <RE/M/MagicSystem.h>
#include <RE/T/TESObjectWEAP.h>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace RE {
class Actor;
}

namespace skyrim_adapter::native {
namespace detail {
template <typename Mappings>
[[nodiscard]] bool relocation_id_present(const Mappings& mappings, std::uint64_t id) noexcept {
    return id != 0 && std::ranges::any_of(mappings, [id](const auto& mapping) noexcept {
        return mapping.id == id && mapping.offset != 0;
    });
}

struct ActorSample {
    float health{};
    float stamina{};
    RE::ACTOR_LIFE_STATE life_state{RE::ACTOR_LIFE_STATE::kAlive};
    RE::WEAPON_STATE weapon_state{RE::WEAPON_STATE::kSheathed};
    RE::ATTACK_STATE_ENUM attack_state{RE::ATTACK_STATE_ENUM::kNone};
    bool moving{};
    bool sprinting{};
    bool swimming{};
    bool staggered{};
    bool knocked_down{};
    MovementGait gait{MovementGait::unknown};
};

template <typename Setting>
[[nodiscard]] std::optional<bool> read_boolean_setting(const Setting* setting) noexcept {
    if (!setting || setting->GetType() != Setting::Type::kBool) {
        return std::nullopt;
    }
    return setting->GetBool();
}

// Returns plain values sampled through CommonLib's runtime-aware actor accessors.
[[nodiscard]] ActorSample sample_actor(RE::Actor& actor) noexcept;

[[nodiscard]] constexpr bool knocked_down(RE::KNOCK_STATE_ENUM state) noexcept {
    return state == RE::KNOCK_STATE_ENUM::kDown;
}

[[nodiscard]] constexpr MovementGait movement_gait(
    const RE::ActorState::ActorState1& state) noexcept {
    const bool moving =
        state.movingBack || state.movingForward || state.movingRight || state.movingLeft;
    if (!moving) {
        return MovementGait::unknown;
    }
    if (state.sprinting) {
        return MovementGait::sprinting;
    }
    if (state.sneaking) {
        return MovementGait::sneaking;
    }
    if (state.running) {
        return MovementGait::running;
    }
    if (state.walking) {
        return MovementGait::walking;
    }
    return MovementGait::unknown;
}
} // namespace detail

// Call only from an SKSE game task; returned values contain no RE pointers and retain no state.
Observation read_observation(MonoTime started_at,
    bool focused,
    const inputnext::adapters::LedSettings& led_settings) noexcept;
// Resolves only event source IDs and preserves the raw event envelope. Native lookup failures propagate to the
// scheduled-task boundary, which retains the owner inert.
std::optional<Action>
resolve_action(const RawEvent& event, const Observation& observation, MonoTime now);

namespace detail {
struct SpellEvidence {
    RE::ActorValue primary_av{RE::ActorValue::kNone};
    RE::ActorValue resist_av{RE::ActorValue::kNone};
    bool recovers{};
    bool ward_keyword{};
    bool fire_keyword{};
    bool frost_keyword{};
    bool shock_keyword{};
    bool restore_health{};
};

[[nodiscard]] constexpr BowPhase bow_phase(RE::ATTACK_STATE_ENUM state) noexcept {
    switch (state) {
    case RE::ATTACK_STATE_ENUM::kBowDraw:
        return BowPhase::draw;
    case RE::ATTACK_STATE_ENUM::kBowAttached:
        return BowPhase::attached;
    case RE::ATTACK_STATE_ENUM::kBowDrawn:
        return BowPhase::drawn;
    case RE::ATTACK_STATE_ENUM::kBowReleasing:
        return BowPhase::releasing;
    case RE::ATTACK_STATE_ENUM::kBowReleased:
        return BowPhase::released;
    case RE::ATTACK_STATE_ENUM::kBowNextAttack:
        return BowPhase::next_attack;
    case RE::ATTACK_STATE_ENUM::kBowFollowThrough:
        return BowPhase::follow_through;
    case RE::ATTACK_STATE_ENUM::kNone:
    case RE::ATTACK_STATE_ENUM::kDraw:
    case RE::ATTACK_STATE_ENUM::kSwing:
    case RE::ATTACK_STATE_ENUM::kHit:
    case RE::ATTACK_STATE_ENUM::kNextAttack:
    case RE::ATTACK_STATE_ENUM::kFollowThrough:
    case RE::ATTACK_STATE_ENUM::kBash:
    case RE::ATTACK_STATE_ENUM::kFire:
    case RE::ATTACK_STATE_ENUM::kFiring:
    case RE::ATTACK_STATE_ENUM::kFired:
        return BowPhase::none;
    }
    return BowPhase::unknown;
}

[[nodiscard]] constexpr CastPhase cast_phase(RE::MagicCaster::State state) noexcept {
    switch (state) {
    case RE::MagicCaster::State::kNone:
        return CastPhase::none;
    case RE::MagicCaster::State::kReady:
        return CastPhase::ready;
    case RE::MagicCaster::State::kCharging:
        return CastPhase::charging;
    case RE::MagicCaster::State::kCasting:
        return CastPhase::casting;
    case RE::MagicCaster::State::kUnk01:
    case RE::MagicCaster::State::kUnk02:
    case RE::MagicCaster::State::kUnk04:
    case RE::MagicCaster::State::kUnk07:
    case RE::MagicCaster::State::kUnk08:
    case RE::MagicCaster::State::kUnk09:
        return CastPhase::unknown;
    }
    return CastPhase::unknown;
}

[[nodiscard]] constexpr CastMode cast_mode(RE::MagicSystem::CastingType mode,
    bool is_scroll = false) noexcept {
    switch (mode) {
    case RE::MagicSystem::CastingType::kFireAndForget:
        return CastMode::fire_and_forget;
    case RE::MagicSystem::CastingType::kConcentration:
        return CastMode::concentration;
    case RE::MagicSystem::CastingType::kConstantEffect:
    case RE::MagicSystem::CastingType::kScroll:
        return is_scroll ? CastMode::fire_and_forget : CastMode::unknown;
    }
    return CastMode::unknown;
}

[[nodiscard]] constexpr WeaponClass weapon_class(RE::WEAPON_TYPE type) noexcept {
    switch (type) {
    case RE::WeaponTypes::kHandToHandMelee:
        return WeaponClass::unarmed;
    case RE::WeaponTypes::kOneHandSword:
        return WeaponClass::sword;
    case RE::WeaponTypes::kOneHandDagger:
        return WeaponClass::dagger;
    case RE::WeaponTypes::kOneHandAxe:
        return WeaponClass::axe;
    case RE::WeaponTypes::kOneHandMace:
        return WeaponClass::mace;
    case RE::WeaponTypes::kTwoHandSword:
    case RE::WeaponTypes::kTwoHandAxe:
        return WeaponClass::two_handed;
    case RE::WeaponTypes::kBow:
        return WeaponClass::bow;
    case RE::WeaponTypes::kCrossbow:
        return WeaponClass::crossbow;
    case RE::WeaponTypes::kStaff:
        return WeaponClass::none;
    case RE::WeaponTypes::kTotal:
        return WeaponClass::unknown;
    }
    return WeaponClass::unknown;
}

[[nodiscard]] constexpr bool melee_weapon(WeaponClass type) noexcept {
    switch (type) {
    case WeaponClass::unarmed:
    case WeaponClass::dagger:
    case WeaponClass::sword:
    case WeaponClass::axe:
    case WeaponClass::mace:
    case WeaponClass::two_handed:
        return true;
    case WeaponClass::none:
    case WeaponClass::bow:
    case WeaponClass::crossbow:
    case WeaponClass::unknown:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr SpellKind spell_kind(const SpellEvidence& evidence) noexcept {
    const unsigned elemental_matches =
        static_cast<unsigned>(
            evidence.fire_keyword || evidence.resist_av == RE::ActorValue::kResistFire) +
        static_cast<unsigned>(
            evidence.frost_keyword || evidence.resist_av == RE::ActorValue::kResistFrost) +
        static_cast<unsigned>(
            evidence.shock_keyword || evidence.resist_av == RE::ActorValue::kResistShock);

    if (elemental_matches > 1) {
        return SpellKind::generic;
    }
    if (evidence.ward_keyword && elemental_matches == 0) {
        return SpellKind::ward;
    }
    if (evidence.ward_keyword) {
        return SpellKind::generic;
    }
    if (evidence.fire_keyword || evidence.resist_av == RE::ActorValue::kResistFire) {
        return SpellKind::fire;
    }
    if (evidence.frost_keyword || evidence.resist_av == RE::ActorValue::kResistFrost) {
        return SpellKind::frost;
    }
    if (evidence.shock_keyword || evidence.resist_av == RE::ActorValue::kResistShock) {
        return SpellKind::shock;
    }
    if (evidence.restore_health ||
        (evidence.recovers && evidence.primary_av == RE::ActorValue::kHealth)) {
        return SpellKind::healing;
    }
    return SpellKind::generic;
}

[[nodiscard]] constexpr bool menu_suppresses_effects(bool on_stack,
    bool always_open,
    bool pauses_game,
    bool modal,
    bool inventory,
    bool application,
    bool uses_context,
    bool uses_cursor,
    bool freeze_frame) noexcept {
    return on_stack && !always_open &&
           (pauses_game || modal || inventory || application || uses_context || uses_cursor ||
               freeze_frame);
}

[[nodiscard]] constexpr bool caster_matches_equipped(EquipmentKind equipment,
    FormId equipped_id,
    FormId current_spell_id,
    FormId staff_enchantment_id = 0) noexcept {
    if (equipped_id == 0 || current_spell_id == 0) {
        return false;
    }
    if (equipment == EquipmentKind::spell || equipment == EquipmentKind::scroll) {
        return current_spell_id == equipped_id;
    }
    return equipment == EquipmentKind::staff && staff_enchantment_id != 0 &&
           current_spell_id == staff_enchantment_id;
}

[[nodiscard]] constexpr bool staff_action_matches_source(FormId event_source_id,
    FormId staff_id,
    FormId enchantment_id) noexcept {
    return event_source_id != 0 && event_source_id == staff_id && enchantment_id != 0;
}
} // namespace detail
} // namespace skyrim_adapter::native
