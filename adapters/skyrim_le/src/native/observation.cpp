#include "observation.hpp"
#include "actor_values.hpp"
#include "native/translators.hpp"
#include "skyrim_adapter/content_observation.hpp"
#include "skyrim_adapter/detail/led_observation.hpp"
#include "skyrim_adapter/model.hpp"

#include <array>
#include <common/ITypes.h>
#include <cstdint>
#include <skse/GameAPI.h>
#include <skse/GameData.h>
#include <skse/GameForms.h>
#include <skse/GameMenus.h>
#include <skse/GameObjects.h>
#include <skse/GameReferences.h>
#include <skse/GameSettings.h>
#include <skse/GameTypes.h>
#include <skse/NiTypes.h>
#include <skse/Utilities.h>
#include <skyrim_adapter/blocking_menus.hpp>
#include <skyrim_adapter/detail/ingress.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>

namespace skyrim_adapter::native {
namespace {
using detail::OptionalRaceRecord;

[[nodiscard]] std::string_view bounded_string(const char* text, std::size_t capacity) noexcept {
    if (!text) {
        return {};
    }
    std::size_t length{};
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    if (length == capacity) {
        return {};
    }
    return {text, length};
}

[[nodiscard]] std::optional<FormId> lookup_optional_race(const DataHandler& data,
    std::string_view plugin_name,
    FormId local_form_id) noexcept {
    constexpr std::size_t loaded_mod_capacity =
        sizeof(data.modList.loadedMods) / sizeof(data.modList.loadedMods[0]);
    const ModInfo* match{};
    std::size_t match_slot{};
    // loadedMods is indexed by the actual runtime mod index and can be sparse. The loaded count is not a high-water
    // mark, so inspect its fixed, bounded table rather than treating list position as a FormID index.
    for (std::size_t slot = 0; slot < loaded_mod_capacity; ++slot) {
        const ModInfo* candidate = data.modList.loadedMods[slot];
        if (!candidate || !detail::ascii_equal_case_insensitive(
                              bounded_string(candidate->name, sizeof(candidate->name)),
                              plugin_name)) {
            continue;
        }
        if (match) {
            return std::nullopt;
        }
        match = candidate;
        match_slot = slot;
    }
    if (!match || match_slot >= loaded_mod_capacity) {
        return std::nullopt;
    }

    const auto composed_id = (static_cast<FormId>(match->modIndex) << 24U) | local_form_id;
    const TESForm* resolved = LookupFormByID(composed_id);
    if (!resolved) {
        return std::nullopt;
    }

    const OptionalRaceRecord record{
        .has_loaded_entry = true,
        .mod_index = match->modIndex,
        .loaded_slot = static_cast<UInt8>(match_slot),
        .loaded_name = bounded_string(match->name, sizeof(match->name)),
        .returned_form_id = resolved->formID,
        .returned_form_type = resolved->formType,
    };
    return detail::validate_optional_race_record(record,
        plugin_name,
        local_form_id,
        detail::ExpectedFormType{.value = kFormType_Race});
}

struct CachedOptionalRaces {
    std::optional<FormId> werewolf_id;
    std::optional<FormId> vampire_lord_id;
    bool resolved{};
};

static_assert(std::is_trivially_destructible_v<CachedOptionalRaces>);

[[nodiscard]] const CachedOptionalRaces* cached_optional_races() noexcept {
    const auto* const data = DataHandler::GetSingleton();
    if (data == nullptr || !LookupFormByID) {
        return nullptr;
    }

    // Called only from the admitted game-task observation path after DataLoaded. Store validated IDs, not engine
    // handles; optional plugins absent at this point remain absent for the process lifetime.
    static CachedOptionalRaces cache{};
    if (!cache.resolved) {
        cache.werewolf_id =
            lookup_optional_race(*data, detail::skyrim_master_name, detail::werewolf_race_local_id);
        cache.vampire_lord_id =
            lookup_optional_race(*data, detail::dawnguard_name, detail::vampire_lord_race_local_id);
        cache.resolved = true;
    }
    return &cache;
}

[[nodiscard]] ContentObservation read_content_observation(const PlayerCharacter& player) noexcept {
    ContentObservation content{};
    const auto* const races = cached_optional_races();
    if (races == nullptr) {
        return content;
    }
    if (!player.race) {
        return content;
    }

    if (!races->werewolf_id && !races->vampire_lord_id) {
        return content;
    }

    content.beast = BeastForm::none;
    if (races->werewolf_id && player.race->formID == *races->werewolf_id) {
        content.beast = BeastForm::werewolf;
    } else if (races->vampire_lord_id && player.race->formID == *races->vampire_lord_id) {
        content.beast = BeastForm::vampire_lord;
    }
    // Skyrim LE deliberately never reads Survival globals, quests, or plugin state.
    return content;
}

[[nodiscard]] std::optional<float> ratio(float current, float maximum) noexcept {
    if (!std::isfinite(current) || !std::isfinite(maximum) || maximum <= 0.0F) {
        return std::nullopt;
    }
    return std::clamp(current / maximum, 0.0F, 1.0F);
}

void record_menu_block(detail::MenuReadDiagnostics& diagnostics,
    std::string_view name,
    detail::MenuBlockReason reason,
    std::uint32_t flags) noexcept {
    diagnostics.clear = false;
    diagnostics.reason = reason;
    diagnostics.flags = flags;
    diagnostics.name_length = std::min(name.size(), diagnostics.name.size() - 1);
    for (std::size_t index = 0; index < diagnostics.name_length; ++index) {
        const auto character = static_cast<unsigned char>(name[index]);
        diagnostics.name[index] =
            character >= 0x20 && character <= 0x7E ? static_cast<char>(character) : '?';
    }
    diagnostics.name[diagnostics.name_length] = '\0';
}

[[nodiscard]] bool menu_blocks_gameplay(MenuManager& manager,
    BSFixedString& fixed_name,
    std::string_view name,
    detail::MenuReadDiagnostics& diagnostics) noexcept {
    const bool is_open = manager.IsMenuOpen(&fixed_name);
    const IMenu* menu = is_open ? manager.GetMenu(&fixed_name) : nullptr;
    const std::uint32_t flags = menu ? static_cast<std::uint32_t>(menu->flags) : 0;
    const auto decision = detail::classify_menu(is_open, menu != nullptr, flags);
    if (decision.blocks_gameplay) {
        record_menu_block(diagnostics, name, decision.reason, flags);
    }
    return decision.blocks_gameplay;
}

[[nodiscard]] bool menu_blocks_gameplay(MenuManager& manager,
    std::string_view name,
    detail::MenuReadDiagnostics& diagnostics) noexcept {
    if (name.empty() || name.size() >= detail::menu_name_capacity) {
        record_menu_block(diagnostics, name, detail::MenuBlockReason::snapshot_unreliable, 0);
        return true;
    }
    std::array<char, detail::menu_name_capacity> null_terminated{};
    for (std::size_t index = 0; index < name.size(); ++index) {
        if (name[index] == '\0') {
            record_menu_block(diagnostics, name, detail::MenuBlockReason::snapshot_unreliable, 0);
            return true;
        }
        null_terminated[index] = name[index];
    }
    null_terminated[name.size()] = '\0';

    BSFixedString fixed_name{null_terminated.data()};
    const bool blocks_gameplay = menu_blocks_gameplay(manager, fixed_name, name, diagnostics);
    if (fixed_name.data) {
        CALL_MEMBER_FN(&fixed_name, Release)();
    }
    return blocks_gameplay;
}

using PinnedBlockingMenus = std::array<BSFixedString, skyrim_adapter::known_blocking_menus.size()>;
static_assert(std::is_trivially_destructible_v<BSFixedString>);

[[nodiscard]] PinnedBlockingMenus& pinned_blocking_menus() noexcept {
    // LE's StringCache::Ref has no destructor. Initialize only after the manager is available and retain this bounded
    // catalogue for the process lifetime; its static teardown does not call back into the engine.
    static PinnedBlockingMenus menus = [] {
        PinnedBlockingMenus result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            std::array<char, detail::menu_name_capacity> null_terminated{};
            const auto name = skyrim_adapter::known_blocking_menus[index];
            std::ranges::copy(name, null_terminated.begin());
            result[index] = BSFixedString{null_terminated.data()};
        }
        return result;
    }();
    return menus;
}

[[nodiscard]] bool menus_clear(const detail::MenuSnapshot& snapshot,
    detail::MenuReadDiagnostics& diagnostics) noexcept {
    if (!detail::menu_snapshot_is_reliable(snapshot)) {
        diagnostics.clear = false;
        diagnostics.reason = detail::MenuBlockReason::snapshot_unreliable;
        return false;
    }
    auto* manager = MenuManager::GetSingleton();
    if (!manager) {
        diagnostics.clear = false;
        diagnostics.reason = detail::MenuBlockReason::manager_unavailable;
        return false;
    }

    auto& blocking_menus = pinned_blocking_menus();
    for (std::size_t index = 0; index < blocking_menus.size(); ++index) {
        if (menu_blocks_gameplay(*manager,
                blocking_menus[index],
                skyrim_adapter::known_blocking_menus[index],
                diagnostics)) {
            return false;
        }
    }

    for (std::size_t index = 0; index < snapshot.count; ++index) {
        const auto& open = snapshot.open[index];
        if (menu_blocks_gameplay(*manager,
                bounded_string(open.bytes.data(), detail::menu_name_capacity),
                diagnostics)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] EquipmentKind equipment_kind(const TESObjectWEAP& weapon) noexcept {
    return detail::weapon_equipment(weapon.gameData.type);
}

void fill_equipped_hand(HandObservation& hand, const TESForm* equipped) noexcept {
    if (!equipped) {
        hand.equipment = EquipmentKind::none;
        hand.weapon = WeaponClass::none;
        return;
    }

    hand.equipped_id = equipped->formID;
    if (equipped->formType == kFormType_Weapon) {
        const auto* weapon = static_cast<const TESObjectWEAP*>(equipped);
        hand.equipment = equipment_kind(*weapon);
        hand.weapon = detail::weapon_class(weapon->gameData.type);
        hand.two_handed = detail::weapon_is_two_handed(weapon->gameData.type);
        return;
    }
    if (equipped->formType == kFormType_Armor) {
        const auto* armor = static_cast<const TESObjectARMO*>(equipped);
        hand.equipment = (armor->bipedObject.GetSlotMask() &
                             static_cast<UInt32>(BGSBipedObjectForm::kPart_Shield)) != 0
                             ? EquipmentKind::shield
                             : EquipmentKind::unknown;
        hand.weapon = WeaponClass::none;
        return;
    }
    if (equipped->formType == kFormType_Spell || equipped->formType == kFormType_ScrollItem) {
        hand.equipment = equipped->formType == kFormType_ScrollItem ? EquipmentKind::scroll
                                                                    : EquipmentKind::spell;
        hand.weapon = WeaponClass::none;
        // The classic caster state and spell-effect taxonomy are not confirmed by the binding map.
        hand.spell = SpellKind::unknown;
        hand.cast_mode = CastMode::unknown;
        hand.cast_phase = CastPhase::unknown;
        return;
    }

    hand.equipment = EquipmentKind::unknown;
    hand.weapon = WeaponClass::unknown;
}

void resolve_source(Action& action) noexcept {
    if (action.event.source_id == 0 || !LookupFormByID) {
        return;
    }
    const TESForm* source = LookupFormByID(action.event.source_id);
    if (!source || source->formID != action.event.source_id) {
        return;
    }

    if (source->formType == kFormType_Weapon) {
        const auto* weapon = static_cast<const TESObjectWEAP*>(source);
        action.source_kind = equipment_kind(*weapon);
        action.weapon = detail::weapon_class(weapon->gameData.type);
        action.source_known = true;
        return;
    }
    if (source->formType == kFormType_Armor) {
        const auto* armor = static_cast<const TESObjectARMO*>(source);
        if ((armor->bipedObject.GetSlotMask() &
                static_cast<UInt32>(BGSBipedObjectForm::kPart_Shield)) != 0) {
            action.source_kind = EquipmentKind::shield;
            action.weapon = WeaponClass::none;
            action.source_known = true;
        }
        return;
    }
    if (source->formType == kFormType_Spell) {
        action.source_kind = EquipmentKind::spell;
        action.spell = SpellKind::unknown;
        action.cast_mode = CastMode::unknown;
        action.source_known = true;
        return;
    }
    if (source->formType == kFormType_ScrollItem) {
        action.source_kind = EquipmentKind::scroll;
        action.spell = SpellKind::unknown;
        action.cast_mode = CastMode::unknown;
        action.source_known = true;
    }
}
} // namespace

Observation read_observation(MonoTime sampled_at,
    bool focused,
    const detail::MenuSnapshot& menu_snapshot,
    detail::ObservationDiagnostics* diagnostics) noexcept {
    Observation observation{};
    observation.sampled_at = sampled_at;
    observation.focused = focused;
    observation.body_vibration_enabled = false;

    detail::ObservationDiagnostics local_diagnostics{};
    auto& read_diagnostics = diagnostics ? *diagnostics : local_diagnostics;
    read_diagnostics = {};

    if (const Setting* rumble = GetINISetting("bGamePadRumble:Controls")) {
        double rumble_value{};
        if (rumble->GetType() != Setting::kType_Bool || !rumble->GetDouble(&rumble_value)) {
            read_diagnostics.vibration = detail::VibrationPreference::invalid;
        } else if (rumble_value != 0.0) {
            read_diagnostics.vibration = detail::VibrationPreference::enabled;
        } else {
            read_diagnostics.vibration = detail::VibrationPreference::disabled;
        }
    }
    if (read_diagnostics.vibration == detail::VibrationPreference::enabled) {
        observation.body_vibration_enabled = true;
    }
    observation.menus_clear = menus_clear(menu_snapshot, read_diagnostics.menu);

    PlayerCharacter* player = g_thePlayer ? *g_thePlayer : nullptr;
    if (!player) {
        return observation;
    }

    observation.available = true;
    observation.player_id = player->formID;
    observation.alive = !player->IsDead(1);
    observation.weapon_drawn = player->actorState.IsWeaponDrawn();
    observation.in_combat = player->IsInCombat();
    observation.content = read_content_observation(*player);

    const UInt32 health_value = ActorValueList::ResolveActorValueByName("Health");
    const UInt32 stamina_value = ActorValueList::ResolveActorValueByName("Stamina");
    if (health_value < ActorValueList::kNumActorValues) {
        const auto values = sample_actor_value(player->actorValueOwner, health_value);
        observation.health_ratio = ratio(values.current, values.maximum);
        observation.led.health =
            skyrim_adapter::detail::normalized_led_health(values.current, values.maximum);
    }
    observation.led.subject_id = observation.player_id;
    observation.led.in_combat = observation.in_combat;

    WorldObservation world{};
    const UInt32 movement_flags = player->actorState.flags04;
    world.moving = detail::actor_is_moving(movement_flags);
    world.gait = detail::movement_gait(movement_flags);
    world.sprinting = world.gait == MovementGait::sprinting;
    world.swimming = detail::actor_is_swimming(movement_flags);
    if (stamina_value < ActorValueList::kNumActorValues) {
        const auto values = sample_actor_value(player->actorValueOwner, stamina_value);
        world.stamina_ratio = ratio(values.current, values.maximum);
    }

    const TESObjectCELL* cell = player->parentCell;
    const TESWorldSpace* worldspace = CALL_MEMBER_FN(player, GetWorldspace)();
    if (cell && worldspace) {
        const NiPoint3 position = player->pos;
        world.location = WorldLocation{
            .cell_id = cell->formID,
            .worldspace_id = worldspace->formID,
            .x = position.x,
            .y = position.y,
            .z = position.z,
        };
    }
    observation.world = world;

    fill_equipped_hand(observation.hands[0], player->GetEquippedObject(true));
    fill_equipped_hand(observation.hands[1], player->GetEquippedObject(false));
    observation.shield_blocking = false;
    // Blocking, bow phases/ammunition, caster phase, dual-cast, stagger, mounts, and airborne
    // state stay at neutral defaults because their classic native mappings are unsupported.
    return observation;
}

std::optional<Action>
resolve_action(const RawEvent& event, const Observation& observation, MonoTime now) noexcept {
    const FormId player_id = observation.player_id;
    if (player_id == 0 || event.kind == EventKind::player_bow_shot ||
        event.kind == EventKind::footstep) {
        return std::nullopt;
    }

    Action action{};
    action.event = event;
    action.attributed_player_id = player_id;

    if (event.kind == EventKind::hit) {
        if (event.target_id == player_id) {
            action.incoming = true;
            return action;
        }
        if (event.actor_id != player_id || event.projectile_id != 0 || event.source_id == 0) {
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

    if (event.actor_id != player_id) {
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
