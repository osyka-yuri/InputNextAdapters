#include "nvse_sdk.hpp"
#include "support/test_runner.hpp"

#include <Windows.h>

#include <nvse/PluginAPI.h>
#include <nvse/nvse_version.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {
using Query = bool(__cdecl*)(const NVSEInterface*, PluginInfo*);
using Load = bool(__cdecl*)(const NVSEInterface*);
using NativeHandler = NVSEEventManagerInterface::NativeEventHandler;

constexpr PluginHandle plugin_handle = 4;
constexpr std::array<std::string_view, 8> expected_events{
    "ShowOff:OnFireWeapon",
    "ShowOff:OnProjectileCreate",
    "ShowOff:OnPCMiscStatChange",
    "onhit",
    "onactorequip",
    "onlockpickbroken",
    "onterminalhacked",
    "onterminalhackfailed",
};

struct Host {
    std::string directory;
    NVSEMessagingInterface::EventCallback listener{};
    std::array<NativeHandler, expected_events.size()> handlers{};
    std::size_t registrations{};
    std::size_t subscriptions{};
    std::size_t removals{};
    bool event_manager_available{true};
    bool reject_projectile{};
    bool reject_terminal_failure{};
};

Host host;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

bool register_listener(PluginHandle handle,
    const char* sender,
    NVSEMessagingInterface::EventCallback listener) noexcept {
    if (handle != plugin_handle || sender == nullptr || std::string_view{sender} != "NVSE" ||
        listener == nullptr) {
        return false;
    }
    host.listener = listener;
    ++host.registrations;
    return true;
}

NativeHandler* handler_slot(const char* name) noexcept {
    if (name == nullptr) {
        return nullptr;
    }
    const auto event = std::ranges::find(expected_events, std::string_view{name});
    if (event == expected_events.end()) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(event - expected_events.begin());
    return &host.handlers[index];
}

bool register_event(const char* name, NativeHandler handler) noexcept {
    ++host.subscriptions;
    auto* const slot = handler_slot(name);
    if (slot == nullptr || handler == nullptr || *slot != nullptr ||
        (host.reject_projectile && slot == &host.handlers[1]) ||
        (host.reject_terminal_failure && std::string_view{name} == "onterminalhackfailed")) {
        return false;
    }
    *slot = handler;
    return true;
}

bool remove_event(const char* name, NativeHandler handler) noexcept {
    auto* const slot = handler_slot(name);
    if (slot == nullptr || handler == nullptr || *slot != handler) {
        return false;
    }
    *slot = nullptr;
    ++host.removals;
    return true;
}

void* query_interface(UInt32 interface_id) noexcept {
    static NVSEMessagingInterface messaging{};
    messaging.version = NVSEMessagingInterface::kVersion;
    messaging.RegisterListener = register_listener;
    static NVSEEventManagerInterface events{};
    events.SetNativeEventHandler = register_event;
    events.RemoveNativeEventHandler = remove_event;
    if (interface_id == kInterface_Messaging) {
        return &messaging;
    }
    if (interface_id == kInterface_EventManager) {
        return host.event_manager_available ? &events : nullptr;
    }
    return nullptr;
}

void send_message(UInt32 type, void* data = nullptr) {
    NVSEMessagingInterface::Message message{.sender = "NVSE",
        .type = type,
        .dataLen = type == NVSEMessagingInterface::kMessage_PostLoadGame ? 1U : 0U,
        .data = data};
    host.listener(&message);
}

void flush_shot_events() noexcept {
    host.handlers[0] = nullptr;
    host.handlers[1] = nullptr;
}

bool handlers_installed() noexcept {
    return std::ranges::all_of(host.handlers, [](auto handler) { return handler != nullptr; });
}

std::uintmax_t log_size(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0U : size;
}

