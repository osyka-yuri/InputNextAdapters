#include "observation.hpp"
#include "led_bindings.hpp"
#include "radiation_source.hpp"
#include "ui_capture.hpp"

#include "fallout4_adapter/model.hpp"

#include <inputnext_adapters/led_policy.hpp>

#include <RE/A/ACTOR_VALUE_MODIFIER.h>
#include <RE/A/Actor.h>
#include <RE/A/ActorValue.h>
#include <RE/A/ActorValueInfo.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/B/BSSpinLock.h>

// CommonLib's controller header needs the complete physics-system type first.
#include <RE/H/hkBool.h> // IWYU pragma: keep

#include <RE/B/bhkPhysicsSystem.h> // IWYU pragma: keep

#include <RE/B/bhkCharacterController.h>
#include <RE/E/EquippedItem.h>
#include <RE/G/GUN_STATE.h>
#include <RE/H/hknpCharacterState.h>
#include <RE/M/MiddleHighProcessData.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/PowerArmor.h>
#include <RE/T/TESFormUtil.h> // IWYU pragma: keep
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/W/WEAPON_FLAGS.h>
#include <RE/W/WEAPON_STATE.h>
#include <RE/W/WEAPON_TYPE.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace fallout4_adapter::native {
namespace {
[[nodiscard]] std::optional<LandingObservation> read_landing(
    const RE::PlayerCharacter& player) noexcept {
    const auto* process = player.currentProcess;
    const auto* middle_high = process ? process->middleHigh : nullptr;
    const auto* controller = middle_high ? middle_high->charController.get() : nullptr;
    const auto* cell = player.GetParentCell();
    if (!controller || !cell || player.IsSwimming()) {
        return std::nullopt;
    }
    using State = RE::hknpCharacterState::hknpCharacterStateType;
    const auto state = controller->context.m_currentState;
    if (state != State::kOnGround && state != State::kJumping && state != State::kInAir) {
        return std::nullopt;
    }
    const auto position = player.GetPosition();
    return LandingObservation{
        .player_id = player.GetFormID(),
        .cell_id = cell->GetFormID(),
        .x = position.x,
        .y = position.y,
        .z = position.z,
        .airborne = state != State::kOnGround,
    };
}

[[nodiscard]] RE::GUN_STATE read_gun_state(const RE::PlayerCharacter& player) noexcept {
    return static_cast<RE::GUN_STATE>(gun_state_bits(static_cast<std::int32_t>(player.gunState)));
}

[[nodiscard]] bool ui_snapshot_is_current(std::uint64_t expected_generation,
    const UiScalarSnapshot& snapshot,
    const UiProjection& projection) noexcept {
    return snapshot.valid && ui_generation_matches(expected_generation, snapshot.generation) &&
           projection.valid &&
           ui_generation_matches(expected_generation, ui_transition_generation());
}

[[nodiscard]] WeaponKind weapon_kind(RE::WEAPON_TYPE type) noexcept {
    switch (type) {
    case RE::WEAPON_TYPE::kHandToHand:
        return WeaponKind::unarmed;
    case RE::WEAPON_TYPE::kOneHandSword:
    case RE::WEAPON_TYPE::kOneHandDagger:
    case RE::WEAPON_TYPE::kOneHandAxe:
    case RE::WEAPON_TYPE::kOneHandMace:
        return WeaponKind::melee;
    case RE::WEAPON_TYPE::kTwoHandSword:
    case RE::WEAPON_TYPE::kTwoHandAxe:
        return WeaponKind::two_handed_melee;
    case RE::WEAPON_TYPE::kGun:
        return WeaponKind::firearm;
    case RE::WEAPON_TYPE::kGrenade:
    case RE::WEAPON_TYPE::kMine:
        return WeaponKind::explosive;
    case RE::WEAPON_TYPE::kNone:
    case RE::WEAPON_TYPE::kBow:
    case RE::WEAPON_TYPE::kStaff:
        return WeaponKind::unsupported;
    }
    return WeaponKind::unsupported;
}

[[nodiscard]] bool has_weapon_keyword(const RE::TESObjectWEAP& weapon,
    const RE::TESObjectWEAP::InstanceData& data,
    std::string_view keyword) noexcept {
    const auto* base_keywords = static_cast<const RE::BGSKeywordForm*>(&weapon);
    const auto* keywords = select_weapon_keyword_form(base_keywords, data.keywords);
    return (keywords != nullptr) && keywords->HasKeywordString(keyword);
}

[[nodiscard]] bool has_weapon_keyword_group(const RE::TESObjectWEAP& weapon,
    const RE::TESObjectWEAP::InstanceData& data,
    WeaponKeywordGroup group) noexcept {
    switch (group) {
    case WeaponKeywordGroup::rotary:
        return has_weapon_keyword(weapon, data, "WeaponTypeMinigun") ||
               has_weapon_keyword(weapon, data, "WeaponTypeGatlingLaser");
    case WeaponKeywordGroup::flame:
        return has_weapon_keyword(weapon, data, "WeaponTypeFlamer");
    case WeaponKeywordGroup::cryo:
        return has_weapon_keyword(weapon, data, "WeaponTypeCryolater");
    case WeaponKeywordGroup::launcher:
        return has_weapon_keyword(weapon, data, "WeaponTypeMissileLauncher") ||
               has_weapon_keyword(weapon, data, "WeaponTypeFatman") ||
               has_weapon_keyword(weapon, data, "WeaponTypeBroadsider");
    case WeaponKeywordGroup::shotgun:
        return has_weapon_keyword(weapon, data, "WeaponTypeShotgun");
    case WeaponKeywordGroup::heavy:
        return has_weapon_keyword(weapon, data, "WeaponTypeHeavyGun");
    case WeaponKeywordGroup::energy:
        return has_weapon_keyword(weapon, data, "WeaponTypeLaser") ||
               has_weapon_keyword(weapon, data, "WeaponTypePlasma") ||
               has_weapon_keyword(weapon, data, "WeaponTypeAlienBlaster");
    case WeaponKeywordGroup::pistol:
        return has_weapon_keyword(weapon, data, "WeaponTypePistol");
    case WeaponKeywordGroup::rifle:
        return has_weapon_keyword(weapon, data, "WeaponTypeRifle");
    }
    return false;
}

[[nodiscard]] WeaponSnapshot snapshot_weapon(const RE::EquippedItem& equipped) noexcept {
    const auto* weapon =
        equipped.item.object ? equipped.item.object->As<RE::TESObjectWEAP>() : nullptr;
    if (!weapon) {
        return {};
    }

    const auto* raw_instance = equipped.item.instanceData.get();
    const auto* instance =
        raw_instance ? static_cast<const RE::TESObjectWEAP::InstanceData*>(raw_instance) : nullptr;
    const auto* data = instance ? instance : &weapon->weaponData;
    const auto kind = weapon_kind(data->type.get());

    std::int8_t projectile_count{};
    if (kind == WeaponKind::firearm && data->rangedData) {
        projectile_count = data->rangedData->numProjectiles;
    }
    const auto family = classify_weapon_family_lazy(
        kind,
        [weapon, data](WeaponKeywordGroup group) noexcept {
            return has_weapon_keyword_group(*weapon, *data, group);
        },
        projectile_count);

    const auto form_id = weapon->GetFormID();
    const auto instance_address = reinterpret_cast<std::uintptr_t>(raw_instance);
    return WeaponSnapshot{
        .identity = weapon_identity(form_id, instance_address),
        .family = family,
        .automatic = kind == WeaponKind::firearm && data->flags.any(RE::WEAPON_FLAGS::kAutomatic),
        .drawn = false,
        .ready = false,
    };
}

void read_equipped_weapon(const RE::PlayerCharacter& player, Observation& observation) noexcept {
    const auto* process = player.currentProcess;
    auto* middle_high = process ? process->middleHigh : nullptr;
    if (!middle_high) {
        return;
    }

    RE::BSAutoLock lock{middle_high->equippedItemsLock};
    const auto& items = middle_high->equippedItems;
    if (items.size() > max_context_entries) {
        return;
    }

    const RE::EquippedItem* selected = nullptr;
    for (const auto& item : items) {
        if (!item.item.object || !item.item.object->As<RE::TESObjectWEAP>()) {
            continue;
        }
        if (!selected || item.equipIndex.index < selected->equipIndex.index) {
            selected = &item;
        }
    }
    if (!selected) {
        return;
    }

    auto snapshot = snapshot_weapon(*selected);
    const auto* weapon = selected->item.object->As<RE::TESObjectWEAP>();
    const auto* raw_instance = selected->item.instanceData.get();
    const auto* instance =
        raw_instance ? static_cast<const RE::TESObjectWEAP::InstanceData*>(raw_instance) : nullptr;
    const auto* data = instance ? instance : &weapon->weaponData;
    const bool weapon_drawn = player.weaponState == RE::WEAPON_STATE::kDrawn ||
                              player.weaponState == RE::WEAPON_STATE::kWantToSheathe ||
                              player.weaponState == RE::WEAPON_STATE::kSheathing;
    snapshot.drawn = weapon_drawn;
    const auto gun_state = read_gun_state(player);
    snapshot.ready = weapon_ready(WeaponReadinessFacts{
        .kind = weapon_kind(data->type.get()),
        .identified = snapshot.identity != 0,
        .fully_drawn = player.weaponState == RE::WEAPON_STATE::kDrawn,
        .reloading = gun_state == RE::GUN_STATE::kReloading,
        .throwing = gun_state == RE::GUN_STATE::kThrowing,
        .blocked = gun_state == RE::GUN_STATE::kBlocked,
    });
    observation.weapon = snapshot;
}

[[nodiscard]] std::optional<std::uint8_t> read_critical_count(
    const RE::PlayerCharacter& player) noexcept {
    constexpr auto max_count = std::numeric_limits<std::uint8_t>::max();
    if (player.maxVATSCriticalCount == 0 || player.vatsCriticalCount > max_count ||
        player.vatsCriticalCount > player.maxVATSCriticalCount) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(player.vatsCriticalCount);
}

[[nodiscard]] std::optional<float> read_actor_value_ratio(const RE::ActorValueOwner& owner,
    const RE::ActorValueInfo* actor_value) noexcept {
    if (!actor_value) {
        return std::nullopt;
    }

    return normalized_actor_value(owner.GetActorValue(*actor_value),
        owner.GetModifier(RE::ACTOR_VALUE_MODIFIER::kDamage, *actor_value));
}

void read_resource_ratios(const RE::PlayerCharacter& player,
    Observation& observation,
    const inputnext::adapters::LedSettings& led_settings) noexcept {
    // This is the engine's Actor Value normalization; Health HUD effects can add presentation on top.
    const auto* actor_values = RE::ActorValue::GetSingleton();
    if (!actor_values) {
        return;
    }

    if (actor_values->health) {
        const float current = player.GetActorValue(*actor_values->health);
        const float damage =
            player.GetModifier(RE::ACTOR_VALUE_MODIFIER::kDamage, *actor_values->health);
        observation.health_ratio = normalized_actor_value(current, damage);
        if (led_settings.enabled && led_settings.health_enabled &&
            observation.mode == Mode::gameplay) {
            observation.led.health = absolute_health_sample(current, damage);
        }
    }
    observation.action_points_ratio = read_actor_value_ratio(player, actor_values->actionPoints);
}
} // namespace

