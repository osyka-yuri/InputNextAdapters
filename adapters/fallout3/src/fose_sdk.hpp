#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <type_traits>

using UInt8 = std::uint8_t;
using UInt16 = std::uint16_t;
using UInt32 = std::uint32_t;

#include <fose/fose/PluginAPI.h>

namespace fallout3_adapter::native {

inline constexpr UInt32 supported_fose_version = INPUTNEXT_FALLOUT3_FOSE_VERSION;
inline constexpr UInt32 supported_runtime_version = INPUTNEXT_FALLOUT3_RUNTIME_VERSION;
inline constexpr UInt32 plugin_api_version = 1;

[[nodiscard]] constexpr UInt32 fose_version(UInt32 major, UInt32 minor, UInt32 beta) noexcept {
    return ((major & 0xFFU) << 24U) | ((minor & 0xFFU) << 16U) | ((beta & 0xFFFU) << 4U);
}

static_assert(fose_version(1, 3, 2) == supported_fose_version);
static_assert(fose_version(1, 7, 3) == supported_runtime_version);
static_assert(std::is_standard_layout_v<FOSEInterface>);
static_assert(offsetof(FOSEInterface, foseVersion) == 0);
static_assert(offsetof(FOSEInterface, runtimeVersion) == sizeof(UInt32));
static_assert(offsetof(FOSEInterface, editorVersion) == sizeof(UInt32) * 2);
static_assert(offsetof(FOSEInterface, isEditor) == sizeof(UInt32) * 3);
static_assert(std::is_standard_layout_v<PluginInfo>);
static_assert(PluginInfo::kInfoVersion == plugin_api_version);

[[nodiscard]] constexpr bool supported_interface(const FOSEInterface* fose) noexcept {
    return fose != nullptr && fose->isEditor == 0 && fose->foseVersion == supported_fose_version &&
           fose->runtimeVersion == supported_runtime_version;
}

} // namespace fallout3_adapter::native
