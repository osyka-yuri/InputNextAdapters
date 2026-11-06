#include "observation.hpp"
#include "content_observation.hpp"
#include "spell_keywords.hpp"

#include <inputnext_adapters/led_policy.hpp>
#include <skyrim_adapter/detail/ingress.hpp>
#include <skyrim_adapter/detail/led_observation.hpp>
#include <skyrim_adapter/model.hpp>

#include <RE/A/Actor.h>
#include <RE/A/ActorState.h>
#include <RE/A/ActorValues.h>
#include <RE/B/BSCoreTypes.h>
#include <RE/B/BSFixedString.h>
#include <RE/E/Effect.h>
#include <RE/E/EffectSetting.h>
#include <RE/E/EnchantmentItem.h>
#include <RE/F/FormTraits.h> // IWYU pragma: keep
#include <RE/I/IMenu.h>
#include <RE/I/INIPrefSettingCollection.h>
#include <RE/M/MagicItem.h>
#include <RE/M/MagicSystem.h>
#include <RE/N/NiSmartPointer.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/ProcessLists.h>
#include <RE/S/ScrollItem.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESWorldSpace.h>
#include <RE/U/UI.h>

#include <REL/ID.h>
#include <REL/Offset2ID.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace skyrim_adapter::native {
namespace {
using detail::SpellEvidence;

using BlockingMenuNames = std::array<RE::BSFixedString, 4>;

[[nodiscard]] const BlockingMenuNames* pinned_blocking_menu_names() noexcept {
    try {
        // The plugin module is pinned for the process lifetime. Keep these intern references until process exit so
        // their engine-backed BSFixedString destructors never run during static teardown.
        static const auto* const names = new BlockingMenuNames{
            RE::BSFixedString{"Main Menu"},
            RE::BSFixedString{"Loading Menu"},
            RE::BSFixedString{"LoadWaitSpinner"},
            RE::BSFixedString{"TitleSequence Menu"},
        };
        return names;
    } catch (...) {
        return nullptr;
    }
}

[[nodiscard]] bool is_menu_open(RE::UI& user_interface, const RE::BSFixedString& name) noexcept {
    const auto entry = user_interface.menuMap.find(name);
    const auto* menu = entry != user_interface.menuMap.end() ? entry->second.menu.get() : nullptr;
    return menu != nullptr && menu->OnStack();
}

[[nodiscard]] SpellEvidence spell_evidence(const RE::MagicItem& magic_item) noexcept {
    SpellEvidence evidence{};
    const auto magic_keywords = detail::scan_spell_keywords(magic_item.GetKeywords());
    evidence.ward_keyword = magic_keywords.ward;
    evidence.fire_keyword = magic_keywords.fire;
    evidence.frost_keyword = magic_keywords.frost;
    evidence.shock_keyword = magic_keywords.shock;
    evidence.restore_health = magic_keywords.restore_health;

    if (const auto* effect = magic_item.GetCostliestEffectItem(); effect && effect->baseEffect) {
        const auto& base = *effect->baseEffect;
        evidence.primary_av = base.data.primaryAV;
        evidence.resist_av = base.data.resistVariable;
        evidence.recovers =
            base.data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kRecover);
        if (!(evidence.ward_keyword && evidence.fire_keyword && evidence.frost_keyword &&
                evidence.shock_keyword && evidence.restore_health)) {
            const auto base_keywords = detail::scan_spell_keywords(base.GetKeywords());
            evidence.ward_keyword = evidence.ward_keyword || base_keywords.ward;
            evidence.fire_keyword = evidence.fire_keyword || base_keywords.fire;
            evidence.frost_keyword = evidence.frost_keyword || base_keywords.frost;
            evidence.shock_keyword = evidence.shock_keyword || base_keywords.shock;
            evidence.restore_health = evidence.restore_health || base_keywords.restore_health;
        }
    }

    return evidence;
}

[[nodiscard]] SpellKind classify_spell(const RE::MagicItem& magic_item) noexcept {
    return detail::spell_kind(spell_evidence(magic_item));
}