NativeObservation read_observation(MonoTime now,
    bool focused,
    std::uint64_t expected_ui_generation,
    const inputnext::adapters::LedSettings& led_settings) {
    NativeObservation result{};
    result.value.sampled_at = now;
    result.value.mode = Mode::suspended;
    result.ui_generation = expected_ui_generation;
    if (!focused) {
        return result;
    }

    const auto ui_snapshot = capture_ui_scalars(expected_ui_generation);
    const auto ui_projection = project_ui_snapshot(ui_snapshot);
    if (!ui_snapshot_is_current(expected_ui_generation, ui_snapshot, ui_projection)) {
        result.value.mode = Mode::transition;
        return result;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        result.value.mode = Mode::transition;
        return result;
    }

    auto player_handle = RE::PlayerCharacter::GetPlayerHandle();
    result.player_handle = player_handle.get_handle();
    result.player_address = reinterpret_cast<std::uintptr_t>(player);
    result.player_form_id = player->GetFormID();

    ContextFacts facts = ui_projection.context;

    facts.available = result.player_handle != 0 && result.player_form_id != 0;
    facts.alive = !player->IsDead(false);
    facts.focused = focused;
    result.value.mode = classify_mode(facts);
    if (result.value.mode == Mode::gameplay) {
        result.value.landing = read_landing(*player);
    }
    if (led_settings.enabled && result.value.mode == Mode::gameplay) {
        result.value.led.subject_id = result.player_form_id;
        if (led_settings.health_enabled) {
            result.value.led.in_combat = player->IsInCombat();
        }
        if (led_settings.radiation_enabled) {
            result.value.led.radiation_sources = radiation_source_count();
        }
    }

    result.value.power_armor = RE::PowerArmor::PlayerInPowerArmor();
    result.value.sprinting = player->DoGetSprinting();
    const auto gun_state = read_gun_state(*player);
    result.value.aiming =
        gun_state == RE::GUN_STATE::kSighted || gun_state == RE::GUN_STATE::kFireSighted;
    result.value.reloading = gun_state == RE::GUN_STATE::kReloading;
    result.value.blocking = player->wantBlocking || gun_state == RE::GUN_STATE::kBlocked;
    result.value.critical_count = read_critical_count(*player);
    read_equipped_weapon(*player, result.value);
    read_resource_ratios(*player, result.value, led_settings);
    if (led_settings.enabled && led_settings.stealth_enabled &&
        result.value.mode == Mode::gameplay) {
        if (const auto stealth = read_stealth_facts(player)) {
            result.value.led.stealth = inputnext::adapters::LedStealthSample{
                .sneaking = stealth->sneaking,
                .detection_score = stealth->detection_score,
            };
        }
    }

    if (result.value.mode == Mode::gameplay && result.value.weapon.ready) {
        const auto triggers = trigger_assignments(ui_snapshot.attack_key, ui_snapshot.aim_key);
        result.value.attack_trigger = triggers.attack;
        // aim_key is the existing mapped SecondaryAttack action (aim or melee block).
        result.value.aim_trigger = triggers.aim;
    }
    if (!ui_generation_matches(expected_ui_generation, ui_transition_generation())) {
        result = {};
        result.value.sampled_at = now;
        result.value.mode = Mode::transition;
        result.ui_generation = expected_ui_generation;
    }
    return result;
}
} // namespace fallout4_adapter::native
