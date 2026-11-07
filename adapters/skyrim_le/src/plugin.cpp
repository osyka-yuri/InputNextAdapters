#include "native/diagnostics.hpp"
#include "native/owner.hpp"
#include "native/skse_sdk.hpp"

#include <inputnext_adapters/audio_haptics.hpp>
#include <skyrim_adapter/audio_haptics.hpp>
#include <skyrim_adapter/native/config.hpp>

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace {
constexpr const char* plugin_name = "InputNextSkyrimLE";
constexpr std::uint32_t plugin_version = 1;

[[nodiscard]] bool compatible(const SKSEInterface* skse) noexcept {
    return skse != nullptr && skse->isEditor == 0 &&
           skse->runtimeVersion == RUNTIME_VERSION_1_9_32_0 &&
           skse->skseVersion >= PACKED_SKSE_VERSION;
}

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

void log_configuration_error(const std::filesystem::path& path,
    const skyrim_adapter::native::ConfigError& error) noexcept {
    try {
        // ConfigError messages are static ASCII literals from the shared parser.
        const std::wstring reason{error.message.begin(), error.message.end()};
        const std::wstring message = L"InputNext Skyrim LE: invalid configuration at line " +
                                     std::to_wstring(error.line) + L" in " + path.native() + L": " +
                                     reason + L"\n";
        OutputDebugStringW(message.c_str());
    } catch (...) {
        OutputDebugStringW(L"InputNext Skyrim LE: invalid configuration file.\n");
    }
}
} // namespace

extern "C" bool __cdecl SKSEPlugin_Query(const SKSEInterface* skse, PluginInfo* info) {
    static_assert(sizeof(void*) == 4);
    if (info) {
        info->infoVersion = PluginInfo::kInfoVersion;
        info->name = plugin_name;
        info->version = plugin_version;
    }
    return compatible(skse);
}

extern "C" bool __cdecl SKSEPlugin_Load(const SKSEInterface* skse) {
    try {
        if (!compatible(skse) || !skse->QueryInterface || !skse->GetPluginHandle) {
            OutputDebugStringW(
                L"InputNext Skyrim LE: incompatible SKSE interface; startup rejected.\n");
            return false;
        }

        const auto* task_interface =
            static_cast<const SKSETaskInterface*>(skse->QueryInterface(kInterface_Task));
        const auto* messaging_interface =
            static_cast<const SKSEMessagingInterface*>(skse->QueryInterface(kInterface_Messaging));
        const PluginHandle plugin_handle = skse->GetPluginHandle();
        if (!task_interface ||
            task_interface->interfaceVersion < SKSETaskInterface::kInterfaceVersion ||
            !task_interface->AddTask || !messaging_interface ||
            messaging_interface->interfaceVersion < SKSEMessagingInterface::kInterfaceVersion ||
            !messaging_interface->RegisterListener || !messaging_interface->GetEventDispatcher ||
            plugin_handle == static_cast<PluginHandle>(kPluginHandle_Invalid)) {
            OutputDebugStringW(
                L"InputNext Skyrim LE: required SKSE task or messaging API is unavailable.\n");
            return false;
        }

        const auto folder = executable_folder();
        if (!folder) {
            OutputDebugStringW(
                L"InputNext Skyrim LE: unable to resolve the Skyrim executable directory.\n");
            return false;
        }

        const auto plugin_folder = *folder / L"Data" / L"SKSE" / L"Plugins";
        skyrim_adapter::native::diagnostics::initialize(plugin_folder / L"InputNextSkyrimLE.log");
        skyrim_adapter::native::diagnostics::write("plugin_load=started");

        const auto config_path = plugin_folder / L"InputNextSkyrimLE.ini";
        const auto runtime_path = *folder / L"inputnext_runtime.dll";
        if (!runtime_path.is_absolute()) {
            OutputDebugStringW(L"InputNext Skyrim LE: runtime path is not absolute.\n");
            return false;
        }

        skyrim_adapter::native::NativeConfig config{};
        skyrim_adapter::native::ConfigError config_error{};
        const auto config_status =
            skyrim_adapter::native::read_config_file(config_path, config, &config_error);
        if (config_status == skyrim_adapter::native::ConfigStatus::invalid) {
            skyrim_adapter::native::diagnostics::write("configuration=invalid line=",
                static_cast<unsigned>(config_error.line));
            log_configuration_error(config_path, config_error);
            return false;
        }
        if (config_status == skyrim_adapter::native::ConfigStatus::io_error) {
            skyrim_adapter::native::diagnostics::write("configuration=read_error");
            OutputDebugStringW(L"InputNext Skyrim LE: unable to read configuration file.\n");
            return false;
        }
        if (config_status == skyrim_adapter::native::ConfigStatus::missing) {
            skyrim_adapter::native::diagnostics::write("configuration=missing defaults=neutral");
            OutputDebugStringW(
                L"InputNext Skyrim LE: configuration missing; using neutral defaults.\n");
        } else {
            skyrim_adapter::native::diagnostics::write("configuration=loaded");
        }

        const auto audio_bank = skyrim_adapter::load_audio_haptics(config_path,
            inputnext::adapters::AudioHapticGame::skyrim_le,
            config.effects);
        skyrim_adapter::native::diagnostics::write("audio_haptics_status=",
            static_cast<unsigned>(audio_bank.status),
            " profiles=",
            audio_bank.loaded_profiles);

        std::unique_ptr<skyrim_adapter::native::NativeOwner> owner{
            new (std::nothrow) skyrim_adapter::native::NativeOwner{config}};
        if (!owner) {
            skyrim_adapter::native::diagnostics::write("native_owner=allocation_failed");
            OutputDebugStringW(L"InputNext Skyrim LE: unable to allocate native owner.\n");
            return false;
        }

        const auto started =
            owner->start(runtime_path, plugin_handle, *task_interface, *messaging_interface);
        if (started == skyrim_adapter::native::OwnerStart::failed_before_callbacks) {
            skyrim_adapter::native::diagnostics::write("plugin_load=failed before_callbacks=1");
            return false;
        }

        // Callback-visible owners remain process-bound.
        std::ignore = owner.release();
        skyrim_adapter::native::diagnostics::write("plugin_load=accepted owner_state=",
            started == skyrim_adapter::native::OwnerStart::active ? "active" : "inert");

        return true;
    } catch (...) {
        skyrim_adapter::native::diagnostics::write("plugin_load=exception");
        OutputDebugStringW(L"InputNext Skyrim LE: unexpected startup exception.\n");
        return false;
    }
}