[[nodiscard]] EquipmentKind equipment_kind(const RE::TESObjectWEAP& weapon) noexcept {
    switch (weapon.GetWeaponType()) {
    case RE::WeaponTypes::kBow:
        return EquipmentKind::bow;
    case RE::WeaponTypes::kCrossbow:
        return EquipmentKind::crossbow;
    case RE::WeaponTypes::kStaff:
        return EquipmentKind::staff;
    case RE::WeaponTypes::kHandToHandMelee:
    case RE::WeaponTypes::kOneHandSword:
    case RE::WeaponTypes::kOneHandDagger:
    case RE::WeaponTypes::kOneHandAxe:
    case RE::WeaponTypes::kOneHandMace:
    case RE::WeaponTypes::kTwoHandSword:
    case RE::WeaponTypes::kTwoHandAxe:
        return EquipmentKind::weapon;
    case RE::WeaponTypes::kTotal:
        return EquipmentKind::unknown;
    }
    return EquipmentKind::unknown;
}

void fill_equipped_hand(HandObservation& hand, RE::TESForm* equipped) noexcept {
    if (!equipped) {
        hand.equipment = EquipmentKind::none;
        hand.weapon = WeaponClass::none;
        hand.spell = SpellKind::unknown;
        return;
    }

    hand.equipped_id = equipped->GetFormID();
    if (const auto* weapon = equipped->As<RE::TESObjectWEAP>()) {
        hand.equipment = equipment_kind(*weapon);
        hand.weapon = detail::weapon_class(weapon->GetWeaponType());
        hand.two_handed = weapon->IsTwoHandedSword() || weapon->IsTwoHandedAxe() ||
                          weapon->IsBow() || weapon->IsCrossbow();
        return;
    }
    if (const auto* armor = equipped->As<RE::TESObjectARMO>()) {
        hand.equipment = armor->IsShield() ? EquipmentKind::shield : EquipmentKind::unknown;
        hand.weapon = WeaponClass::none;
        return;
    }
    if (const auto* magic_item = equipped->As<RE::MagicItem>()) {
        hand.equipment =
            equipped->As<RE::ScrollItem>() ? EquipmentKind::scroll : EquipmentKind::spell;
        hand.weapon = WeaponClass::none;
        hand.spell = classify_spell(*magic_item);
        return;
    }

    hand.equipment = EquipmentKind::unknown;
    hand.weapon = WeaponClass::unknown;
}

void fill_caster_hand(HandObservation& hand,
    RE::Actor& player,
    RE::TESForm* equipped,
    RE::MagicSystem::CastingSource source) noexcept {
    const auto* caster = player.GetMagicCaster(source);
    if (!caster || !caster->currentSpell) {
        hand.caster_source_id = 0;
        hand.cast_mode = CastMode::none;
        hand.cast_phase = CastPhase::none;
        return;
    }

    const auto& magic_item = *caster->currentSpell;
    RE::FormID staff_enchantment_id = 0;
    if (hand.equipment == EquipmentKind::staff && equipped) {
        if (const auto* weapon = equipped->As<RE::TESObjectWEAP>();
            weapon && weapon->GetWeaponType() == RE::WeaponTypes::kStaff &&
            weapon->formEnchanting) {
            staff_enchantment_id = weapon->formEnchanting->GetFormID();
        }
    }
    if (!detail::caster_matches_equipped(hand.equipment,
            hand.equipped_id,
            magic_item.GetFormID(),
            staff_enchantment_id)) {
        hand.caster_source_id = 0;
        hand.cast_mode = CastMode::none;
        hand.cast_phase = CastPhase::none;
        return;
    }

    hand.caster_source_id = magic_item.GetFormID();
    const bool equipped_spell_was_classified =
        hand.equipped_id == magic_item.GetFormID() &&
        (hand.equipment == EquipmentKind::spell || hand.equipment == EquipmentKind::scroll);
    if (!equipped_spell_was_classified) {
        hand.spell = classify_spell(magic_item);
    }
    hand.cast_mode =
        detail::cast_mode(magic_item.GetCastingType(), magic_item.As<RE::ScrollItem>() != nullptr);
    hand.cast_phase = detail::cast_phase(caster->state.get());
}

