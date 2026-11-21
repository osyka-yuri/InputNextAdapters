#include "player_state.hpp"
#include "bow_projectiles.hpp"
#include "effect_model.hpp"
#include "memory_guard.hpp"
#include "model.hpp"
#include "xobse_sdk.hpp"

#include <GameExtraData.h>
#include <GameForms.h>
#include <GameObjects.h>
#include <GameProcess.h>
#include <HavokCharacters.h>
#include <NiHavokCharacters.h>
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace oblivion_adapter::native {
namespace {

constexpr std::uint32_t kPlayerRefId = 0x00000014;

bool seh_validate_pointer(const PlayerCharacter* player) noexcept {
    return guarded_game_read([&] {
        if (!player) {
            return false;
        }
        if (player->refID != kPlayerRefId || !player->baseForm || !player->parentCell) {
            return false;
        }
        constexpr auto kDisabledFlag = static_cast<std::uint32_t>(TESObjectREFR::kFlags_Disabled);
        constexpr auto kDeletedFlag = static_cast<std::uint32_t>(TESObjectREFR::kFlags_Deleted);
        return (static_cast<std::uint32_t>(player->flags) & (kDisabledFlag | kDeletedFlag)) == 0U;
    });
}

struct RawHealthValues {
    float current{};
    float max{};
};

bool seh_read_health(PlayerCharacter* player, RawHealthValues* out_health) noexcept {
    return guarded_game_read([&] {
        if (!player || player->refID != kPlayerRefId || !player->parentCell) {
            return false;
        }
        const float cur = player->GetAV_F(kActorVal_Health);
        const float base = player->GetCalculatedBaseAV(kActorVal_Health);
        const float max_mod = player->GetAVModifier(kAVModifier_Max, kActorVal_Health);
        const float maximum = base + max_mod;
        if (!std::isfinite(cur) || !std::isfinite(maximum) || maximum <= 0.0F) {
            return false;
        }
        if (out_health) {
            out_health->current = cur;
            out_health->max = maximum;
        }
        return true;
    });
}

bool seh_read_alive(PlayerCharacter* player, bool* out_alive) noexcept {
    return guarded_game_read([&] {
        if (!player || player->refID != kPlayerRefId || !player->parentCell || !out_alive) {
            return false;
        }
        *out_alive = !player->IsDead();
        return true;
    });
}

bool seh_read_combat(PlayerCharacter* player, bool* out_combat) noexcept {
    return guarded_game_read([&] {
        if (!player || player->refID != kPlayerRefId || !player->parentCell || !out_combat) {
            return false;
        }
        *out_combat = player->IsInCombat(false);
        return true;
    });
}

bool seh_read_form_id(const TESForm* form, std::uint32_t* out_form_id) noexcept {
    return guarded_game_read([&] {
        if (!form || !out_form_id) {
            return false;
        }
        *out_form_id = form->refID;
        return true;
    });
}

struct RawMenuState {
    bool game_mode{};
    std::uint32_t top_menu{};
};

bool seh_read_menu(RawMenuState* out_state) noexcept {
    return guarded_game_read([&] {
        if (!out_state) {
            return false;
        }
        InterfaceManager* interface_manager = InterfaceManager::GetSingleton();
        if (!interface_manager) {
            return false;
        }
        if (IsConsoleOpen()) {
            return true;
        }
        out_state->game_mode = interface_manager->IsGameMode();
        if (!out_state->game_mode) {
            out_state->top_menu = interface_manager->GetTopVisibleMenuID();
        }
        return true;
    });
}

Weapon weapon_from_type(std::uint32_t weapon_type) noexcept {
    switch (weapon_type) {
    case TESObjectWEAP::kType_BladeOneHand:
        return Weapon::blade_one_hand;
    case TESObjectWEAP::kType_BladeTwoHand:
        return Weapon::blade_two_hand;
    case TESObjectWEAP::kType_BluntOneHand:
        return Weapon::blunt_one_hand;
    case TESObjectWEAP::kType_BluntTwoHand:
        return Weapon::blunt_two_hand;
    case TESObjectWEAP::kType_Staff:
        return Weapon::staff;
    case TESObjectWEAP::kType_Bow:
        return Weapon::bow;
    default:
        return Weapon::unknown;
    }
}

bool seh_read_weapon(const TESForm* form, Weapon* out_weapon) noexcept {
    return guarded_game_read([&] {
        if (!form || !out_weapon || form->typeID != kFormType_Weapon) {
            return false;
        }
        const auto* weapon = reinterpret_cast<const TESObjectWEAP*>(form);
        *out_weapon = weapon_from_type(weapon->type);
        return true;
    });
}

bool seh_read_equipped_weapon(const PlayerCharacter* player,
    Weapon* out_weapon,
    std::uint32_t* out_id) noexcept {
    return guarded_game_read([&] {
        if (!player || player->refID != kPlayerRefId || !player->parentCell || !out_weapon ||
            !out_id) {
            return false;
        }
        *out_weapon = Weapon::unarmed;
        *out_id = 0;

        const auto* mobile = static_cast<const MobileObject*>(player);
        BaseProcess* process = mobile->process;
        if (!process) {
            return false;
        }
        const auto* high_process = static_cast<const MiddleHighProcess*>(
            Oblivion_DynamicCast(process, 0, RTTI_BaseProcess, RTTI_MiddleHighProcess, 0));
        if (!high_process) {
            // Equipped-weapon reads require at least a MiddleHighProcess.
            return true;
        }
        const ExtraContainerChanges::EntryData* entry = process->GetEquippedWeaponData(false);
        if (!entry || !entry->type) {
            return true;
        }
        if (entry->type->typeID != kFormType_Weapon) {
            return true;
        }
        const auto* weapon = reinterpret_cast<const TESObjectWEAP*>(entry->type);
        const Weapon mapped = weapon_from_type(weapon->type);
        *out_weapon = mapped;
        if (mapped != Weapon::unknown) {
            *out_id = entry->type->refID;
        }
        return true;
    });
}

bool seh_is_projectile_actor(const TESForm* form,
    const PlayerCharacter* player,
    bool* out_is_player_arrow) noexcept {
    return guarded_game_read([&] {
        if (!form || !player || !out_is_player_arrow) {
            return false;
        }
        *out_is_player_arrow = false;
        // xOBSE 22.15 exposes this fixed-address RTTI function with a mutable pointer ABI.
        // The form is only inspected; this cast adapts that pinned SDK signature.
        auto* reference = static_cast<TESObjectREFR*>(Oblivion_DynamicCast(
            const_cast<TESForm*>(form), // NOLINT(cppcoreguidelines-pro-type-const-cast)
            0,
            RTTI_TESForm,
            RTTI_TESObjectREFR,
            0));
        if (!reference || !reference->baseForm || reference->baseForm->typeID != kFormType_Ammo) {
            return true;
        }
        const auto* arrow = static_cast<const ArrowProjectile*>(
            Oblivion_DynamicCast(reference, 0, RTTI_TESObjectREFR, RTTI_ArrowProjectile, 0));
        if (arrow) {
            *out_is_player_arrow = arrow->shooter == player;
        }
        return true;
    });
}

struct RawLocation {
    std::uint32_t cell_id{};
    float position_z{};
};

bool seh_read_location(const PlayerCharacter* player, RawLocation* out_location) noexcept {
    return guarded_game_read([&] {
        if (!player || player->refID != kPlayerRefId || !player->parentCell || !out_location) {
            return false;
        }
        out_location->cell_id = player->parentCell->refID;
        out_location->position_z = player->posZ;
        return std::isfinite(out_location->position_z);
    });
}

struct RawRatios {
    float magicka{};
    float fatigue{};
};

bool seh_read_ratios(PlayerCharacter* player, RawRatios* out_ratios) noexcept {
    return guarded_game_read([&] {
        if (!player || player->refID != kPlayerRefId || !player->parentCell || !out_ratios) {
            return false;
        }
        const float magicka_current = player->GetAV_F(kActorVal_Magicka);
        const float magicka_max = player->GetCalculatedBaseAV(kActorVal_Magicka) +
                                  player->GetAVModifier(kAVModifier_Max, kActorVal_Magicka);
        const float fatigue_current = player->GetAV_F(kActorVal_Fatigue);
        const float fatigue_max = player->GetCalculatedBaseAV(kActorVal_Fatigue) +
                                  player->GetAVModifier(kAVModifier_Max, kActorVal_Fatigue);
        if (!std::isfinite(magicka_current) || !std::isfinite(magicka_max) ||
            !std::isfinite(fatigue_current) || !std::isfinite(fatigue_max) || magicka_max <= 0.0F ||
            fatigue_max <= 0.0F) {
            return false;
        }
        out_ratios->magicka = std::clamp(magicka_current / magicka_max, 0.0F, 1.0F);
        out_ratios->fatigue = std::clamp(fatigue_current / fatigue_max, 0.0F, 1.0F);
        return true;
    });
}

struct RawMotion {
    std::uint32_t movement_flags{};
    std::uint32_t mount_movement_flags{};
    HorizontalPosition horizontal_position;
    std::int16_t action{-1};
    std::uint8_t has_process{};
    std::uint8_t has_mount_movement_flags{};
    std::uint8_t has_ground{};
    std::uint8_t ground_state{0xFF};
    std::uint8_t mounted{};
    std::uint8_t has_mounted{};
    std::uint8_t melee_anim{};
    std::uint8_t power_anim{};
    std::uint8_t block_idle{};
    std::uint8_t block_hit{};
    std::uint8_t cast_anim{};
};

bool seh_read_motion_movement(PlayerCharacter* player,
    RawMotion* out_motion,
    const MobileObject*& mover) noexcept {
    return guarded_game_read([&] {
        if (!player || player->refID != kPlayerRefId || !player->parentCell || !out_motion) {
            return false;
        }
        const auto* mobile = static_cast<const MobileObject*>(player);
        BaseProcess* process = mobile->process;
        if (!process) {
            return false;
        }
        out_motion->has_process = 1;
        out_motion->movement_flags = process->GetMovementFlags();
        out_motion->action = process->GetCurrentAction();
        const Creature* mount = player->GetMountedHorse();
        mover = mount ? static_cast<const MobileObject*>(mount) : mobile;
        out_motion->mounted = mount != nullptr ? 1U : 0U;
        out_motion->has_mounted = 1;
        if (mount) {
            BaseProcess* mount_process = mover->process;
            if (mount_process) {
                out_motion->mount_movement_flags = mount_process->GetMovementFlags();
                out_motion->has_mount_movement_flags = 1;
            }
        }
        if (mover->parentCell == player->parentCell) {
            out_motion->horizontal_position = {.x = mover->posX,
                .y = mover->posY,
                .mover_id = mover->refID};
        }
        return true;
    });
}

void seh_read_motion_ground(const MobileObject* mover, RawMotion* out_motion) noexcept {
    const auto state = guarded_game_read([&] -> std::optional<std::uint8_t> {
        if (!mover || !mover->process) {
            return std::nullopt;
        }
        const auto* middle_high = static_cast<const MiddleHighProcess*>(
            Oblivion_DynamicCast(mover->process, 0, RTTI_BaseProcess, RTTI_MiddleHighProcess, 0));
        if (middle_high) {
            const auto* controller = static_cast<const bhkCharacterController*>(
                Oblivion_DynamicCast(middle_high->charProxy,
                    0,
                    RTTI_NiObject,
                    RTTI_bhkCharacterController,
                    0));
            if (controller) {
                return static_cast<std::uint8_t>(controller->ctx.hkState);
            }
        }
        return std::nullopt;
    });
    if (state) {
        out_motion->ground_state = *state;
        out_motion->has_ground = 1;
    }
}

void classify_animation_group(std::uint32_t group, RawMotion& motion) noexcept {
    if (group >= TESAnimGroup::kAnimGroup_AttackLeft &&
        group <= TESAnimGroup::kAnimGroup_AttackRightPower) {
        motion.melee_anim = 1;
    }
    if (group >= TESAnimGroup::kAnimGroup_AttackPower &&
        group <= TESAnimGroup::kAnimGroup_AttackRightPower) {
        motion.power_anim = 1;
    }
    if (group == TESAnimGroup::kAnimGroup_BlockIdle) {
        motion.block_idle = 1;
    } else if (group == TESAnimGroup::kAnimGroup_BlockHit) {
        motion.block_hit = 1;
    }
    if (group >= TESAnimGroup::kAnimGroup_CastSelf &&
        group <= TESAnimGroup::kAnimGroup_CastTargetAlt) {
        motion.cast_anim = 1;
    }
}

bool seh_read_motion_animations(const PlayerCharacter* player, RawMotion* out_motion) noexcept {
    return guarded_game_read([&] {
        if (!player || player->refID != kPlayerRefId || !player->parentCell || !out_motion) {
            return false;
        }
        BaseProcess* process = static_cast<const MobileObject*>(player)->process;
        if (!process) {
            return false;
        }
        const auto* middle_high = static_cast<const MiddleHighProcess*>(
            Oblivion_DynamicCast(process, 0, RTTI_BaseProcess, RTTI_MiddleHighProcess, 0));
        const ActorAnimData* animation_data = nullptr;
        if (player->isThirdPerson == 0) {
            animation_data = player->firstPersonAnimData;
        } else if (middle_high) {
            animation_data = middle_high->animData;
        }
        if (animation_data) {
            for (const auto* sequence : animation_data->animSequences) {
                if (!sequence || !sequence->animGroup) {
                    continue;
                }
                classify_animation_group(sequence->animGroup->animGroup, *out_motion);
            }
        }
        return true;
    });
}

bool seh_read_motion(PlayerCharacter* player, RawMotion* out_motion) noexcept {
    const MobileObject* mover = nullptr;
    if (!seh_read_motion_movement(player, out_motion, mover)) {
        return false;
    }
    seh_read_motion_ground(mover, out_motion);
    return seh_read_motion_animations(player, out_motion);
}

bool seh_read_shield(const PlayerCharacter* player, bool* out_shield) noexcept {
    return guarded_game_read([&] {
        if (!player || player->refID != kPlayerRefId || !player->parentCell || !out_shield) {
            return false;
        }
        *out_shield = false;
        const auto* mobile = static_cast<const MobileObject*>(player);
        BaseProcess* process = mobile->process;
        if (!process) {
            return false;
        }
        const auto* middle_high = static_cast<const MiddleHighProcess*>(
            Oblivion_DynamicCast(process, 0, RTTI_BaseProcess, RTTI_MiddleHighProcess, 0));
        if (!middle_high) {
            return true;
        }
        const ExtraContainerChanges::EntryData* entry = process->GetEquippedShieldData(false);
        *out_shield = entry != nullptr && entry->type != nullptr;
        return true;
    });
}

bool seh_read_action_stats(const PlayerCharacter* player, ActionStats* out_stats) noexcept {
    return guarded_game_read([&] {
        if (!player || player->refID != kPlayerRefId || !player->parentCell || !out_stats) {
            return false;
        }
        *out_stats = {
            .locks_picked = player->miscStats[PlayerCharacter::kMiscStat_LocksPicked],
            .lockpicks_broken = player->miscStats[PlayerCharacter::kMiscStat_LockpicksBroken],
            .training_sessions = player->miscStats[PlayerCharacter::kMiscStat_TrainingSessions],
            .items_pickpocketed = player->miscStats[PlayerCharacter::kMiscStat_ItemsPickpocketed],
            .gates_closed = player->miscStats[PlayerCharacter::kMiscStat_OblivionGatesShut],
        };
        return true;
    });
}

bool seh_scan_player_cell(const PlayerCharacter* player,
    std::uint32_t expected_cell_id,
    ProjectileScan* out_scan) noexcept {
    return guarded_game_read([&] {
        if (!player || player->refID != kPlayerRefId || !player->parentCell || !out_scan ||
            player->parentCell->refID != expected_cell_id) {
            return false;
        }
        const TESObjectCELL::ObjectListEntry* entry = &player->parentCell->objectList;
        std::size_t visited_refs = 0;
        while (entry && visited_refs < projectile_scan_limit) {
            ++visited_refs;
            TESObjectREFR* reference = entry->refr;
            entry = entry->next;
            if (!reference || !reference->baseForm ||
                reference->baseForm->typeID != kFormType_Ammo ||
                (static_cast<std::uint32_t>(reference->flags) &
                    static_cast<std::uint32_t>(TESObjectREFR::kFlags_Deleted)) != 0U) {
                continue;
            }
            const auto* arrow = static_cast<const ArrowProjectile*>(
                Oblivion_DynamicCast(reference, 0, RTTI_TESObjectREFR, RTTI_ArrowProjectile, 0));
            if (!arrow) {
                continue;
            }
            const std::uint32_t ref_id = arrow->refID;
            if (ref_id == 0U || !std::isfinite(arrow->elapsedTime)) {
                continue;
            }
            out_scan->arrows[out_scan->count++] = {.id = ref_id,
                .elapsed_seconds = arrow->elapsedTime,
                .player_owned = arrow->shooter == player};
        }
        return true;
    });
}

bool is_melee_action(std::int16_t action) noexcept {
    return action == HighProcess::kAction_Attack ||
           action == HighProcess::kAction_AttackFollowThrough;
}

void apply_attack_state(const RawMotion& motion,
    Weapon weapon,
    EffectObservation& observation) noexcept {
    const bool is_bow = weapon == Weapon::bow;
    if (is_bow && motion.action == HighProcess::kAction_AttackBowArrowAttached) {
        observation.bow = BowPhase::attached;
    } else if (is_bow && motion.action == HighProcess::kAction_AttackBow) {
        observation.bow = BowPhase::drawing;
    } else {
        observation.bow = BowPhase::none;
    }
    observation.casting = motion.cast_anim != 0U;
    observation.melee_attack = !is_bow && !observation.casting && motion.melee_anim != 0U &&
                               is_melee_action(motion.action);
    observation.power_attack = observation.melee_attack && motion.power_anim != 0U;
    observation.blocking = motion.action == HighProcess::kAction_Block || motion.block_idle != 0U;
    observation.block_hit = motion.block_hit != 0U;
}

Gait classify_gait(std::uint32_t movement_flags, bool mounted) noexcept {
    constexpr std::uint32_t kTranslationalFlags =
        static_cast<std::uint32_t>(HighProcess::kMovement_Forward) |
        static_cast<std::uint32_t>(HighProcess::kMovement_Backward) |
        static_cast<std::uint32_t>(HighProcess::kMovement_Left) |
        static_cast<std::uint32_t>(HighProcess::kMovement_Right);
    const bool translating = (movement_flags & kTranslationalFlags) != 0U;
    const bool is_running =
        (movement_flags & static_cast<std::uint32_t>(HighProcess::kMovement_Run)) != 0U;
    const bool is_walking =
        (movement_flags & static_cast<std::uint32_t>(HighProcess::kMovement_Walk)) != 0U;
    const bool is_sneaking =
        (movement_flags & static_cast<std::uint32_t>(HighProcess::kMovement_Sneak)) != 0U;
    if (!translating) {
        return Gait::none;
    }
    if (mounted && is_running) {
        return Gait::mounted_run;
    }
    if (mounted && is_walking) {
        return Gait::mounted_walk;
    }
    if (is_sneaking) {
        return Gait::sneak;
    }
    if (is_running) {
        return Gait::run;
    }
    if (is_walking) {
        return Gait::walk;
    }
    return Gait::none;
}

void apply_movement_state(const RawMotion& motion, EffectObservation& observation) noexcept {
    if (motion.has_mounted != 0U) {
        observation.mounted = motion.mounted != 0U;
    }
    const bool mounted = observation.mounted.value_or(false);
    const std::uint32_t gait_flags = mounted && motion.has_mount_movement_flags != 0U
                                         ? motion.mount_movement_flags
                                         : motion.movement_flags;
    constexpr auto kSwimmingFlag = static_cast<std::uint32_t>(BaseProcess::kMovementFlag_Swimming);
    observation.swimming = ((motion.movement_flags | gait_flags) & kSwimmingFlag) != 0U;
    if (!observation.swimming && (!mounted || motion.has_mount_movement_flags != 0U)) {
        observation.gait = classify_gait(gait_flags, mounted);
    }
    const auto& position = motion.horizontal_position;
    if (position.mover_id != 0U && std::isfinite(position.x) && std::isfinite(position.y)) {
        observation.horizontal_position = position;
    }
}

void apply_ground_state(const RawMotion& motion, EffectObservation& observation) noexcept {
    if (motion.has_ground == 0U) {
        return;
    }
    switch (motion.ground_state) {
    case kHKState_OnGround:
        observation.ground = Ground::grounded;
        break;
    case kHKState_Jumping:
    case kHKState_InAir:
    case kHKState_Climbing:
    case kHKState_Flying:
        observation.ground = Ground::airborne;
        break;
    default:
        observation.ground = Ground::unknown;
        break;
    }
}

void apply_motion_state(const RawMotion& motion,
    Weapon weapon,
    EffectObservation& observation) noexcept {
    apply_attack_state(motion, weapon, observation);
    apply_movement_state(motion, observation);
    apply_ground_state(motion, observation);
}

void read_effect_resources(PlayerCharacter* player, EffectObservation& observation) noexcept {
    bool shield = false;
    if (seh_read_shield(player, &shield)) {
        observation.shield = shield;
    }

    RawRatios ratios{};
    if (seh_read_ratios(player, &ratios)) {
        observation.magicka_ratio = ratios.magicka;
        observation.fatigue_ratio = ratios.fatigue;
    }

    ActionStats action_stats{};
    if (seh_read_action_stats(player, &action_stats)) {
        observation.action_stats = action_stats;
    }
}

} // namespace

PlayerCharacter* PlayerStateReader::get_player() noexcept {
    return guarded_game_read([] { return g_thePlayer ? *g_thePlayer : nullptr; });
}

bool PlayerStateReader::is_player_pointer_valid(const PlayerCharacter* player) noexcept {
    return seh_validate_pointer(player);
}

bool PlayerStateReader::is_game_mode() noexcept {
    return guarded_game_read([&] {
        auto* interface_manager = InterfaceManager::GetSingleton();
        if (!interface_manager) {
            return false;
        }
        return !IsConsoleOpen() && interface_manager->IsGameMode();
    });
}

EffectMode PlayerStateReader::read_effect_mode() noexcept {
    RawMenuState state{};
    if (!seh_read_menu(&state)) {
        return EffectMode::unavailable;
    }
    if (state.game_mode) {
        return EffectMode::gameplay;
    }
    switch (state.top_menu) {
    case kMenuType_Inventory:
        return EffectMode::inventory;
    case kMenuType_Magic:
        return EffectMode::magic;
    case kMenuType_Alchemy:
        return EffectMode::alchemy;
    case kMenuType_Enchantment:
        return EffectMode::enchantment;
    case kMenuType_Spellmaking:
        return EffectMode::spellmaking;
    case kMenuType_LockPick:
        return EffectMode::lockpick;
    case kMenuType_Training:
        return EffectMode::training;
    case kMenuType_Container:
        return EffectMode::container;
    case kMenuType_Loading:
        return EffectMode::loading;
    case kMenuType_Main:
        return EffectMode::unavailable;
    default:
        return EffectMode::menu;
    }
}

std::optional<PlayerHealth> PlayerStateReader::read_player_health(
    PlayerCharacter* player) noexcept {
    RawHealthValues raw{};
    if (!seh_read_health(player, &raw)) {
        return std::nullopt;
    }
    const auto health = calculate_health(raw.current, raw.max);
    if (!health.valid) {
        return std::nullopt;
    }
    return health;
}

std::optional<bool> PlayerStateReader::read_player_alive(PlayerCharacter* player) noexcept {
    bool alive = false;
    if (seh_read_alive(player, &alive)) {
        return alive;
    }
    return std::nullopt;
}

std::optional<bool> PlayerStateReader::read_player_in_combat(PlayerCharacter* player) noexcept {
    bool in_combat = false;
    if (seh_read_combat(player, &in_combat)) {
        return in_combat;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> PlayerStateReader::read_player_form_id(
    const PlayerCharacter* player) noexcept {
    std::uint32_t form_id = 0;
    if (seh_read_form_id(player, &form_id)) {
        return form_id;
    }
    return std::nullopt;
}

std::uint32_t PlayerStateReader::read_player_cell_id(PlayerCharacter* player) noexcept {
    RawLocation location{};
    return seh_read_location(player, &location) ? location.cell_id : 0;
}

Weapon PlayerStateReader::read_weapon(const TESForm* form) noexcept {
    Weapon weapon = Weapon::unknown;
    return seh_read_weapon(form, &weapon) ? weapon : Weapon::unknown;
}

Weapon PlayerStateReader::read_equipped_weapon(PlayerCharacter* player,
    std::uint32_t& out_id) noexcept {
    Weapon weapon = Weapon::unknown;
    std::uint32_t form_id = 0;
    if (!seh_read_equipped_weapon(player, &weapon, &form_id)) {
        out_id = 0;
        return Weapon::unknown;
    }
    out_id = form_id;
    return weapon;
}

bool PlayerStateReader::read_projectile_actor(const TESForm* form,
    const PlayerCharacter* player) noexcept {
    bool is_player_arrow = false;
    return seh_is_projectile_actor(form, player, &is_player_arrow) && is_player_arrow;
}

void PlayerStateReader::read_effect_state(PlayerCharacter* player,
    EffectObservation& observation,
    bool read_stealth) noexcept {
    if (!seh_validate_pointer(player)) {
        return;
    }

    RawLocation location{};
    if (seh_read_location(player, &location)) {
        observation.cell_id = location.cell_id;
        observation.position_z = location.position_z;
    }

    std::uint32_t weapon_id = 0;
    const Weapon weapon = read_equipped_weapon(player, weapon_id);
    observation.weapon = weapon;
    observation.weapon_id = weapon_id;

    RawMotion motion{};
    if (seh_read_motion(player, &motion) && motion.has_process != 0U) {
        apply_motion_state(motion, weapon, observation);
        constexpr auto kSneaking = static_cast<std::uint32_t>(BaseProcess::kMovementFlag_Sneaking);
        if (read_stealth && observation.mode == EffectMode::gameplay && !observation.swimming &&
            !observation.mounted.value_or(true) && (motion.movement_flags & kSneaking) != 0U) {
            observation.sneak_detected = read_sneak_eye_detection();
        }
    }
    read_effect_resources(player, observation);
}

bool PlayerStateReader::scan_projectiles(PlayerCharacter* player,
    std::uint32_t cell_id,
    ProjectileScan& scan) noexcept {
    scan.count = 0;
    return seh_validate_pointer(player) && seh_scan_player_cell(player, cell_id, &scan);
}

} // namespace oblivion_adapter::native
