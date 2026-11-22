#include "native_game.hpp"

#include "native_combat.hpp"
#include "native_observation.hpp"
#include "nvse_sdk.hpp"

#include <fallout_gamebryo/model.hpp>

#include <nvse/GameAPI.h>
#include <nvse/GameForms.h>
#include <nvse/GameObjects.h>
#include <nvse/GameProcess.h>
#include <nvse/GameTypes.h>
#include <nvse/GameUI.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fallout_new_vegas {
namespace {
using fallout_gamebryo::Menu;
using fallout_gamebryo::Mode;
using fallout_gamebryo::WeaponClass;

// JIP-LN's IsInAir reads hkState bit 1 from this controller context.
// ShowOff 1.84's typed Havok layout places the context and state at these x86 offsets.
struct CharacterContextView {
    std::uint32_t vtable;
    std::uint16_t size_and_flags;
    std::uint16_t reference_count;
    std::uint32_t unknown_08;
    std::uint32_t state_manager;
    std::uint32_t state;
    std::array<std::uint32_t, 7> unknown_14;
};

struct CharacterControllerView {
    std::array<std::byte, 0x3E0> inherited;
    CharacterContextView character_context;
};

static_assert(sizeof(void*) == sizeof(std::uint32_t));
static_assert(sizeof(CharacterContextView) == 0x30);
static_assert(offsetof(CharacterControllerView, character_context) == 0x3E0);
static_assert(offsetof(CharacterControllerView, character_context.state) == 0x3F0);

constexpr std::uint32_t hk_state_in_air = 0x2;

// xNVSE declares these virtuals for the game-owned vtable. Member-function pointers keep the
// calls virtual when Clang sees the concrete SDK member type.
constexpr auto current_actor_value = &ActorValueOwner::Fn_03;
constexpr auto permanent_actor_value = &ActorValueOwner::Fn_08;

MiddleHighProcess* middle_high_process(BaseProcess& process) noexcept {
    if (process.processLevel > BaseProcess::kProcessLevel_MiddleHigh) {
        return nullptr;
    }
    return static_cast<MiddleHighProcess*>(&process);
}

void capture_actor_values(PlayerCharacter* player, ObservationFacts& facts) noexcept {
    auto& values = player->avOwner;
    facts.health_current = (values.*current_actor_value)(eActorVal_Health);
    facts.health_maximum = (values.*permanent_actor_value)(eActorVal_Health);
    facts.action_points_current = (values.*current_actor_value)(eActorVal_ActionPoints);
    facts.action_points_maximum = (values.*permanent_actor_value)(eActorVal_ActionPoints);
    facts.in_combat = player->IsInCombat();
}

void capture_radiation(PlayerCharacter* player,
    const MiddleHighProcess* middle_high,
    ObservationFacts& facts) noexcept {
    if (!middle_high) {
        return;
    }
    const auto radiation_dose = (player->avOwner.*current_actor_value)(eActorVal_RadLevel);
    const auto water_radiation = middle_high->waterRadsSec;
    const auto active_radiation = player->baseProcess->GetRadsSec();
    if (std::isfinite(radiation_dose) && radiation_dose >= 0.0F) {
        facts.radiation = std::clamp(radiation_dose / 1000.0F, 0.0F, 1.0F);
    }
    if (std::isfinite(water_radiation) && std::isfinite(active_radiation)) {
        facts.radiation_sources =
            static_cast<std::uint32_t>(water_radiation > 0.0F || active_radiation > 0.0F);
    }
}

bool accepts_ammo(const TESForm& configured, const TESAmmo& selected) noexcept {
    if (selected.typeID != kFormType_TESAmmo) {
        return false;
    }
    return &configured == &selected ||
           (configured.typeID == kFormType_BGSListForm &&
               static_cast<const BGSListForm&>(configured).GetIndexOf(&selected) != eListInvalid);
}

void capture_weapon(const PlayerCharacter* player,
    const MiddleHighProcess* middle_high,
    PlayerAnimation animation,
    ObservationFacts& facts) noexcept {
    facts.aiming = middle_high != nullptr && middle_high->isAiming != 0;
    const auto* weapon = player->GetEquippedWeapon();
    if (!weapon) {
        if (animation == PlayerAnimation::melee_attack) {
            facts.weapon.kind = WeaponClass::unarmed;
        }
        return;
    }
    facts.weapon.id = form_id(weapon);
    facts.weapon.kind = classify_weapon(weapon);
    facts.weapon.drawn = middle_high != nullptr && middle_high->isWeaponOut;
    facts.weapon.ready = player->baseProcess->IsReadyForAnim();
    facts.weapon.automatic = weapon->IsAutomatic();
    const auto* configured_ammo = weapon->ammo.ammo;
    if (fallout_gamebryo::is_firearm(facts.weapon.kind) && configured_ammo &&
        weapon->ammoUse != 0U) {
        // xNVSE exposes the active clip count and ammo form on the player's process.
        if (const auto* ammo = player->baseProcess->GetAmmoInfo();
            ammo && ammo->ammo && accepts_ammo(*configured_ammo, *ammo->ammo)) {
            facts.weapon.loaded_ammo = ammo->count;
        }
    }
}

void capture_movement(const PlayerCharacter* player,
    MiddleHighProcess* process,
    std::uint32_t movement_flags,
    ObservationFacts& facts) noexcept {
    if (!process) {
        return;
    }
    const auto* controller = process->GetCharacterController();
    if (!controller) {
        return;
    }
    const auto* view = reinterpret_cast<const CharacterControllerView*>(controller);
    fallout_gamebryo::Movement movement{};
    movement.cell = player->parentCell ? form_id(player->parentCell) : 0;
    movement.height = player->posZ;
    movement.x = player->posX;
    movement.y = player->posY;
    movement.airborne = (view->character_context.state & hk_state_in_air) != 0U;
    movement.gait = gait_from_movement_flags(movement_flags, movement.airborne);
    facts.movement = movement;
}

PlayerAnimation classify_animation(std::int32_t action) noexcept {
    if (action >= HighProcess::kAnimAction_Attack &&
        action <= HighProcess::kAnimAction_Attack_Follow_Through) {
        return PlayerAnimation::melee_attack;
    }
    switch (action) {
    case HighProcess::kAnimAction_Wait_For_Special_Idle:
        return PlayerAnimation::special_idle;
    case HighProcess::kAnimAction_Attack_Throw_Release:
        return PlayerAnimation::throw_release;
    default:
        return PlayerAnimation::other;
    }
}

Mode observe_mode(Menu& menu, bool vats_menu_visible, std::uint32_t vats_mode) noexcept {
    if (InterfaceManager::IsMenuVisible(kMenuType_Start) ||
        InterfaceManager::IsMenuVisible(kMenuType_Loading)) {
        menu = Menu::other;
        return Mode::unavailable;
    }
    if (vats_mode == VATSCameraData::kVATSMode_Playback) {
        menu = Menu::none;
        return Mode::vats_playback;
    }
    if (vats_menu_visible) {
        if (vats_mode == VATSCameraData::kVATSMode_TargetSelect ||
            vats_mode == VATSCameraData::kVATSMode_LimbSelectOrZoom) {
            menu = Menu::none;
            return Mode::vats_selection;
        }
        menu = Menu::other;
        return Mode::menu;
    }
    if (InterfaceManager::IsMenuVisible(kMenuType_LockPick)) {
        menu = Menu::lockpick;
        return Mode::menu;
    }
    if (InterfaceManager::IsMenuVisible(kMenuType_Computers) ||
        InterfaceManager::IsMenuVisible(kMenuType_Hacking)) {
        menu = Menu::terminal;
        return Mode::menu;
    }
    if (InterfaceManager::IsMenuVisible(kMenuType_Repair)) {
        menu = Menu::repair;
        return Mode::menu;
    }
    if (InterfaceManager::IsMenuVisible(kMenuType_Recipe)) {
        menu = Menu::crafting;
        return Mode::menu;
    }
    if (InterfaceManager::IsMenuVisible(kMenuType_Inventory)) {
        menu = Menu::inventory;
        return Mode::menu;
    }
    const auto* interface = InterfaceManager::GetSingleton();
    if (interface && interface->activeMenu) {
        menu = Menu::other;
        return Mode::menu;
    }
    menu = Menu::none;
    return Mode::gameplay;
}
} // namespace

