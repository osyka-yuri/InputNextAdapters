#include "memory_guard.hpp"
#include "xobse_sdk.hpp"

#include <cstdint>
#include <optional>

// Oblivion 1.2.0.416 addresses from xOBSE 22.15. Define the required SDK symbols
// locally to avoid linking xOBSE object files.
// DynamicCast and RTTI come from GameAPI.cpp and GameRTTI_1_2_416.inl.
const _Oblivion_DynamicCast Oblivion_DynamicCast =
    reinterpret_cast<_Oblivion_DynamicCast>(0x009832E6);
const void* RTTI_ArrowProjectile = reinterpret_cast<const void*>(0x00B05AD0);
const void* RTTI_BaseProcess = reinterpret_cast<const void*>(0x00B05908);
const void* RTTI_MiddleHighProcess = reinterpret_cast<const void*>(0x00B09DFC);
const void* RTTI_NiObject = reinterpret_cast<const void*>(0x00B02E64);
const void* RTTI_TESForm = reinterpret_cast<const void*>(0x00B02F9C);
const void* RTTI_TESObjectREFR = reinterpret_cast<const void*>(0x00B03054);
const void* RTTI_bhkCharacterController = reinterpret_cast<const void*>(0x00B14A0C);

// The player singleton and actor value calls come from GameObjects.cpp.
PlayerCharacter** g_thePlayer = reinterpret_cast<PlayerCharacter**>(0x00B333C4);

float Actor::GetCalculatedBaseAV(UInt32 avCode) {
    // NOLINTNEXTLINE(misc-const-correctness) - Written by inline assembly.
    float result = 0.0F;
    __asm {
        mov ecx, this
        push avCode
        mov eax, 0x005EAD00
        call eax
        fstp dword ptr [result]
    }
    return result;
}

float Actor::GetAVModifier(eAVModifier modifier, UInt32 avCode) {
    if (modifier == kAVModifier_Invalid) {
        return 0.0F;
    }
    // NOLINTNEXTLINE(misc-const-correctness) - Written by inline assembly.
    float result = 0.0F;
    const auto mod_val = static_cast<std::uint32_t>(modifier);
    __asm {
        mov ecx, this
        push avCode
        push mod_val
        mov eax, 0x0065D270
        call eax
        fstp dword ptr [result]
    }
    return result;
}

// Menu functions and flags come from GameAPI.cpp.
using GetSingletonFunc = void* (*)(bool, bool);
static const auto s_InterfaceManager_GetSingleton = reinterpret_cast<GetSingletonFunc>(0x00582160);

InterfaceManager* InterfaceManager::GetSingleton() {
    return static_cast<InterfaceManager*>(s_InterfaceManager_GetSingleton(false, true));
}

using IsMenuModeFunc = bool (*)();
static const auto s_IsMenuMode = reinterpret_cast<IsMenuModeFunc>(0x00578F60);

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) - Matches the SDK declaration.
bool InterfaceManager::IsGameMode() {
    return !s_IsMenuMode();
}

// GameAPI.cpp exposes a separate flag for the console.
bool IsConsoleOpen() {
    // This is the pinned Oblivion 1.2.0.416 console flag used by xOBSE 22.15.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.FixedAddressDereference)
    return *reinterpret_cast<const std::uint8_t*>(0x00B33415) != 0U;
}

UInt32 InterfaceManager::GetTopVisibleMenuID() {
    using GetTopVisibleMenuIDFunc = UInt32(__thiscall*)(InterfaceManager*);
    const auto get_top_visible_menu_id = reinterpret_cast<GetTopVisibleMenuIDFunc>(0x0057CF60);
    return get_top_visible_menu_id(this);
}

namespace oblivion_adapter::native {

std::optional<bool> read_sneak_eye_detection() noexcept {
    // Oblivion 1.2.0.416 HUDReticle: 005A8000 binds the eye tile, 005A80D0
    // publishes its detection state, and 005A82D0 manages its visibility.
    constexpr std::uintptr_t kEyeTile = 0x00B3B354;
    constexpr std::uintptr_t kEyeVisible = 0x00B3B361;
    constexpr std::uintptr_t kEyeDetectionState = 0x00B3B368;
    return guarded_game_read([] -> std::optional<bool> {
        const auto* const eye = reinterpret_cast<Tile* const*>(kEyeTile);
        const auto* const visible = reinterpret_cast<const std::uint8_t*>(kEyeVisible);
        const auto* const detection = reinterpret_cast<const std::uint32_t*>(kEyeDetectionState);
        // NOLINTNEXTLINE(clang-analyzer-optin.core.FixedAddressDereference)
        if (!*eye || *visible != 1U || *detection >= HighProcess::kDetectionState_Max) {
            return std::nullopt;
        }
        // The stock eye brightens only at Seen.
        return *detection == HighProcess::kDetectionState_Seen;
    });
}

} // namespace oblivion_adapter::native