[[nodiscard]] bool menus_clear(RE::UI* user_interface) noexcept {
    if (!user_interface) {
        return false;
    }

    if (user_interface->GameIsPaused() || user_interface->IsModalMenuOpen() ||
        user_interface->IsItemMenuOpen() || user_interface->IsApplicationMenuOpen()) {
        return false;
    }

    const auto* blocking_menus = pinned_blocking_menu_names();
    if (!blocking_menus || std::ranges::any_of(*blocking_menus, [&](const auto& name) {
            return is_menu_open(*user_interface, name);
        })) {
        return false;
    }

    return std::ranges::none_of(user_interface->menuStack, [](const auto& entry) {
        const auto* menu = entry.get();
        return menu && detail::menu_suppresses_effects(menu->OnStack(),
                           menu->AlwaysOpen(),
                           menu->PausesGame(),
                           menu->Modal(),
                           menu->InventoryItemMenu(),
                           menu->ApplicationMenu(),
                           menu->UsesMenuContext(),
                           menu->UsesCursor(),
                           menu->FreezeFramePause() || menu->FreezeFrameBackground());
    });
}

[[nodiscard]] bool is_bow(const HandObservation& hand) noexcept {
    return hand.equipment == EquipmentKind::bow && hand.weapon == WeaponClass::bow;
}

[[nodiscard]] bool detection_relocations_available() noexcept {
    static const bool available = [] noexcept {
        try {
            const REL::Offset2ID mappings{};
            const std::uint64_t singleton_id = REL::RelocationID{514167, 400315}.id();
            const std::uint64_t detection_id = REL::RelocationID{40394, 41408}.id();
            return detail::relocation_id_present(mappings, singleton_id) &&
                   detail::relocation_id_present(mappings, detection_id);
        } catch (...) {
            return false;
        }
    }();
    return available;
}

[[nodiscard]] std::optional<std::int32_t> highest_detection_score(RE::Actor& player) noexcept {
    if (!detection_relocations_available()) {
        return std::nullopt;
    }
    auto* process_lists = RE::ProcessLists::GetSingleton();
    if (!process_lists) {
        return std::nullopt;
    }
    std::uint32_t line_of_sight_count{};
    return static_cast<std::int32_t>(
        process_lists->RequestHighestDetectionLevelAgainstActor(&player, line_of_sight_count));
}

void read_led_observation(RE::Actor& player,
    Observation& observation,
    const inputnext::adapters::LedSettings& settings,
    inputnext::adapters::LedHealthSample health) noexcept {
    if (!settings.enabled) {
        return;
    }
    observation.led.subject_id = observation.player_id;
    if (settings.health_enabled) {
        observation.led.health =
            skyrim_adapter::detail::normalized_led_health(health.current, health.maximum);
        observation.led.in_combat = observation.in_combat;
    }
    if (!settings.stealth_enabled || !observation.focused || !observation.menus_clear ||
        !observation.alive || !observation.world || observation.world->mounted) {
        return;
    }
    const auto* actor_state = player.AsActorState();
    if (!actor_state->IsSneaking() || actor_state->IsSwimming()) {
        return;
    }
    observation.led.stealth =
        skyrim_adapter::detail::normalized_led_stealth(true, highest_detection_score(player));
}

void resolve_source(Action& action) {
    if (action.event.source_id == 0) {
        return;
    }

    auto* source = RE::TESForm::LookupByID(action.event.source_id);
    if (!source) {
        return;
    }

    if (const auto* weapon = source->As<RE::TESObjectWEAP>()) {
        action.source_kind = equipment_kind(*weapon);
        action.weapon = detail::weapon_class(weapon->GetWeaponType());
        if (action.source_kind == EquipmentKind::staff) {
            const auto* enchantment = weapon->formEnchanting;
            if (!enchantment || !detail::staff_action_matches_source(action.event.source_id,
                                    weapon->GetFormID(),
                                    enchantment->GetFormID())) {
                return;
            }
            action.spell = classify_spell(*enchantment);
            action.cast_mode = detail::cast_mode(enchantment->GetCastingType());
        }
        action.source_known = true;
        return;
    }
    if (const auto* armor = source->As<RE::TESObjectARMO>(); armor && armor->IsShield()) {
        action.source_kind = EquipmentKind::shield;
        action.source_known = true;
        return;
    }
    if (const auto* magic_item = source->As<RE::MagicItem>()) {
        action.source_kind =
            source->As<RE::ScrollItem>() ? EquipmentKind::scroll : EquipmentKind::spell;
        action.spell = classify_spell(*magic_item);
        action.cast_mode = detail::cast_mode(magic_item->GetCastingType(),
            source->As<RE::ScrollItem>() != nullptr);
        action.source_known = true;
    }
}
} // namespace