PlayerCharacter* current_player() noexcept {
    return g_thePlayer ? *g_thePlayer : nullptr;
}

std::uint32_t form_id(const TESForm* form) noexcept {
    return form != nullptr ? form->refID : 0;
}

WeaponClass classify_weapon(const TESObjectWEAP* weapon) noexcept {
    if (!weapon) {
        return WeaponClass::none;
    }
    switch (form_id(weapon)) {
    case 0x0000433FU: // WeapMinigun
    case 0x001629B6U: // WeapNVMinigunUnique
    case 0x0000432EU: // WeapGatlingLaser
    case 0x0000432DU: // WeapFlamer
    case 0x000906DAU: // WeapNVIncinerator
    case 0x000E2BFCU: // WeapHeavyIncinerator
        return WeaponClass::heavy;
    default:
        break;
    }
    if (weapon->numProjectiles > 1U && weapon->weaponSkill == eActorVal_Guns) {
        return WeaponClass::shotgun;
    }
    switch (weapon->eWeaponType) {
    case TESObjectWEAP::kWeapType_HandToHandMelee:
        return WeaponClass::unarmed;
    case TESObjectWEAP::kWeapType_OneHandMelee:
    case TESObjectWEAP::kWeapType_TwoHandMelee:
    case TESObjectWEAP::kWeapType_TwoHandHandle:
        return WeaponClass::melee;
    case TESObjectWEAP::kWeapType_OneHandPistol:
        return WeaponClass::pistol;
    case TESObjectWEAP::kWeapType_OneHandPistolEnergy:
    case TESObjectWEAP::kWeapType_TwoHandRifleEnergy:
        return WeaponClass::energy;
    case TESObjectWEAP::kWeapType_TwoHandRifle:
    case TESObjectWEAP::kWeapType_TwoHandAutomatic:
        return WeaponClass::rifle;
    case TESObjectWEAP::kWeapType_TwoHandLauncher:
        return WeaponClass::launcher;
    case TESObjectWEAP::kWeapType_OneHandGrenade:
    case TESObjectWEAP::kWeapType_OneHandMine:
    case TESObjectWEAP::kWeapType_OneHandLunchboxMine:
    case TESObjectWEAP::kWeapType_OneHandThrown:
        return WeaponClass::thrown;
    default:
        return WeaponClass::generic_firearm;
    }
}

