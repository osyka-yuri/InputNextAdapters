#pragma once

#include <cstddef>
#include <optional>

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4200 4244 4267 4311 4312 4324 4458 4702 4996)
#    pragma pack(push, 8)
#endif

// IWYU pragma: begin_exports
#include <common/IPrefix.h>
#include <common/ITypes.h>
#include <obse_common/obse_version.h>

#include <obse/CommandTable.h>
#include <obse/EventManager.h>
#include <obse/GameAPI.h>
#include <obse/GameActorValues.h>
#include <obse/GameForms.h>
#include <obse/GameMenus.h>
#include <obse/GameObjects.h>
#include <obse/GameProcess.h>
#include <obse/GameRTTI.h>
#include <obse/GameTypes.h>
#include <obse/NiHavokCharacters.h>
#include <obse/NiNodes.h>
#include <obse/PluginAPI.h>
#include <obse/Tasks.h>
// IWYU pragma: end_exports

#ifdef _MSC_VER
#    pragma pack(pop)
#    pragma warning(pop)
#endif

static_assert(sizeof(void*) == 4, "InputNextOblivion requires 32-bit x86 architecture");
static_assert(sizeof(PluginInfo) == 0x0C, "PluginInfo must be 12 bytes on x86");
static_assert(sizeof(PlayerCharacter) == 0x800,
    "PlayerCharacter must be 0x800 bytes on Oblivion 1.2.0.416");
static_assert(OBLIVION_VERSION_1_2_416 == 0x010201A0,
    "OBLIVION_VERSION_1_2_416 must be 0x010201A0");

// clang-cl supports offsetof for these MSVC ABI types as an extension.
#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
static_assert(offsetof(TESObjectREFR, parentCell) == 0x40,
    "TESObjectREFR::parentCell offset changed from xOBSE 22.15");
static_assert(offsetof(TESObjectCELL, objectList) == 0x48,
    "TESObjectCELL::objectList offset changed from xOBSE 22.15");
static_assert(offsetof(ArrowProjectile, elapsedTime) == 0x68,
    "ArrowProjectile::elapsedTime offset changed from xOBSE 22.15");
static_assert(offsetof(ArrowProjectile, shooter) == 0x78,
    "ArrowProjectile::shooter offset changed from xOBSE 22.15");
static_assert(offsetof(TESObjectWEAP, type) == 0x90,
    "TESObjectWEAP::type offset changed from xOBSE 22.15");
static_assert(offsetof(PlayerCharacter, firstPersonAnimData) == 0x5CC,
    "PlayerCharacter first-person animation data offset changed from xOBSE 22.15");
static_assert(offsetof(PlayerCharacter, miscStats) == 0x658,
    "PlayerCharacter miscStats offset changed from xOBSE 22.15");
static_assert(offsetof(MiddleHighProcess, charProxy) == 0x118,
    "MiddleHighProcess::charProxy offset changed from xOBSE 22.15");
static_assert(offsetof(HighProcess, currentAction) == 0x1F4,
    "HighProcess::currentAction offset changed from xOBSE 22.15");
static_assert(offsetof(HighProcess, movementFlags) == 0x1FC,
    "HighProcess::movementFlags offset changed from xOBSE 22.15");
#ifdef __clang__
#    pragma clang diagnostic pop
#endif

namespace oblivion_adapter::native {

[[nodiscard]] std::optional<bool> read_sneak_eye_detection() noexcept;

} // namespace oblivion_adapter::native