std::string log_since(const std::filesystem::path& path, std::uintmax_t offset) {
    std::ifstream input(path, std::ios::binary);
    input.seekg(static_cast<std::streamoff>(offset));
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_fixture(const std::filesystem::path& path, std::string_view data) {
    std::ofstream output(path, std::ios::binary);
    output << data;
    output.close();
    require(static_cast<bool>(output), "Could not write a plugin load fixture");
}

struct SettingsFixture {
    bool valid{true};
    bool audio_enabled{true};
    std::string_view diagnostic;
};

SettingsFixture prepare_settings(const std::filesystem::path& plugins,
    std::string_view mode,
    const std::filesystem::path& other_game_bank) {
    const auto ini = plugins / "InputNextFalloutNewVegas.ini";
    const auto bank = plugins / "InputNextFalloutNewVegas.audiohaptics";
    if (mode == "invalid-ini") {
        write_fixture(ini, "[General]\nEnabled=true\n[Effects]\nBodyScale=nan\n");
        return {.valid = false,
            .diagnostic =
                "InputNextFalloutNewVegas.ini:4 setting value is outside its supported range"};
    }
    if (mode == "unreadable-ini") {
        std::filesystem::create_directory(ini);
        return {.valid = false,
            .diagnostic = "InputNextFalloutNewVegas.ini could not open configuration file"};
    }
    if (mode == "invalid-audio-bank") {
        write_fixture(bank, "invalid bank bytes\n");
        return {.diagnostic = "InputNextFalloutNewVegas.audiohaptics failed bank validation"};
    }
    if (mode == "disabled-audio-bank") {
        write_fixture(bank, "invalid bank bytes\n");
        write_fixture(ini, "[Effects]\nAudioHapticsEnabled=false\n");
        return {.audio_enabled = false, .diagnostic = {}};
    }
    if (mode == "unreadable-audio-bank") {
        std::filesystem::create_directory(bank);
        return {.diagnostic = "could not read InputNextFalloutNewVegas.audiohaptics"};
    }
    if (mode == "wrong-game-audio-bank") {
        std::filesystem::copy_file(other_game_bank,
            bank,
            std::filesystem::copy_options::overwrite_existing);
        return {.diagnostic = "InputNextFalloutNewVegas.audiohaptics targets a different game"};
    }
    return {};
}

void verify_settings_diagnostics(const SettingsFixture& settings, std::string_view log) {
    require(settings.diagnostic.empty() || log.contains(settings.diagnostic),
        "The startup log must identify the settings failure");
    require(log.contains("25 audio haptic profiles available") ==
                (settings.valid && settings.audio_enabled),
        "Built-in audio profiles must survive an invalid override and respect AudioHapticsEnabled");
    require(settings.audio_enabled || !log.contains(".audiohaptics"),
        "Disabled audio haptics must skip the external bank");
}

void verify_shot_rebinding() {
    constexpr std::size_t shot_events = 2U;
    const auto handlers = host.handlers;
    auto subscriptions = host.subscriptions;
    const auto check_rebind = [&handlers, &subscriptions](std::size_t removals,
                                  const char* message) {
        subscriptions += shot_events;
        require(host.handlers == handlers && host.subscriptions == subscriptions &&
                    host.removals == removals,
            message);
    };

    flush_shot_events();
    send_message(NVSEMessagingInterface::kMessage_NewGame);
    check_rebind(0U, "A new game must restore the flushed shot handlers");
    send_message(NVSEMessagingInterface::kMessage_PostLoadGame);
    check_rebind(shot_events, "A failed load must retain the shot handlers");

    send_message(NVSEMessagingInterface::kMessage_PreLoadGame);
    flush_shot_events();
    // xNVSE sends the successful-load boolean in the value of the data pointer.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* const success = reinterpret_cast<void*>(std::uintptr_t{1});
    send_message(NVSEMessagingInterface::kMessage_PostLoadGame, success);
    check_rebind(shot_events, "A saved game with a co-save must restore the flushed handlers");

    send_message(NVSEMessagingInterface::kMessage_PreLoadGame);
    send_message(NVSEMessagingInterface::kMessage_PostLoadGame, success);
    check_rebind(2U * shot_events, "A saved game without a co-save must rebind retained handlers");
}

void verify_partial_registration(const std::filesystem::path& log_path, std::uintmax_t offset) {
    require(host.handlers[1] == nullptr && std::ranges::count(host.handlers, nullptr) == 1,
        "Rejecting projectile events must leave unrelated handlers active");
    send_message(NVSEMessagingInterface::kMessage_NewGame);
    require(host.handlers[1] == nullptr && std::ranges::count(host.handlers, nullptr) == 1,
        "Rebinding an unavailable projectile source must preserve unrelated handlers");
    constexpr std::string_view unavailable = "native event unavailable: ShowOff:OnProjectileCreate";
    const auto failure_log = log_since(log_path, offset);
    require(failure_log.contains("gameplay observation active with limited native event sources") &&
                failure_log.contains("shot feedback unavailable") &&
                failure_log.contains(unavailable) &&
                failure_log.find(unavailable) == failure_log.rfind(unavailable),
        "Limited observation must report the exact missing dependency once");

    host.reject_projectile = false;
    send_message(NVSEMessagingInterface::kMessage_PostLoadGame);
    require(handlers_installed(), "Rebinding must restore a previously unavailable shot source");
    const auto recovery_log = log_since(log_path, offset);
    require(recovery_log.contains("native event restored: ShowOff:OnProjectileCreate") &&
                recovery_log.contains("shot feedback restored"),
        "Restored sources and shot capability must be reported");
}

void verify_plugin_compatibility(Query query, Load load, NVSEInterface& extender) {
    if (query == nullptr || load == nullptr) {
        throw std::runtime_error{"The two xNVSE exports must be available"};
    }

    extender.nvseVersion = PACKED_NVSE_VERSION;
    extender.runtimeVersion = RUNTIME_VERSION_1_4_0_525;
    extender.QueryInterface = query_interface;
    extender.GetPluginHandle = [] {
        return plugin_handle;
    };
    extender.GetRuntimeDirectory = [] {
        return host.directory.c_str();
    };
    PluginInfo info{};
    require(query(&extender, &info), "New Vegas 1.4.0.525 with xNVSE 6.4.9 must pass Query");
    require(info.name != nullptr && std::string_view{info.name} == "InputNextFalloutNewVegas",
        "Plugin metadata must name the adapter");
    extender.nvseVersion = MAKE_NEW_VEGAS_VERSION(6U, 4U, 8U);
    require(!query(&extender, &info) && !load(&extender),
        "xNVSE older than the supported 6.4.9 SDK must be rejected before startup");
    extender.nvseVersion = MAKE_NEW_VEGAS_VERSION(6U, 4U, 10U);
    require(query(&extender, &info), "A newer xNVSE version may expose the same interfaces");
    extender.nvseVersion = PACKED_NVSE_VERSION;
    extender.isEditor = 1;
    require(!query(&extender, &info) && !load(&extender), "The editor must be rejected");
    extender.isEditor = 0;
    const auto runtime_directory = extender.GetRuntimeDirectory;
    extender.GetRuntimeDirectory = nullptr;
    require(!query(&extender, &info) && !load(&extender),
        "Query and Load must require the runtime directory callback");
    extender.GetRuntimeDirectory = runtime_directory;
}
} // namespace

