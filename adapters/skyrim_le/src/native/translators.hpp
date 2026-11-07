#pragma once

#include "native/skse_sdk.hpp"

#include <skyrim_adapter/blocking_menus.hpp>
#include <skyrim_adapter/model.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

namespace skyrim_adapter::native::detail {
[[nodiscard]] inline std::optional<EventKind> action_event_kind(UInt32 type) noexcept {
    switch (type) {
    case SKSEActionEvent::kType_WeaponSwing:
        return EventKind::weapon_swing;
    case SKSEActionEvent::kType_SpellCast:
        return EventKind::spell_cast;
    case SKSEActionEvent::kType_SpellFire:
        return EventKind::spell_fire;
    case SKSEActionEvent::kType_VoiceCast:
        return EventKind::voice_cast;
    case SKSEActionEvent::kType_VoiceFire:
        return EventKind::voice_fire;
    case SKSEActionEvent::kType_BowDraw:
        return EventKind::bow_draw;
    case SKSEActionEvent::kType_BowRelease:
        return EventKind::bow_release;
    case SKSEActionEvent::kType_BeginDraw:
        return EventKind::begin_draw;
    case SKSEActionEvent::kType_EndDraw:
        return EventKind::end_draw;
    case SKSEActionEvent::kType_BeginSheathe:
        return EventKind::begin_sheathe;
    case SKSEActionEvent::kType_EndSheathe:
        return EventKind::end_sheathe;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] inline Hand event_hand(UInt32 slot) noexcept {
    switch (slot) {
    case SKSEActionEvent::kSlot_Left:
        return Hand::left;
    case SKSEActionEvent::kSlot_Right:
        return Hand::right;
    case SKSEActionEvent::kSlot_Voice:
        return Hand::voice;
    default:
        return Hand::unknown;
    }
}

[[nodiscard]] inline bool actor_is_moving(UInt32 flags) noexcept {
    constexpr UInt32 moving = static_cast<UInt32>(ActorState::kState_Running) |
                              static_cast<UInt32>(ActorState::kState_Walking) |
                              static_cast<UInt32>(ActorState::kState_Sprinting) |
                              static_cast<UInt32>(ActorState::kState_Sneaking);
    return (flags & moving) != 0;
}

[[nodiscard]] inline bool actor_is_swimming(UInt32 flags) noexcept {
    return (flags & static_cast<UInt32>(ActorState::kState_Swimming)) != 0;
}

[[nodiscard]] inline MovementGait movement_gait(UInt32 flags) noexcept {
    if (!actor_is_moving(flags)) {
        return MovementGait::unknown;
    }
    if ((flags & static_cast<UInt32>(ActorState::kState_Sprinting)) != 0) {
        return MovementGait::sprinting;
    }
    if ((flags & static_cast<UInt32>(ActorState::kState_Sneaking)) != 0) {
        return MovementGait::sneaking;
    }
    if ((flags & static_cast<UInt32>(ActorState::kState_Running)) != 0) {
        return MovementGait::running;
    }
    if ((flags & static_cast<UInt32>(ActorState::kState_Walking)) != 0) {
        return MovementGait::walking;
    }
    return MovementGait::unknown;
}

[[nodiscard]] inline WeaponClass weapon_class(UInt8 type) noexcept {
    using Weapon = TESObjectWEAP::GameData;
    switch (type) {
    case Weapon::kType_HandToHandMelee:
        return WeaponClass::unarmed;
    case Weapon::kType_OneHandSword:
        return WeaponClass::sword;
    case Weapon::kType_OneHandDagger:
        return WeaponClass::dagger;
    case Weapon::kType_OneHandAxe:
        return WeaponClass::axe;
    case Weapon::kType_OneHandMace:
        return WeaponClass::mace;
    case Weapon::kType_TwoHandSword:
    case Weapon::kType_TwoHandAxe:
        return WeaponClass::two_handed;
    case Weapon::kType_Bow:
        return WeaponClass::bow;
    case Weapon::kType_CrossBow:
        return WeaponClass::crossbow;
    case Weapon::kType_Staff:
        return WeaponClass::none;
    default:
        return WeaponClass::unknown;
    }
}

[[nodiscard]] inline EquipmentKind weapon_equipment(UInt8 type) noexcept {
    using Weapon = TESObjectWEAP::GameData;
    switch (type) {
    case Weapon::kType_HandToHandMelee:
    case Weapon::kType_OneHandSword:
    case Weapon::kType_OneHandDagger:
    case Weapon::kType_OneHandAxe:
    case Weapon::kType_OneHandMace:
    case Weapon::kType_TwoHandSword:
    case Weapon::kType_TwoHandAxe:
        return EquipmentKind::weapon;
    case Weapon::kType_Bow:
        return EquipmentKind::bow;
    case Weapon::kType_CrossBow:
        return EquipmentKind::crossbow;
    case Weapon::kType_Staff:
        return EquipmentKind::staff;
    default:
        return EquipmentKind::unknown;
    }
}

[[nodiscard]] inline bool weapon_is_two_handed(UInt8 type) noexcept {
    using Weapon = TESObjectWEAP::GameData;
    return type == Weapon::kType_TwoHandSword || type == Weapon::kType_TwoHandAxe ||
           type == Weapon::kType_Bow || type == Weapon::kType_CrossBow;
}

[[nodiscard]] inline bool melee_weapon(WeaponClass type) noexcept {
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

[[nodiscard]] constexpr bool post_load_succeeded(std::uint32_t data_length,
    std::uintptr_t encoded_result) noexcept {
    // SKSE 1.7.3 passes the bool value as an encoded pointer, not an address to read.
    return data_length == 1 && encoded_result == std::uintptr_t{1};
}

struct OptionalRaceRecord {
    bool has_loaded_entry{};
    UInt8 mod_index{0xFF};
    UInt8 loaded_slot{0xFF};
    std::string_view loaded_name;
    FormId returned_form_id{};
    UInt32 returned_form_type{};
};

[[nodiscard]] inline bool ascii_equal_case_insensitive(std::string_view left,
    std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        char actual = left[index];
        if (actual >= 'A' && actual <= 'Z') {
            actual = static_cast<char>(actual - 'A' + 'a');
        }
        char expected = right[index];
        if (expected >= 'A' && expected <= 'Z') {
            expected = static_cast<char>(expected - 'A' + 'a');
        }
        if (actual != expected) {
            return false;
        }
    }
    return true;
}

struct ExpectedFormType {
    UInt32 value{};
};

[[nodiscard]] inline std::optional<FormId> validate_optional_race_record(
    const OptionalRaceRecord& record,
    std::string_view expected_plugin,
    FormId local_form_id,
    ExpectedFormType expected_form_type) noexcept {
    constexpr FormId local_id_mask = 0x00FFFFFF;
    if (!record.has_loaded_entry || record.mod_index == 0xFF ||
        record.loaded_slot != record.mod_index || local_form_id > local_id_mask ||
        !ascii_equal_case_insensitive(record.loaded_name, expected_plugin)) {
        return std::nullopt;
    }

    const auto expected_form_id = (static_cast<FormId>(record.mod_index) << 24U) | local_form_id;
    if (record.returned_form_id != expected_form_id ||
        record.returned_form_type != expected_form_type.value) {
        return std::nullopt;
    }
    return expected_form_id;
}

inline constexpr std::string_view skyrim_master_name = "Skyrim.esm";
inline constexpr std::string_view dawnguard_name = "Dawnguard.esm";
inline constexpr FormId werewolf_race_local_id = 0x000CDD84;
inline constexpr FormId vampire_lord_race_local_id = 0x0000283A;

inline constexpr std::size_t menu_name_capacity = 128;
inline constexpr std::size_t open_menu_capacity = 32;
// Skyrim LE IMenu flags: PauseGame 0x01, DoNotDeleteOnClose 0x02, ShowCursor 0x04.
inline constexpr std::uint32_t le_menu_pause_game = 0x01;
inline constexpr std::uint32_t le_menu_do_not_delete_on_close = 0x02;
inline constexpr std::uint32_t le_menu_show_cursor = 0x04;

enum class MenuBlockReason : std::uint8_t {
    clear,
    missing_instance,
    pause,
    cursor,
    pause_and_cursor,
    snapshot_unreliable,
    manager_unavailable,
};

struct MenuDecision {
    bool blocks_gameplay{};
    MenuBlockReason reason{MenuBlockReason::clear};
};

[[nodiscard]] constexpr MenuDecision
classify_menu(bool is_open, bool has_instance, std::uint32_t flags) noexcept {
    if (!is_open) {
        return {};
    }
    if (!has_instance) {
        return {.blocks_gameplay = true, .reason = MenuBlockReason::missing_instance};
    }

    const bool pauses_game = (flags & le_menu_pause_game) != 0;
    const bool shows_cursor = (flags & le_menu_show_cursor) != 0;
    if (pauses_game && shows_cursor) {
        return {.blocks_gameplay = true, .reason = MenuBlockReason::pause_and_cursor};
    }
    if (pauses_game) {
        return {.blocks_gameplay = true, .reason = MenuBlockReason::pause};
    }
    if (shows_cursor) {
        return {.blocks_gameplay = true, .reason = MenuBlockReason::cursor};
    }
    return {};
}

struct OpenMenuName {
    std::array<char, menu_name_capacity> bytes{};
    std::size_t length{};
};

struct MenuSnapshot {
    std::array<OpenMenuName, open_menu_capacity> open{};
    std::size_t count{};
    bool unreliable{};
};

[[nodiscard]] constexpr bool menu_snapshot_is_reliable(const MenuSnapshot& snapshot) noexcept {
    return !snapshot.unreliable && snapshot.count <= snapshot.open.size();
}

[[nodiscard]] inline bool menu_event_requires_immediate_suppression(const MenuSnapshot& snapshot,
    std::string_view menu_name,
    bool opening) noexcept {
    return !menu_snapshot_is_reliable(snapshot) ||
           (opening && skyrim_adapter::is_known_blocking_menu(menu_name));
}

inline void apply_menu_event(MenuSnapshot& snapshot, const char* name, bool opening) noexcept {
    if (snapshot.count > snapshot.open.size()) {
        snapshot.unreliable = true;
        snapshot.count = 0;
        return;
    }

    if (!name || !*name) {
        snapshot.unreliable = true;
        return;
    }

    std::size_t length{};
    while (length < menu_name_capacity && name[length] != '\0') {
        ++length;
    }
    if (length == 0 || length == menu_name_capacity) {
        snapshot.unreliable = true;
        return;
    }

    std::size_t found = snapshot.count;
    for (std::size_t index = 0; index < snapshot.count; ++index) {
        const auto& existing = snapshot.open[index];
        if (existing.length == length &&
            std::char_traits<char>::compare(existing.bytes.data(), name, length) == 0) {
            found = index;
            break;
        }
    }

    if (opening) {
        if (found != snapshot.count) {
            return;
        }
        if (snapshot.count == snapshot.open.size()) {
            snapshot.unreliable = true;
            return;
        }
        auto& added = snapshot.open[snapshot.count++];
        std::char_traits<char>::copy(added.bytes.data(), name, length);
        added.bytes[length] = '\0';
        added.length = length;
        return;
    }

    if (found == snapshot.count) {
        return;
    }
    for (std::size_t index = found + 1; index < snapshot.count; ++index) {
        snapshot.open[index - 1] = snapshot.open[index];
    }
    --snapshot.count;
    snapshot.open[snapshot.count] = {};
}
} // namespace skyrim_adapter::native::detail