GameSample sample_game(ObservationFacts facts) noexcept {
    auto* const player = current_player();
    const auto player_id = form_id(player);
    if (!player || !player->baseProcess || player_id == 0U) {
        return {.observation = make_observation(facts)};
    }
    facts.player = player_id;
    const auto* vats = VATSCameraData::GetSingleton();
    // xNVSE exposes this singleton at a fixed address in the supported executable.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.FixedAddressDereference)
    const auto vats_mode = vats ? vats->mode : VATSCameraData::kVATSMode_None;
    facts.mode =
        observe_mode(facts.menu, InterfaceManager::IsMenuVisible(kMenuType_VATS), vats_mode);

    capture_actor_values(player, facts);
    auto* const middle_high = middle_high_process(*player->baseProcess);
    capture_radiation(player, middle_high, facts);
    const auto action = player->baseProcess->GetCurrentAnimAction();
    const auto animation = classify_animation(action);
    std::optional<ReactionState> reaction;
    if (middle_high) {
        reaction =
            ReactionState{.knocked_state = middle_high->knockedState, .animation_action = action};
        facts.reaction = read_player_reaction(*reaction);
    }
    facts.reloading = action == HighProcess::kAnimAction_Reload;
    capture_weapon(player, middle_high, animation, facts);
    std::uint32_t movement_flags{};
    if (player->actorMover) {
        movement_flags = player->actorMover->Unk_08();
        capture_movement(player, middle_high, movement_flags, facts);
    }

    GameSample sample{.observation = make_observation(facts),
        .raw_reaction = reaction,
        .animation = animation,
        .sneaking = is_sneaking(movement_flags)};
    if (facts.mode == Mode::vats_selection) {
        sample.vats_selection = vats_mode == VATSCameraData::kVATSMode_TargetSelect
                                    ? VatsSelection::target
                                    : VatsSelection::limb;
    }
    return sample;
}
} // namespace fallout_new_vegas
