#include "binding_test_support.hpp"
#include "native/skse_sdk.hpp"
#include "native/translators.hpp"
#include "skyrim_adapter/model.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace skyrim_le::binding_tests {
namespace {

// Action events and hand mapping.
void test_action_translation() {
    using namespace skyrim_adapter;
    using namespace skyrim_adapter::native::detail;

    constexpr std::array<std::pair<UInt32, EventKind>, 11> cases{{
        {SKSEActionEvent::kType_WeaponSwing, EventKind::weapon_swing},
        {SKSEActionEvent::kType_SpellCast, EventKind::spell_cast},
        {SKSEActionEvent::kType_SpellFire, EventKind::spell_fire},
        {SKSEActionEvent::kType_VoiceCast, EventKind::voice_cast},
        {SKSEActionEvent::kType_VoiceFire, EventKind::voice_fire},
        {SKSEActionEvent::kType_BowDraw, EventKind::bow_draw},
        {SKSEActionEvent::kType_BowRelease, EventKind::bow_release},
        {SKSEActionEvent::kType_BeginDraw, EventKind::begin_draw},
        {SKSEActionEvent::kType_EndDraw, EventKind::end_draw},
        {SKSEActionEvent::kType_BeginSheathe, EventKind::begin_sheathe},
        {SKSEActionEvent::kType_EndSheathe, EventKind::end_sheathe},
    }};

    for (const auto& [source, expected] : cases) {
        const auto translated = action_event_kind(source);
        check(translated.has_value() && *translated == expected,
            "known action event type translates correctly");
    }

    check(!action_event_kind(11), "unknown action event 11 remains absent");
    check(!action_event_kind(100), "unknown action event 100 remains absent");
    check(!action_event_kind(0xFFFFFFFFU), "unknown action event 0xFFFFFFFF remains absent");

    const auto release = action_event_kind(SKSEActionEvent::kType_BowRelease);
    check(release.has_value() && *release == EventKind::bow_release &&
              *release != EventKind::player_bow_shot,
        "BowRelease is never translated as an actual shot");

    check(event_hand(SKSEActionEvent::kSlot_Left) == Hand::left, "left action hand");
    check(event_hand(SKSEActionEvent::kSlot_Right) == Hand::right, "right action hand");
    check(event_hand(SKSEActionEvent::kSlot_Voice) == Hand::voice, "voice action hand");
    check(event_hand(3) == Hand::unknown, "unknown action slot 3 is neutral");
    check(event_hand(0xFFFFFFFFU) == Hand::unknown, "unknown action slot 0xFFFFFFFF is neutral");
}

// Actor state, movement, gait, and equipment translation.
void test_actor_and_equipment_translation() {
    using namespace skyrim_adapter;
    using namespace skyrim_adapter::native::detail;

    constexpr auto walking = static_cast<UInt32>(ActorState::kState_Walking);
    constexpr auto running = static_cast<UInt32>(ActorState::kState_Running);
    constexpr auto sprinting = static_cast<UInt32>(ActorState::kState_Sprinting);
    constexpr auto sneaking = static_cast<UInt32>(ActorState::kState_Sneaking);
    constexpr auto swimming = static_cast<UInt32>(ActorState::kState_Swimming);

    check(actor_is_moving(walking), "walking translates to moving");
    check(actor_is_moving(running), "running translates to moving");
    check(actor_is_moving(sprinting), "sprinting translates to moving");
    check(actor_is_moving(sneaking), "sneaking translates to moving");
    check(actor_is_moving(walking | running), "walking and running flags translate to moving");
    check(!actor_is_moving(0), "zero flags is not moving");
    check(!actor_is_moving(swimming), "swimming alone is not moving in actor_is_moving");
    check(!actor_is_moving(0x80000000U), "unrecognized movement bit is not moving");

    check(actor_is_swimming(swimming), "swimming flag is detected");
    check(!actor_is_swimming(0), "zero flags is not swimming");
    check(!actor_is_swimming(running), "running flag is not swimming");
    check(actor_is_swimming(swimming | running), "swimming with running is detected");

    // Movement gait priority: sprinting > sneaking > running > walking
    check(movement_gait(sprinting) == MovementGait::sprinting, "sprinting gait");
    check(movement_gait(sneaking) == MovementGait::sneaking, "sneaking gait");
    check(movement_gait(running) == MovementGait::running, "running gait");
    check(movement_gait(walking) == MovementGait::walking, "walking gait");

    check(movement_gait(sprinting | running) == MovementGait::sprinting,
        "sprinting takes priority over running");
    check(movement_gait(sprinting | sneaking) == MovementGait::sprinting,
        "sprinting takes priority over sneaking");
    check(movement_gait(sprinting | walking) == MovementGait::sprinting,
        "sprinting takes priority over walking");
    check(movement_gait(sneaking | running) == MovementGait::sneaking,
        "sneaking takes priority over running");
    check(movement_gait(sneaking | walking) == MovementGait::sneaking,
        "sneaking takes priority over walking");
    check(movement_gait(running | walking) == MovementGait::running,
        "running takes priority over walking");
    check(movement_gait(sprinting | sneaking | running | walking) == MovementGait::sprinting,
        "sprinting takes top priority when all movement flags are present");

    check(movement_gait(0) == MovementGait::unknown, "no movement flags produces unknown gait");
    check(movement_gait(swimming) == MovementGait::unknown,
        "swimming without moving flags produces unknown gait");
    check(movement_gait(0x80000000U) == MovementGait::unknown, "unknown bit produces unknown gait");

    using Weapon = TESObjectWEAP::GameData;
    check(weapon_class(Weapon::kType_HandToHandMelee) == WeaponClass::unarmed,
        "hand to hand melee weapon class");
    check(weapon_class(Weapon::kType_OneHandSword) == WeaponClass::sword, "one-handed sword class");
    check(weapon_class(Weapon::kType_OneHandDagger) == WeaponClass::dagger,
        "one-handed dagger class");
    check(weapon_class(Weapon::kType_OneHandAxe) == WeaponClass::axe, "one-handed axe class");
    check(weapon_class(Weapon::kType_OneHandMace) == WeaponClass::mace, "one-handed mace class");
    check(weapon_class(Weapon::kType_TwoHandSword) == WeaponClass::two_handed,
        "two-handed sword class");
    check(weapon_class(Weapon::kType_TwoHandAxe) == WeaponClass::two_handed,
        "two-handed axe class");
    check(weapon_class(Weapon::kType_Bow) == WeaponClass::bow, "bow weapon class");
    check(weapon_class(Weapon::kType_CrossBow) == WeaponClass::crossbow, "crossbow weapon class");
    check(weapon_class(Weapon::kType_Staff) == WeaponClass::none, "staff weapon class is none");
    check(weapon_class(0xFF) == WeaponClass::unknown, "unrecognized weapon class stays unknown");

    check(weapon_equipment(Weapon::kType_HandToHandMelee) == EquipmentKind::weapon,
        "unarmed equipment");
    check(weapon_equipment(Weapon::kType_OneHandSword) == EquipmentKind::weapon, "sword equipment");
    check(weapon_equipment(Weapon::kType_OneHandDagger) == EquipmentKind::weapon,
        "dagger equipment");
    check(weapon_equipment(Weapon::kType_OneHandAxe) == EquipmentKind::weapon, "axe equipment");
    check(weapon_equipment(Weapon::kType_OneHandMace) == EquipmentKind::weapon, "mace equipment");
    check(weapon_equipment(Weapon::kType_TwoHandSword) == EquipmentKind::weapon,
        "two-hand sword equipment");
    check(weapon_equipment(Weapon::kType_TwoHandAxe) == EquipmentKind::weapon,
        "two-hand axe equipment");
    check(weapon_equipment(Weapon::kType_Bow) == EquipmentKind::bow, "bow equipment");
    check(weapon_equipment(Weapon::kType_CrossBow) == EquipmentKind::crossbow,
        "crossbow equipment");
    check(weapon_equipment(Weapon::kType_Staff) == EquipmentKind::staff, "staff equipment");
    check(weapon_equipment(0xFF) == EquipmentKind::unknown,
        "unrecognized equipment type stays unknown");

    check(!weapon_is_two_handed(Weapon::kType_HandToHandMelee), "unarmed is not two-handed");
    check(!weapon_is_two_handed(Weapon::kType_OneHandSword), "one-hand sword is not two-handed");
    check(!weapon_is_two_handed(Weapon::kType_OneHandDagger), "one-hand dagger is not two-handed");
    check(!weapon_is_two_handed(Weapon::kType_OneHandAxe), "one-hand axe is not two-handed");
    check(!weapon_is_two_handed(Weapon::kType_OneHandMace), "one-hand mace is not two-handed");
    check(weapon_is_two_handed(Weapon::kType_TwoHandSword), "two-hand sword is two-handed");
    check(weapon_is_two_handed(Weapon::kType_TwoHandAxe), "two-hand axe is two-handed");
    check(weapon_is_two_handed(Weapon::kType_Bow), "bow is two-handed");
    check(weapon_is_two_handed(Weapon::kType_CrossBow), "crossbow is two-handed");
    check(!weapon_is_two_handed(Weapon::kType_Staff), "staff is not two-handed");
    check(!weapon_is_two_handed(0xFF), "unrecognized type is not two-handed");

    check(melee_weapon(WeaponClass::unarmed), "unarmed is melee");
    check(melee_weapon(WeaponClass::dagger), "dagger is melee");
    check(melee_weapon(WeaponClass::sword), "sword is melee");
    check(melee_weapon(WeaponClass::axe), "axe is melee");
    check(melee_weapon(WeaponClass::mace), "mace is melee");
    check(melee_weapon(WeaponClass::two_handed), "two_handed is melee");
    check(!melee_weapon(WeaponClass::none), "none is not melee");
    check(!melee_weapon(WeaponClass::bow), "bow is not melee");
    check(!melee_weapon(WeaponClass::crossbow), "crossbow is not melee");
    check(!melee_weapon(WeaponClass::unknown), "unknown is not melee");
}

// Post-load message decoding.
void test_post_load_message_decoding() {
    using skyrim_adapter::native::detail::post_load_succeeded;
    check(post_load_succeeded(1, std::uintptr_t{1}), "encoded successful post-load message");
    check(!post_load_succeeded(1, std::uintptr_t{0}), "encoded failed post-load message");
    check(!post_load_succeeded(1, std::uintptr_t{2}), "encoded invalid value 2 is rejected");
    check(!post_load_succeeded(0, std::uintptr_t{1}), "zero post-load length is rejected");
    check(!post_load_succeeded(2, std::uintptr_t{1}), "length 2 is rejected");
    check(!post_load_succeeded(4, std::uintptr_t{1}),
        "post-load pointer is never treated as a 4-byte buffer");
}

// Optional race lookup and FormID composition.
void test_optional_race_lookup_validation() {
    using namespace skyrim_adapter::native::detail;
    constexpr UInt32 raw_race_type = kFormType_Race;
    constexpr ExpectedFormType race_type{.value = raw_race_type};

    const OptionalRaceRecord missing{};
    check(!validate_optional_race_record(missing,
              skyrim_master_name,
              werewolf_race_local_id,
              race_type),
        "missing master disables optional form");

    const OptionalRaceRecord disabled{
        .has_loaded_entry = true,
        .mod_index = 0xFF,
        .loaded_slot = 0xFF,
        .loaded_name = "Skyrim.esm",
        .returned_form_id = 0x000CDD84,
        .returned_form_type = raw_race_type,
    };
    check(!validate_optional_race_record(disabled,
              skyrim_master_name,
              werewolf_race_local_id,
              race_type),
        "disabled plugin index 0xFF disables optional form");

    const OptionalRaceRecord slot_zero{
        .has_loaded_entry = true,
        .mod_index = 0x00,
        .loaded_slot = 0x00,
        .loaded_name = "Skyrim.esm",
        .returned_form_id = 0x000CDD84,
        .returned_form_type = raw_race_type,
    };
    const auto resolved_zero = validate_optional_race_record(slot_zero,
        skyrim_master_name,
        werewolf_race_local_id,
        race_type);
    check(resolved_zero.has_value() && *resolved_zero == 0x000CDD84,
        "slot 0 resolves werewolf FormID");

    // Case-insensitivity and arbitrary loaded slot.
    const OptionalRaceRecord reordered{
        .has_loaded_entry = true,
        .mod_index = 0x17,
        .loaded_slot = 0x17,
        .loaded_name = "sKyRiM.EsM",
        .returned_form_id = 0x170CDD84,
        .returned_form_type = raw_race_type,
    };
    const auto resolved = validate_optional_race_record(reordered,
        skyrim_master_name,
        werewolf_race_local_id,
        race_type);
    check(resolved.has_value() && *resolved == 0x170CDD84,
        "arbitrary loaded plugin index composes the race FormID");

    const OptionalRaceRecord high_mod_index{
        .has_loaded_entry = true,
        .mod_index = 0xFE,
        .loaded_slot = 0xFE,
        .loaded_name = "SKYRIM.ESM",
        .returned_form_id = 0xFE0CDD84,
        .returned_form_type = raw_race_type,
    };
    const auto resolved_high = validate_optional_race_record(high_mod_index,
        skyrim_master_name,
        werewolf_race_local_id,
        race_type);
    check(resolved_high.has_value() && *resolved_high == 0xFE0CDD84,
        "high mod index 0xFE resolves correctly");

    const OptionalRaceRecord wrong_slot{
        .has_loaded_entry = true,
        .mod_index = 0x17,
        .loaded_slot = 0x03,
        .loaded_name = "Skyrim.esm",
        .returned_form_id = 0x170CDD84,
        .returned_form_type = raw_race_type,
    };
    check(!validate_optional_race_record(wrong_slot,
              skyrim_master_name,
              werewolf_race_local_id,
              race_type),
        "loaded table slot must match plugin index");

    const OptionalRaceRecord wrong_plugin{
        .has_loaded_entry = true,
        .mod_index = 0x17,
        .loaded_slot = 0x17,
        .loaded_name = "Other.esm",
        .returned_form_id = 0x170CDD84,
        .returned_form_type = raw_race_type,
    };
    check(!validate_optional_race_record(wrong_plugin,
              skyrim_master_name,
              werewolf_race_local_id,
              race_type),
        "wrong plugin cannot satisfy optional lookup");

    const OptionalRaceRecord prefix_name{
        .has_loaded_entry = true,
        .mod_index = 0x17,
        .loaded_slot = 0x17,
        .loaded_name = "Skyrim",
        .returned_form_id = 0x170CDD84,
        .returned_form_type = raw_race_type,
    };
    check(!validate_optional_race_record(prefix_name,
              skyrim_master_name,
              werewolf_race_local_id,
              race_type),
        "truncated plugin name is rejected");

    const OptionalRaceRecord suffix_name{
        .has_loaded_entry = true,
        .mod_index = 0x17,
        .loaded_slot = 0x17,
        .loaded_name = "Skyrim.esm.esp",
        .returned_form_id = 0x170CDD84,
        .returned_form_type = raw_race_type,
    };
    check(!validate_optional_race_record(suffix_name,
              skyrim_master_name,
              werewolf_race_local_id,
              race_type),
        "extended plugin name is rejected");

    const OptionalRaceRecord empty_name{
        .has_loaded_entry = true,
        .mod_index = 0x17,
        .loaded_slot = 0x17,
        .loaded_name = "",
        .returned_form_id = 0x170CDD84,
        .returned_form_type = raw_race_type,
    };
    check(!validate_optional_race_record(empty_name,
              skyrim_master_name,
              werewolf_race_local_id,
              race_type),
        "empty plugin name is rejected");

    const OptionalRaceRecord wrong_type{
        .has_loaded_entry = true,
        .mod_index = 0x17,
        .loaded_slot = 0x17,
        .loaded_name = "Skyrim.esm",
        .returned_form_id = 0x170CDD84,
        .returned_form_type = kFormType_Weapon,
    };
    check(!validate_optional_race_record(wrong_type,
              skyrim_master_name,
              werewolf_race_local_id,
              race_type),
        "wrong form type cannot satisfy optional race lookup");

    const OptionalRaceRecord mismatched_id{
        .has_loaded_entry = true,
        .mod_index = 0x17,
        .loaded_slot = 0x17,
        .loaded_name = "Skyrim.esm",
        .returned_form_id = 0x180CDD84,
        .returned_form_type = raw_race_type,
    };
    check(!validate_optional_race_record(mismatched_id,
              skyrim_master_name,
              werewolf_race_local_id,
              race_type),
        "returned FormID must match the loaded plugin index and local ID");

    // 24-bit local FormID boundary.
    const OptionalRaceRecord high_local_id{
        .has_loaded_entry = true,
        .mod_index = 0x17,
        .loaded_slot = 0x17,
        .loaded_name = "Skyrim.esm",
        .returned_form_id = 0x170CDD84,
        .returned_form_type = raw_race_type,
    };
    check(!validate_optional_race_record(high_local_id, skyrim_master_name, 0x010CDD84, race_type),
        "optional local FormID exceeding 24 bits is rejected");
    check(!validate_optional_race_record(high_local_id, skyrim_master_name, 0x01000000, race_type),
        "local FormID with bit 24 set is rejected");

    const OptionalRaceRecord max_24bit{
        .has_loaded_entry = true,
        .mod_index = 0x05,
        .loaded_slot = 0x05,
        .loaded_name = "Skyrim.esm",
        .returned_form_id = 0x05FFFFFF,
        .returned_form_type = raw_race_type,
    };
    const auto resolved_max =
        validate_optional_race_record(max_24bit, skyrim_master_name, 0x00FFFFFF, race_type);
    check(resolved_max.has_value() && *resolved_max == 0x05FFFFFF,
        "maximum 24-bit local FormID is accepted");

    const OptionalRaceRecord dawnguard{
        .has_loaded_entry = true,
        .mod_index = 0x2A,
        .loaded_slot = 0x2A,
        .loaded_name = "Dawnguard.esm",
        .returned_form_id = 0x2A00283A,
        .returned_form_type = raw_race_type,
    };
    const auto vampire = validate_optional_race_record(dawnguard,
        dawnguard_name,
        vampire_lord_race_local_id,
        race_type);
    check(vampire.has_value() && *vampire == 0x2A00283A,
        "Dawnguard vampire record follows its loaded index");
}

} // namespace

void run_translation_tests() {
    test_action_translation();
    test_actor_and_equipment_translation();
    test_post_load_message_decoding();
    test_optional_race_lookup_validation();
}
} // namespace skyrim_le::binding_tests
