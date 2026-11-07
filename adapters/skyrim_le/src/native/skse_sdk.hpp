#pragma once

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma pack(push, 8)
#endif

// IWYU pragma: begin_exports
#include <common/IPrefix.h>
#include <common/ITypes.h>

#include <skse/GameAPI.h>
#include <skse/GameData.h>
#include <skse/GameEvents.h>
#include <skse/GameFormComponents.h>
#include <skse/GameForms.h>
#include <skse/GameMenus.h>
#include <skse/GameObjects.h>
#include <skse/GameRTTI.h>
#include <skse/GameReferences.h>
#include <skse/GameSettings.h>
#include <skse/GameThreads.h>
#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wpragma-pack"
#endif
#include <skse/PapyrusEvents.h>
#ifdef __clang__
#    pragma clang diagnostic pop
#endif
#include <skse/PluginAPI.h>
#include <skse/skse_version.h>
// IWYU pragma: end_exports

#ifdef _MSC_VER
#    pragma pack(pop)
#    pragma warning(pop)
#endif

static_assert(sizeof(void*) == 4, "Skyrim LE native bindings require x86 pointers.");
static_assert(sizeof(SKSEActionEvent) == 0x10);
static_assert(sizeof(TESHitEvent) == 0x30);
static_assert(sizeof(MenuOpenCloseEvent) == 0x08);
