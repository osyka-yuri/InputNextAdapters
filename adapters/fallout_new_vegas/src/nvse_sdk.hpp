#pragma once

#include <algorithm>

#pragma warning(push)
#include <nvse/prefix.h>

using std::max;
using byte = unsigned char;

// xNVSE 6.4.9 uses the Win32 max macro with these mixed integer types.
[[nodiscard]] inline SInt32 max(SInt32 left, UInt32 right) noexcept {
    return static_cast<SInt32>(std::max(static_cast<UInt32>(left), right));
}

// IWYU pragma: begin_exports
#include <common/ITypes.h>
#include <nvse/GameAPI.h>
#include <nvse/GameForms.h>
#include <nvse/GameObjects.h>
#include <nvse/GameProcess.h>
#include <nvse/GameUI.h>
#include <nvse/PluginAPI.h>
#include <nvse/nvse_version.h>
// IWYU pragma: end_exports

#include <Windows.h>
#pragma warning(pop)