namespace detail {
ActorSample sample_actor(RE::Actor& actor) noexcept {
    const auto* actor_values = actor.AsActorValueOwner();
    const auto* actor_state = actor.AsActorState();
    const auto& state = actor_state->actorState1;
    const bool moving =
        state.movingBack || state.movingForward || state.movingRight || state.movingLeft;
    return ActorSample{
        .health = actor_values->GetActorValue(RE::ActorValue::kHealth),
        .stamina = actor_values->GetActorValue(RE::ActorValue::kStamina),
        .life_state = actor_state->GetLifeState(),
        .weapon_state = actor_state->GetWeaponState(),
        .attack_state = actor_state->GetAttackState(),
        .moving = moving,
        .sprinting = actor_state->IsSprinting(),
        .swimming = actor_state->IsSwimming(),
        .staggered = actor_state->IsStaggered(),
        .knocked_down = knocked_down(actor_state->GetKnockState()),
        .gait = movement_gait(state),
    };
}
} // namespace detail

Observation read_observation(MonoTime started_at,
    bool focused,
    const inputnext::adapters::LedSettings& led_settings) noexcept {
    Observation observation{};
    observation.sampled_at = started_at;
    observation.focused = focused;

    auto* preferences = RE::INIPrefSettingCollection::GetSingleton();
    const auto* rumble_setting =
        preferences ? preferences->GetSetting("bGamePadRumble:Controls") : nullptr;
    observation.body_vibration_enabled =
        detail::read_boolean_setting(rumble_setting).value_or(false);

    auto* user_interface = RE::UI::GetSingleton();
    observation.menus_clear = menus_clear(user_interface);

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return observation;
    }

    observation.available = true;
    observation.player_id = player->GetFormID();
    observation.content = read_content_observation(*player);
    const auto actor_sample = detail::sample_actor(*player);
    observation.alive = actor_sample.life_state == RE::ACTOR_LIFE_STATE::kAlive;
    observation.weapon_drawn = actor_sample.weapon_state == RE::WEAPON_STATE::kDrawn;
    observation.dual_cast = player->IsDualCasting();
    observation.in_combat = player->IsInCombat();
    observation.blocking = player->IsBlocking();

    WorldObservation world{};
    world.moving = actor_sample.moving;
    world.sprinting = actor_sample.sprinting;
    world.swimming = actor_sample.swimming;
    world.staggered = actor_sample.staggered;
    world.knocked_down = actor_sample.knocked_down;
    world.killmove = player->IsInKillMove();
    world.airborne = player->IsInMidair();
    RE::NiPointer<RE::Actor> mount;
    world.mounted = player->GetMount(mount);
    world.gait = actor_sample.gait;
    if (world.mounted && mount) {
        world.mount_id = mount->GetFormID();
        const auto mount_sample = detail::sample_actor(*mount);
        world.mount_gait = mount_sample.gait;
        world.mount_swimming = mount_sample.swimming;
        world.mount_airborne = mount->IsInMidair();
    }

    const float stamina_max = player->GetActorValueMax(RE::ActorValue::kStamina);
    if (std::isfinite(actor_sample.stamina) && std::isfinite(stamina_max) && stamina_max > 0.0F) {
        world.stamina_ratio = std::clamp(actor_sample.stamina / stamina_max, 0.0F, 1.0F);
    }

    const auto* cell = player->GetParentCell();
    const auto* worldspace = player->GetWorldspace();
    if (cell) {
        const auto position = player->GetPosition();
        world.location = WorldLocation{
            .cell_id = cell->GetFormID(),
            .worldspace_id = worldspace ? worldspace->GetFormID() : 0,
            .x = position.x,
            .y = position.y,
            .z = position.z,
        };
    }
    observation.world = world;

    const float health = actor_sample.health;
    const float max_health = player->GetActorValueMax(RE::ActorValue::kHealth);
    if (std::isfinite(health) && std::isfinite(max_health) && max_health > 0.0F) {
        observation.health_ratio = std::clamp(health / max_health, 0.0F, 1.0F);
    }
    read_led_observation(*player,
        observation,
        led_settings,
        {.current = health, .maximum = max_health});

    auto* left_equipped = player->GetEquippedObject(true);
    auto* right_equipped = player->GetEquippedObject(false);
    fill_equipped_hand(observation.hands[0], left_equipped);
    fill_equipped_hand(observation.hands[1], right_equipped);
    fill_caster_hand(observation.hands[0],
        *player,
        left_equipped,
        RE::MagicSystem::CastingSource::kLeftHand);
    fill_caster_hand(observation.hands[1],
        *player,
        right_equipped,
        RE::MagicSystem::CastingSource::kRightHand);

    if (is_bow(observation.hands[0]) && is_bow(observation.hands[1]) &&
        observation.hands[0].equipped_id == observation.hands[1].equipped_id) {
        observation.hands[0] = {};
        observation.hands[0].equipment = EquipmentKind::none;
        observation.hands[0].weapon = WeaponClass::none;
        observation.hands[0].cast_mode = CastMode::none;
        observation.hands[0].cast_phase = CastPhase::none;
    }

    observation.shield_blocking =
        observation.blocking && (observation.hands[0].equipment == EquipmentKind::shield ||
                                    observation.hands[1].equipment == EquipmentKind::shield);

    const bool has_bow = is_bow(observation.hands[0]) || is_bow(observation.hands[1]);
    observation.bow = has_bow ? detail::bow_phase(actor_sample.attack_state) : BowPhase::none;
    observation.ammo_present = player->GetCurrentAmmo() != nullptr;
    observation.ranged_ready =
        observation.weapon_drawn && observation.ammo_present && has_bow &&
        (observation.bow == BowPhase::attached || observation.bow == BowPhase::drawn);

    return observation;
}

