#include "native/dispatch.hpp"
#include "native/owner.hpp"
#include "native/ui_capture.hpp"
#include "native/ui_capture_abi.hpp"

#include "fallout4_adapter/config.hpp"
#include <fallout4_adapter/audio_haptics.hpp>
#include <inputnext_adapters/audio_haptics.hpp>

#include <F4SE/API.h>
#include <F4SE/Interfaces.h>
#include <REL/Version.h>
#include <REX/LOG.h>

#include <Windows.h>
#ifdef ERROR
#    undef ERROR
#endif

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace {
[[nodiscard]] bool supported_runtime(REL::Version version) noexcept {
    return version == REL::Version{1, 11, 240, 0};
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

[[nodiscard]] std::string utf8_path(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}
} // namespace

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* interfaces) {
    try {
        if (!interfaces || interfaces->IsEditor() ||
            !supported_runtime(interfaces->RuntimeVersion()) ||
            interfaces->F4SEVersion() != REL::Version{0, 7, 9, 0}) {
            return false;
        }

        F4SE::InitInfo init{};
        init.log = true;
        init.logName = "InputNextFallout4";
        init.hook = false;
        init.trampoline = false;
        F4SE::Init(interfaces, init);

        const auto* messaging = F4SE::GetMessagingInterface();
        const auto* trampoline = F4SE::GetTrampolineInterface();
        if (!messaging || !trampoline ||
            !fallout4_adapter::native::supported_ui_capture_host(interfaces->RuntimeVersion(),
                F4SE::GetF4SEVersion(),
                messaging->Version(),
                trampoline->Version())) {
            REX::ERROR("The Fallout 4 plugin requires F4SE 0.7.9 with the pinned messaging and "
                       "trampoline interfaces; startup rejected.");
            return false;
        }

        const auto folder = executable_folder();
        if (!folder) {
            REX::ERROR("Unable to resolve the Fallout 4 executable directory; startup rejected.");
            return false;
        }

        const auto plugin_directory = *folder / L"Data" / L"F4SE" / L"Plugins";
        const auto config_path = plugin_directory / L"InputNextFallout4.ini";
        const auto runtime_path = *folder / L"inputnext_runtime.dll";
        const auto config_path_text = utf8_path(config_path);

        fallout4_adapter::Config config{};
        fallout4_adapter::ConfigError config_error{};
        const auto config_status =
            fallout4_adapter::read_config(config_path, config, &config_error);
        if (config_status == fallout4_adapter::ConfigStatus::invalid) {
            REX::ERROR("Configuration file {} line {}: {}; startup rejected.",
                config_path_text,
                config_error.line,
                config_error.message);
            return false;
        }
        if (config_status == fallout4_adapter::ConfigStatus::io_error) {
            REX::ERROR("Unable to read configuration file {}; startup rejected.", config_path_text);
            return false;
        }
        if (config_status == fallout4_adapter::ConfigStatus::missing) {
            REX::INFO("InputNextFallout4.ini is absent; using built-in defaults.");
        }

        if (config.diagnostics) {
            try {
                REX::INFO("Fallout 4 operational status logging enabled.");
            } catch (...) {
                OutputDebugStringA("InputNext Fallout 4: operational status logging enabled.\n");
            }
        }

        const auto audio_bank =
            fallout4_adapter::load_audio_haptics(config_path, config.policy.effects);
        try {
            if (audio_bank.status == inputnext::adapters::AudioHapticBankStatus::loaded) {
                REX::INFO("Loaded {} audio-derived haptic profiles.", audio_bank.loaded_profiles);
            } else if (audio_bank.status != inputnext::adapters::AudioHapticBankStatus::not_found) {
                REX::WARN("Audio haptic bank unavailable or invalid; using built-in profiles.");
            }
        } catch (...) {
            OutputDebugStringA("InputNext Fallout 4: audio haptic bank status log failed.\n");
        }

        const auto dispatch = fallout4_adapter::native::install_native_dispatch();
        if (dispatch != fallout4_adapter::native::NativeDispatchInstall::ready) {
            fallout4_adapter::native::disable_ui_capture();
            if (dispatch == fallout4_adapter::native::NativeDispatchInstall::retained_disabled) {
                REX::ERROR("Fallout 4 native dispatch patches were retained for safe forwarding; "
                           "output startup rejected.");
            } else {
                REX::ERROR("The checked Fallout 4 native dispatch hooks could not be prepared; "
                           "startup rejected.");
            }
            return false;
        }

        std::unique_ptr<fallout4_adapter::native::NativeOwner> owner{
            new (std::nothrow) fallout4_adapter::native::NativeOwner{config}};
        if (!owner) {
            fallout4_adapter::native::disable_ui_capture();
            REX::ERROR("Unable to allocate the Fallout 4 process-lifetime native owner; startup "
                       "rejected.");
            return false;
        }

        const auto started = owner->start(runtime_path);
        if (started == fallout4_adapter::native::OwnerStart::failed_before_callbacks) {
            fallout4_adapter::native::disable_ui_capture();
            return false;
        }

        // Active and callback-escaped inert owners stay bound to the pinned plugin module.
        std::ignore = owner.release();
        return true;
    } catch (...) {
        fallout4_adapter::native::disable_ui_capture();
        OutputDebugStringA("InputNext Fallout 4: unexpected plugin startup exception.\n");
        return false;
    }
}
