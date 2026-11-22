#include "native_state.hpp"
#include "nvse_sdk.hpp"

namespace {
constexpr const char* plugin_name = "InputNextFalloutNewVegas";
constexpr unsigned int plugin_version = 1;

bool compatible(const NVSEInterface* nvse) noexcept {
    return nvse != nullptr && nvse->nvseVersion >= PACKED_NVSE_VERSION && nvse->isEditor == 0 &&
           nvse->runtimeVersion == RUNTIME_VERSION_1_4_0_525 && nvse->isNogore == 0 &&
           nvse->QueryInterface != nullptr && nvse->GetPluginHandle != nullptr &&
           nvse->GetRuntimeDirectory != nullptr;
}
} // namespace

extern "C" bool __cdecl NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info) {
    if (info == nullptr) {
        return false;
    }
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name = plugin_name;
    info->version = plugin_version;
    return compatible(nvse);
}

extern "C" bool __cdecl NVSEPlugin_Load(const NVSEInterface* nvse) {
    return compatible(nvse) && fallout_new_vegas::NativeState::instance().load(*nvse);
}
