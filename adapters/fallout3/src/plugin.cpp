#include "fose_sdk.hpp"

#include <Windows.h>

#include <fose/fose/PluginAPI.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view plugin_name{"InputNextFallout3"};
constexpr UInt32 plugin_version = 1;
std::atomic<bool> startup_reported{};
HMODULE plugin_module{};

void report_missing_startup_receipt(const char* reason) noexcept {
    std::array<char, 256> message{};
    static_cast<void>(std::snprintf(message.data(),
        message.size(),
        "InputNextFallout3 loaded. Optional startup receipt could not be written (%s).\r\n",
        reason));
    OutputDebugStringA(message.data());
}

[[nodiscard]] std::optional<std::filesystem::path> plugin_directory() noexcept {
    try {
        if (plugin_module == nullptr) {
            return std::nullopt;
        }

        std::vector<wchar_t> buffer(512);
        while (buffer.size() <= 32768) {
            SetLastError(ERROR_SUCCESS);
            const DWORD length =
                GetModuleFileNameW(plugin_module, buffer.data(), static_cast<DWORD>(buffer.size()));
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

void write_optional_startup_receipt() noexcept {
    bool expected = false;
    if (!startup_reported.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    const auto directory = plugin_directory();
    if (!directory) {
        report_missing_startup_receipt("the plugin directory could not be resolved");
        return;
    }

    try {
        const auto path = *directory / L"InputNextFallout3.log";
        HANDLE file = CreateFileW(path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            report_missing_startup_receipt("the log file could not be opened");
            return;
        }

        constexpr std::string_view message{
            "InputNextFallout3 0.1.0 loaded through FOSE 1.3 beta 2 on Fallout 3 1.7.0.3\r\n"};
        DWORD written = 0;
        const bool wrote_message = WriteFile(file,
                                       message.data(),
                                       static_cast<DWORD>(message.size()),
                                       &written,
                                       nullptr) != FALSE &&
                                   written == static_cast<DWORD>(message.size());
        const bool closed_file = CloseHandle(file) != FALSE;
        if (!wrote_message || !closed_file) {
            report_missing_startup_receipt("the startup entry could not be written completely");
        }
    } catch (...) {
        report_missing_startup_receipt("an error occurred while creating the log file");
    }
}

} // namespace

// NOLINTNEXTLINE(misc-use-internal-linkage) - The Windows DLL loader requires this entry point.
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, [[maybe_unused]] LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        plugin_module = instance;
    }
    return TRUE;
}

extern "C" bool __cdecl FOSEPlugin_Query(const FOSEInterface* fose, PluginInfo* info) {
    if (info != nullptr) {
        info->infoVersion = PluginInfo::kInfoVersion;
        info->name = plugin_name.data();
        info->version = plugin_version;
    }

    return info != nullptr && fallout3_adapter::native::supported_interface(fose);
}

extern "C" bool __cdecl FOSEPlugin_Load(const FOSEInterface* fose) {
    if (!fallout3_adapter::native::supported_interface(fose)) {
        return false;
    }

    write_optional_startup_receipt();
    return true;
}
