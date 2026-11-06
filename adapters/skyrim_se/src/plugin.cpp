#include "native/owner.hpp"
#include <inputnext_adapters/audio_haptics.hpp>
#include <skyrim_adapter/audio_haptics.hpp>
#include <skyrim_adapter/native/config.hpp>

#include <REL/Version.h>
#include <SKSE/API.h>
#include <SKSE/Interfaces.h>
#include <SKSE/Logger.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <new>
#include <optional>
#include <string>
#include <vector>

namespace {
[[nodiscard]] bool supported_runtime(REL::Version version) noexcept {
    constexpr std::array supported{
        // Steam (x.x.x.0)
        REL::Version{1, 5, 97, 0},
        REL::Version{1, 6, 1170, 0},
        REL::Version{1, 6, 1179, 0},
        REL::Version{1, 7, 104, 0},
        // GOG (x.x.x.1)
        REL::Version{1, 6, 659, 1},
        REL::Version{1, 6, 1170, 1},
        REL::Version{1, 6, 1179, 1},
        // Epic Games Store (x.x.x.2)
        REL::Version{1, 6, 678, 2},
    };
    return std::ranges::find(supported, version) != supported.end();
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

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* interfaces) {
    if (!interfaces || interfaces->IsEditor() || !supported_runtime(interfaces->RuntimeVersion())) {
        return false;
    }

    try {
        SKSE::InitInfo init{};
        init.log = true;
        init.logName = "InputNextSkyrim";
        SKSE::Init(interfaces, init);

        const auto folder = executable_folder();
        if (!folder) {
            SKSE::log::error(
                "Unable to resolve the Skyrim executable directory; startup rejected.");
            return false;
        }

        const auto plugin_directory = *folder / L"Data" / L"SKSE" / L"Plugins";
        const auto config_path = plugin_directory / L"InputNextSkyrim.ini";
        const auto runtime_path = *folder / L"inputnext_runtime.dll";
        const auto config_path_text = utf8_path(config_path);

        skyrim_adapter::native::NativeConfig config{};
        skyrim_adapter::native::ConfigError config_error{};
        const auto config_status =
            skyrim_adapter::native::read_config_file(config_path, config, &config_error);
        if (config_status == skyrim_adapter::native::ConfigStatus::invalid) {
            SKSE::log::error("Configuration file {} line {} [{}] {}: {}; startup rejected.",
                config_path_text,
                config_error.line,
                config_error.section,
                config_error.key,
                config_error.message);
            return false;
        }
        if (config_status == skyrim_adapter::native::ConfigStatus::io_error) {
            SKSE::log::error("Unable to read configuration file {}; startup rejected.",
                config_path_text);
            return false;
        }
        if (config_status == skyrim_adapter::native::ConfigStatus::missing) {
            try {
                SKSE::log::info("InputNextSkyrim.ini is absent; using neutral defaults.");
            } catch (...) {
                OutputDebugStringA("InputNext Skyrim: configuration is absent; informational log "
                                   "write failed. Using neutral defaults.\n");
            }
        }

        const auto audio_bank = skyrim_adapter::load_audio_haptics(config_path,
            inputnext::adapters::AudioHapticGame::skyrim_se,
            config.effects);
        try {
            if (audio_bank.status == inputnext::adapters::AudioHapticBankStatus::loaded) {
                SKSE::log::info("Loaded {} audio-derived haptic profiles.",
                    audio_bank.loaded_profiles);
            } else if (audio_bank.status != inputnext::adapters::AudioHapticBankStatus::not_found) {
                SKSE::log::warn(
                    "Audio haptic bank unavailable or invalid; using built-in profiles.");
            }
        } catch (...) {
            OutputDebugStringA("InputNext Skyrim: audio haptic bank status log failed.\n");
        }

        auto* owner = new (std::nothrow) skyrim_adapter::native::NativeOwner{config};
        if (!owner) {
            SKSE::log::error("Unable to allocate the process-lifetime native owner.");
            return false;
        }

        const auto started = owner->start(runtime_path);
        if (started == skyrim_adapter::native::OwnerStart::failed_before_callbacks) {
            delete owner;
            return false;
        }

        // Successful and callback-escaped inert owners intentionally remain process-bound.
        return true;
    } catch (...) {
        OutputDebugStringA("InputNext Skyrim: unexpected exception during plugin startup.\n");
        return false;
    }
}