int main(int argc, const char* const* argv) {
    return inputnext::test::run_main("New Vegas plugin loading", [&] {
        require(argc == 6,
            "Expected an adapter DLL, runtime DLL, game directory, scenario and audio bank "
            "fixture");
        const auto game = std::filesystem::absolute(argv[3]);
        host.directory = game.string();
        const auto plugins = game / "Data" / "NVSE" / "Plugins";
        std::filesystem::create_directories(plugins);
        const auto log_path = plugins / "InputNextFalloutNewVegas.log";
        const auto log_offset = log_size(log_path);
        const std::string_view mode{argv[4]};
        host.reject_projectile = mode == "partial-events";
        host.reject_terminal_failure = mode == "partial-terminal-events";
        host.event_manager_available = mode != "missing-event-manager";
        const bool valid_runtime = mode != "missing-runtime";
        const auto settings = prepare_settings(plugins, mode, argv[5]);
        const bool initialized = valid_runtime && settings.valid;
        const auto runtime = game / "inputnext_runtime.dll";
        if (valid_runtime) {
            std::filesystem::copy_file(argv[2],
                runtime,
                std::filesystem::copy_options::overwrite_existing);
        }
        require(std::filesystem::is_regular_file(runtime) == valid_runtime,
            "Runtime fixture is missing or unexpected");

        const auto plugin = std::filesystem::absolute(argv[1]);
        // xNVSE keeps loaded plugins for the lifetime of the process.
        auto* const module = LoadLibraryW(plugin.c_str());
        require(module != nullptr, "Unable to load the New Vegas DLL");
        const auto query = reinterpret_cast<Query>(GetProcAddress(module, "NVSEPlugin_Query"));
        const auto load = reinterpret_cast<Load>(GetProcAddress(module, "NVSEPlugin_Load"));
        NVSEInterface extender{};
        verify_plugin_compatibility(query, load, extender);
        require(load(&extender), "xNVSE Load must register its messaging listener");
        require(host.listener != nullptr && host.registrations == 1,
            "Expected one messaging listener");
        require(host.subscriptions == 0, "Event registration must wait for post-load ABI checks");

        send_message(NVSEMessagingInterface::kMessage_PostPostLoad);
        send_message(NVSEMessagingInterface::kMessage_PostPostLoad);
        const auto startup_log = log_since(log_path, log_offset);
        verify_settings_diagnostics(settings, startup_log);
        require(startup_log.contains("stealth lightbar unavailable") == initialized,
            "Startup must report the missing game detection entry point independently of other "
            "effects");
        require(host.subscriptions ==
                    (initialized && host.event_manager_available ? expected_events.size() : 0U),
            "Only compatible settings and runtime may register each event once");

        constexpr std::string_view hook_unavailable = "dry-fire feedback unavailable";
        require(!startup_log.contains(hook_unavailable) &&
                    !startup_log.contains("native dry-fire source ready"),
            "The loader must leave the game call untouched until DeferredInit");
        send_message(NVSEMessagingInterface::kMessage_DeferredInit);
        send_message(NVSEMessagingInterface::kMessage_DeferredInit);
        const auto deferred_log = log_since(log_path, log_offset);
        require(deferred_log.contains(hook_unavailable) == initialized &&
                    deferred_log.find(hook_unavailable) == deferred_log.rfind(hook_unavailable) &&
                    !deferred_log.contains("native dry-fire source ready"),
            "DeferredInit must try the hook once after validation and reject the fixture image");

        if (host.reject_projectile) {
            verify_partial_registration(log_path, log_offset);
        } else if (host.reject_terminal_failure) {
            require(*handler_slot("onterminalhackfailed") == nullptr &&
                        std::ranges::count(host.handlers, nullptr) == 1 &&
                        startup_log.contains("native event unavailable: onterminalhackfailed"),
                "Rejecting terminal failure must preserve the other native sources");
        } else if (initialized && host.event_manager_available) {
            require(handlers_installed(),
                "The adapter must register the expected native game events");
            verify_shot_rebinding();
        } else if (initialized) {
            require(startup_log.contains("xNVSE EventManager is incomplete") &&
                        startup_log.contains(
                            "gameplay observation active with limited native event sources"),
                "Missing EventManager must preserve observation and report limited event coverage");
        }
        const auto subscriptions = host.subscriptions;
        send_message(mode == "partial-events" ? NVSEMessagingInterface::kMessage_ExitGame_Console
                                              : NVSEMessagingInterface::kMessage_ExitGame);
        send_message(NVSEMessagingInterface::kMessage_ExitGame);
        send_message(NVSEMessagingInterface::kMessage_PostPostLoad);
        send_message(NVSEMessagingInterface::kMessage_NewGame);
        require(host.subscriptions == subscriptions, "Exit must close event admission permanently");
        return 0;
    });
}
