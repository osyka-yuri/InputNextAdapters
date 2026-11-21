#include "model.hpp"
#include "native/diagnostics.hpp"
#include "native/interfaces.hpp"
#include "native/xobse_sdk.hpp"
#include "observation.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr const char* plugin_name = "InputNextOblivion";
constexpr std::uint32_t plugin_version = 1;

[[nodiscard]] std::optional<std::filesystem::path> executable_folder() noexcept {
    try {
        std::vector<wchar_t> buffer(512);
        while (buffer.size() <= 32768) {
            SetLastError(ERROR_SUCCESS);
            const DWORD length =
                GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) {
                return std::nullopt;
            }
            if (static_cast<std::size_t>(length) < buffer.size()) {
                return std::filesystem::path{
                    std::wstring{buffer.data(), static_cast<std::size_t>(length)}}
                    .parent_path();
            }
            if (buffer.size() == 32768) {
                return std::nullopt;
            }
            buffer.resize(std::min<std::size_t>(buffer.size() * 2, 32768));
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

void setup_logging(const std::filesystem::path& executable_dir) noexcept {
    try {
        const auto plugin_dir = executable_dir / "Data" / "OBSE" / "Plugins";
        std::error_code ec;
        std::filesystem::create_directories(plugin_dir, ec);
        if (ec) {
            OutputDebugStringA("InputNextOblivion: failed to create logging directory.\n");
            return;
        }

        const auto log_path = plugin_dir / "InputNextOblivion.log";
        oblivion_adapter::native::diagnostics::initialize(log_path);
    } catch (...) {
        OutputDebugStringA("InputNextOblivion: logging setup failed.\n");
    }
}

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage) - Standard Windows DLL entry point.
BOOL WINAPI DllMain(HINSTANCE /*hinstDLL*/, DWORD /*fdwReason*/, LPVOID /*lpvReserved*/) {
    return TRUE;
}

extern "C" bool __cdecl OBSEPlugin_Query(const OBSEInterface* obse, PluginInfo* info) {
    if (info) {
        info->infoVersion = PluginInfo::kInfoVersion;
        info->name = plugin_name;
        info->version = plugin_version;
    }

    if (!obse) {
        return false;
    }

    if (obse->isEditor != 0 || obse->oblivionVersion != OBLIVION_VERSION_1_2_416 ||
        obse->obseVersion < 22 || !obse->QueryInterface || !obse->GetPluginHandle) {
        return false;
    }

    return true;
}

extern "C" bool __cdecl OBSEPlugin_Load(const OBSEInterface* obse) {
    const auto executable_dir = executable_folder();
    if (!executable_dir) {
        OutputDebugStringA("InputNextOblivion: executable folder not found.\n");
        return false;
    }
    setup_logging(*executable_dir);
    oblivion_adapter::native::diagnostics::write("InputNextOblivion: OBSEPlugin_Load invoked");

    if (!oblivion_adapter::native::PluginInterfaceManager::instance().initialize(obse,
            *executable_dir)) {
        oblivion_adapter::native::diagnostics::write("InputNextOblivion: OBSEPlugin_Load failed");
        oblivion_adapter::native::diagnostics::shutdown();
        return false;
    }

    oblivion_adapter::native::diagnostics::write(
        "InputNextOblivion: OBSEPlugin_Load completed successfully");
    return true;
}

#ifdef INPUTNEXT_OBLIVION_ENABLE_TEST_EXPORTS
extern "C" __declspec(dllexport) bool __cdecl InputNext_GetObservation(
    oblivion_adapter::Observation* out) {
    if (!out) {
        return false;
    }
    *out = oblivion_adapter::ObservationManager::instance().get_observation();
    return true;
}

extern "C" __declspec(dllexport) std::uint64_t __cdecl InputNext_GetSessionEpoch() {
    return oblivion_adapter::ObservationManager::instance().current_epoch();
}

extern "C" __declspec(dllexport) std::uint8_t __cdecl InputNext_GetSessionState() {
    return static_cast<std::uint8_t>(
        oblivion_adapter::native::PluginInterfaceManager::instance().session_state());
}
#endif
