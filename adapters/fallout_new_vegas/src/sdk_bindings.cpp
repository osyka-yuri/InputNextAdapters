#include "nvse_sdk.hpp"

#include "stealth_detection.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace {
// xNVSE 6.4.9 GameObjects.cpp and GameUI.cpp for FalloutNV.exe 1.4.0.525.
constexpr std::uintptr_t player_slot = 0x011DEA3C;
constexpr std::uintptr_t interface_slot = 0x011D8A80;
constexpr std::uintptr_t menu_visibility_table = 0x011F308F;
} // namespace

// NOLINTNEXTLINE(performance-no-int-to-ptr) - Runtime addresses are fixed for this executable.
PlayerCharacter** g_thePlayer = reinterpret_cast<PlayerCharacter**>(player_slot);

TESObjectWEAP* Actor::GetEquippedWeapon() const {
    const auto* entry = baseProcess != nullptr ? baseProcess->GetWeaponInfo() : nullptr;
    auto* const form = entry != nullptr ? entry->type : nullptr;
    return form != nullptr && form->IsWeapon() ? static_cast<TESObjectWEAP*>(form) : nullptr;
}

InterfaceManager* InterfaceManager::GetSingleton() {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.FixedAddressDereference, performance-no-int-to-ptr)
    return *reinterpret_cast<InterfaceManager* const*>(interface_slot);
}

bool InterfaceManager::IsMenuVisible(UInt32 menuType) {
    return menuType >= kMenuType_Min && menuType <= kMenuType_Max &&
           // NOLINTNEXTLINE(clang-analyzer-optin.core.FixedAddressDereference, performance-no-int-to-ptr)
           reinterpret_cast<const UInt8*>(menu_visibility_table)[menuType] != 0;
}

namespace fallout_new_vegas {
namespace {
// FalloutNV 1.4.0.525 detection inputs exposed by JIP LN's GetPCDetectionState.
constexpr std::uintptr_t image_base = 0x00400000;
constexpr std::uintptr_t process_manager = 0x011E0E80;
constexpr std::uintptr_t total_detection_value = 0x00973710;
constexpr std::size_t in_combat_offset = 0xDF0;
constexpr std::size_t unseen_offset = 0xDF1;
static_assert(unseen_offset < sizeof(PlayerCharacter));

using TotalDetectionValue = std::int32_t(__thiscall*)(ActorProcessManager*, Actor*, bool);

std::optional<DetectionState> read_player_detection() noexcept {
    auto* const player = *g_thePlayer;
    if (!player || !player->parentCell || !player->baseProcess ||
        player->baseProcess->processLevel != BaseProcess::kProcessLevel_High) {
        return std::nullopt;
    }

    const auto fields = std::as_bytes(std::span{player, 1});
    const bool unseen = fields[unseen_offset] != std::byte{};
    const bool in_combat = fields[in_combat_offset] != std::byte{};
    const auto total = std::bit_cast<TotalDetectionValue>(total_detection_value);
    const auto value =
        unseen || in_combat
            ? 0
            : total(std::bit_cast<ActorProcessManager*>(process_manager), player, false);
    return classify_detection(unseen, in_combat, value);
}
} // namespace

StealthDetection::Reader native_stealth_reader() noexcept {
    const auto* module = GetModuleHandleW(nullptr);
    if (std::bit_cast<std::uintptr_t>(module) != image_base) {
        return nullptr;
    }
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(std::bit_cast<const void*>(total_detection_value), &region, sizeof(region)) !=
            sizeof(region) ||
        region.AllocationBase != module || region.State != MEM_COMMIT || region.Type != MEM_IMAGE ||
        (region.Protect != PAGE_EXECUTE_READ && region.Protect != PAGE_EXECUTE_READWRITE &&
            region.Protect != PAGE_EXECUTE_WRITECOPY)) {
        return nullptr;
    }
    return &read_player_detection;
}
} // namespace fallout_new_vegas