std::optional<Action>
resolve_action(const RawEvent& event, const Observation& observation, MonoTime now) {
    const FormId current_player = observation.player_id;
    if (current_player == 0) {
        return std::nullopt;
    }

    Action action{};
    action.event = event;
    action.attributed_player_id = current_player;

    if (event.kind == EventKind::footstep) {
        if (!skyrim_adapter::detail::event_fresh_at(event.observed_at, now)) {
            return std::nullopt;
        }
        const auto attribution =
            skyrim_adapter::detail::current_footstep_attribution(observation, event.actor_id, now);
        if (!attribution) {
            return std::nullopt;
        }
        action.gait = attribution->gait;
        action.mount_attributed = attribution->mounted;
        action.attributed_mount_id = attribution->mount_id;
        return action;
    }

    if (event.kind == EventKind::hit) {
        if (event.target_id == current_player) {
            action.incoming = true;
            return action;
        }
        if (event.actor_id != current_player || event.projectile_id != 0 || event.source_id == 0) {
            return std::nullopt;
        }

        resolve_source(action);
        if (!action.source_known || action.source_kind != EquipmentKind::weapon ||
            !detail::melee_weapon(action.weapon)) {
            return std::nullopt;
        }

        action.outgoing = true;
        return action;
    }

    if (event.actor_id != current_player) {
        return std::nullopt;
    }

    if (event.kind == EventKind::player_bow_shot &&
        (!skyrim_adapter::detail::observation_usable_at(observation, now) ||
            !skyrim_adapter::detail::event_fresh_at(event.observed_at, now))) {
        return std::nullopt;
    }

    if (event.kind == EventKind::weapon_swing && event.source_id == 0 &&
        skyrim_adapter::detail::source_less_empty_hand_swing(event, observation, now)) {
        action.source_kind = EquipmentKind::weapon;
        action.weapon = WeaponClass::unarmed;
        action.source_known = true;
        return action;
    }

    resolve_source(action);
    return action;
}
} // namespace skyrim_adapter::native
