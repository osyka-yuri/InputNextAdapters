#include "load_scenarios.hpp"
#include "mock_xobse.hpp"
#include "model.hpp"
#include "native/xobse_sdk.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace oblivion_adapter::test {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

LoadedPlugin LoadedPlugin::load(const char* plugin_path) {
    LoadedPlugin plugin{};
    plugin.handle = LoadLibraryA(plugin_path);
    if (plugin.handle == nullptr) {
        throw std::runtime_error{"Failed to load InputNextOblivion DLL"};
    }

    plugin.load_fn =
        reinterpret_cast<LoadFunction>(GetProcAddress(plugin.handle, "OBSEPlugin_Load"));
    require(plugin.load_fn != nullptr, "OBSEPlugin_Load export not found");

    plugin.get_obs_fn = reinterpret_cast<GetObservationFunction>(
        GetProcAddress(plugin.handle, "InputNext_GetObservation"));
    require(plugin.get_obs_fn != nullptr, "InputNext_GetObservation export not found");

    plugin.get_epoch_fn = reinterpret_cast<GetSessionEpochFunction>(
        GetProcAddress(plugin.handle, "InputNext_GetSessionEpoch"));
    require(plugin.get_epoch_fn != nullptr, "InputNext_GetSessionEpoch export not found");

    plugin.get_state_fn = reinterpret_cast<GetSessionStateFunction>(
        GetProcAddress(plugin.handle, "InputNext_GetSessionState"));
    require(plugin.get_state_fn != nullptr, "InputNext_GetSessionState export not found");

    return plugin;
}

void LoadedPlugin::unload() noexcept {
    if (handle) {
        FreeLibrary(handle);
        handle = nullptr;
    }
}

namespace {

constexpr auto kNativeEventNames = std::to_array<std::string_view>({
    "onhealthdamage",
    "onhit",
    "onhitwith",
    "onattack",
    "onbowattack",
    "onbowarrowattach",
    "onmagiccast",
    "onmagicapply",
    "onrecoil",
    "onstagger",
    "ondodge",
    "onknockout",
    "onfallimpact",
    "onwatersurface",
    "onwaterdive",
    "ondrinkpotion",
    "oneatingredient",
    "onactorequip",
    "onactorunequip",
    "onactordrop",
    "oncreatepotion",
    "onenchant",
    "oncreatespell",
    "onvampirefeed",
    "onsoultrap",
    "onskillup",
    "onquestcomplete",
    "onmapmarkeradd",
});

class ScopedRuntimeMode final {
public:
    explicit ScopedRuntimeMode(const char* mode) {
        constexpr auto variable = "INPUTNEXT_OBLIVION_TEST_RUNTIME_MODE";
        const auto capacity = static_cast<DWORD>(previous_.size());
        const DWORD length = GetEnvironmentVariableA(variable, previous_.data(), capacity);
        had_previous_ = length != 0 && length < capacity;
        if (SetEnvironmentVariableA(variable, mode) == FALSE) {
            throw std::runtime_error("Failed to set test runtime mode");
        }
    }

    ~ScopedRuntimeMode() {
        SetEnvironmentVariableA("INPUTNEXT_OBLIVION_TEST_RUNTIME_MODE",
            had_previous_ ? previous_.data() : nullptr);
    }

    ScopedRuntimeMode(const ScopedRuntimeMode&) = delete;
    ScopedRuntimeMode& operator=(const ScopedRuntimeMode&) = delete;
    ScopedRuntimeMode(ScopedRuntimeMode&&) = delete;
    ScopedRuntimeMode& operator=(ScopedRuntimeMode&&) = delete;

private:
    std::array<char, 128> previous_{};
    bool had_previous_{};
};

bool contains_event(std::string_view name) noexcept {
    return std::ranges::find(kNativeEventNames, name) != kNativeEventNames.end();
}

bool is_lowercase_ascii(std::string_view name) noexcept {
    return std::ranges::all_of(name, [](char value) { return value < 'A' || value > 'Z'; });
}

void verify_native_events(const MockEnvironment& env,
    std::span<const std::string_view> unavailable = {}) {
    for (const auto event_name : kNativeEventNames) {
        const bool collided = std::ranges::find(unavailable, event_name) != unavailable.end();
        const std::string event_name_with_terminator{event_name};
        const auto attempts = std::ranges::count_if(env.register_event_names,
            [event_name](const std::string& name) { return std::string_view{name} == event_name; });
        require(attempts == 1,
            "Each supported native event is attempted exactly once after initialization");
        require(env.count_callbacks(event_name_with_terminator.c_str()) == 1U,
            "Each native event keeps one callback when registered or externally occupied");
        if (collided) {
            require(env.get_first_function(event_name_with_terminator.c_str()) != nullptr,
                "A conflicting event keeps its existing external callback");
        }
    }
    for (const auto& name : env.register_event_names) {
        require(is_lowercase_ascii(name), "Native xOBSE event names must be lowercase");
        require(contains_event(name), "Only approved native events are registered");
    }
}

void scenario_compatible_load(const char* plugin_path) {
    const auto plugin = LoadedPlugin::load(plugin_path);
    const auto& env = MockEnvironment::instance();
    const auto obse = make_obse_interface();
    bool result = false;
    std::jthread loader([&] { result = plugin.load_fn(&obse); });
    loader.join();
    require(result, "Expected compatible OBSEPlugin_Load to succeed");
    require(GetModuleHandleA("inputnext_runtime.dll") != nullptr,
        "Successful Load retains its validated runtime module");
    require(env.register_listener_calls == 1, "RegisterListener should be called once");
    require(env.registered_listener_sender == "OBSE", "Listener registered for OBSE sender");
    require(env.enqueue_task_calls == 1, "EnqueueTask should be called once");
    require(env.enqueued_task_func != nullptr, "Task callback function set");
    require(env.set_newgame_callback_calls == 1,
        "SetNewGameCallback called for isolated new game events");
    require(env.registered_newgame_callback != nullptr, "New game callback function set");
    require(env.register_event_calls == 0,
        "No native event callbacks registered during initial Load");

    OBSEMessagingInterface::Message msg{};
    msg.type = OBSEMessagingInterface::kMessage_PostLoad;
    msg.sender = "OBSE";
    env.registered_msg_callback(&msg);
    require(env.register_event_calls == 0,
        "Native callbacks are NOT registered on kMessage_PostLoad");

    msg.type = OBSEMessagingInterface::kMessage_GameInitialized;
    env.registered_msg_callback(&msg);
    require(env.count_callbacks("onhealthdamage") == 1,
        "onhealthdamage registered on kMessage_GameInitialized");
    require(env.registered_damage_func != nullptr, "Damage function registered");
    verify_native_events(env);
    const int registered_events = env.register_event_calls;

    env.registered_msg_callback(&msg);
    require(env.count_callbacks("onhealthdamage") == 1,
        "Duplicate kMessage_GameInitialized must not duplicate callbacks");
    require(env.register_event_calls == registered_events,
        "Duplicate GameInitialized must not re-register native event callbacks");

    msg.type = OBSEMessagingInterface::kMessage_PostLoadGame;
    msg.data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
    env.registered_msg_callback(&msg);
    const auto task = env.enqueued_task_func;
    if (task == nullptr) {
        throw std::runtime_error{"Task callback remains registered after PostLoadGame"};
    }
    task();
    oblivion_adapter::Observation observation{};
    require(plugin.get_state_fn() == 2 && plugin.get_obs_fn(&observation) &&
                observation.tick_count == 1,
        "Lifecycle messages and tasks on the game thread work after Load on another thread");

    msg.type = OBSEMessagingInterface::kMessage_ExitToMainMenu;
    std::jthread foreign_thread([&] { env.registered_msg_callback(&msg); });
    foreign_thread.join();
    require(plugin.get_state_fn() == 2, "A foreign thread cannot change the active session");
}

void scenario_invalid_runtime_abi(const char* plugin_path) {
    ScopedRuntimeMode const runtime_mode{"bad_size"};
    const auto plugin = LoadedPlugin::load(plugin_path);
    auto const& env = MockEnvironment::instance();
    const auto obse = make_obse_interface();
    require(!plugin.load_fn(&obse), "Load rejects a runtime with an incompatible Effects ABI");
    require(env.register_event_calls == 0 && env.xobse_callbacks.empty(),
        "Invalid runtime ABI is rejected before native callbacks are published");
    require(env.register_listener_calls == 0 && env.enqueued_task_func == nullptr,
        "Invalid runtime startup leaves no queued game worker or messaging listener");
    require(GetModuleHandleA("inputnext_runtime.dll") == nullptr,
        "Rejected runtime module is released during startup rollback");
}

void scenario_missing_serialization(const char* plugin_path) {
    auto plugin = LoadedPlugin::load(plugin_path);
    auto& env = MockEnvironment::instance();
    env.has_serialization = false;

    const auto obse = make_obse_interface();
    const bool result = plugin.load_fn(&obse);
    require(!result, "Load must fail when OBSESerializationInterface is missing");

    plugin.unload();
}

void scenario_outdated_serialization(const char* plugin_path) {
    auto plugin = LoadedPlugin::load(plugin_path);
    auto& env = MockEnvironment::instance();
    env.serialization.version = 0;

    const auto obse = make_obse_interface();
    const bool result = plugin.load_fn(&obse);
    require(!result, "Load must fail when Serialization version is outdated");

    plugin.unload();
}

void scenario_missing_event_manager(const char* plugin_path) {
    auto plugin = LoadedPlugin::load(plugin_path);
    auto& env = MockEnvironment::instance();
    env.has_event_mgr = false;

    const auto obse = make_obse_interface();
    const bool result = plugin.load_fn(&obse);
    require(!result, "Load must fail when EventManager is missing");

    plugin.unload();
}

void scenario_missing_tasks2(const char* plugin_path) {
    auto plugin = LoadedPlugin::load(plugin_path);
    auto& env = MockEnvironment::instance();
    env.has_tasks2 = false;

    const auto obse = make_obse_interface();
    const bool result = plugin.load_fn(&obse);
    require(!result, "Load must fail when Tasks2 is missing");

    plugin.unload();
}

void scenario_outdated_tasks2(const char* plugin_path) {
    auto plugin = LoadedPlugin::load(plugin_path);
    auto& env = MockEnvironment::instance();
    env.tasks2.version = 0;

    const auto obse = make_obse_interface();
    const bool result = plugin.load_fn(&obse);
    require(!result, "Load must fail when Tasks2 version is outdated");

    plugin.unload();
}

void scenario_missing_messaging(const char* plugin_path) {
    auto plugin = LoadedPlugin::load(plugin_path);
    auto& env = MockEnvironment::instance();
    env.has_messaging = false;

    const auto obse = make_obse_interface();
    const bool result = plugin.load_fn(&obse);
    require(!result, "Load must fail when Messaging is missing");

    plugin.unload();
}

void scenario_invalid_plugin_handle(const char* plugin_path) {
    auto plugin = LoadedPlugin::load(plugin_path);
    auto& env = MockEnvironment::instance();
    env.handle = static_cast<PluginHandle>(kPluginHandle_Invalid);

    const auto obse = make_obse_interface();
    const bool result = plugin.load_fn(&obse);
    require(!result, "Load must fail when plugin handle is invalid");

    plugin.unload();
}

void scenario_load_failure_rollback(const char* plugin_path) {
    auto plugin = LoadedPlugin::load(plugin_path);
    auto& env = MockEnvironment::instance();
    env.listener_register_result = false;

    const auto obse = make_obse_interface();
    const bool result = plugin.load_fn(&obse);
    require(!result, "Load must fail when RegisterListener fails");
    require(env.enqueue_task_calls == 1, "EnqueueTask was attempted");
    require(env.remove_task_calls == 1, "Task must be rolled back with RemoveTask on failure");
    require(env.enqueued_task_func == nullptr,
        "Task callback must be removed from queue on load rollback");
    require(env.active_task_ptr == nullptr, "Active task pointer must be cleared on load rollback");
    require(env.registered_newgame_callback == nullptr,
        "New game callback must be reset to nullptr on failure");
    require(env.registered_msg_callback == nullptr,
        "Messaging listener must NOT be registered when RegisterListener fails");
    require(env.count_callbacks("onhealthdamage") == 0,
        "No damage callbacks registered on load failure");

    plugin.unload();
}

void scenario_damage_event_unavailable(const char* plugin_path) {
    const auto other_damage_func = [](void*, void*, TESObjectREFR*) {
    };

    auto& env = MockEnvironment::instance();
    env.add_external_callback("onhealthdamage", other_damage_func);

    const auto plugin = LoadedPlugin::load(plugin_path);
    const auto obse = make_obse_interface();
    const bool result = plugin.load_fn(&obse);
    require(result, "Load succeeds even when damage event slot is occupied");

    OBSEMessagingInterface::Message msg{};
    msg.type = OBSEMessagingInterface::kMessage_GameInitialized;
    msg.sender = "OBSE";
    env.registered_msg_callback(&msg);

    require(env.has_function("onhealthdamage", other_damage_func),
        "Other plugin's callback must NOT be destroyed by InputNextOblivion");
    require(env.unregister_event_calls == 0,
        "UnregisterEvent must NOT be called on conflict/unavailability");
    verify_native_events(env, std::array<std::string_view, 1>{"onhealthdamage"});

    env.registered_newgame_callback(nullptr);
    require(plugin.get_state_fn() == 2,
        "Session successfully starts InSession despite damage callback unavailability");

    const int register_calls_before_tick = env.register_event_calls;
    env.enqueued_task_func();
    env.enqueued_task_func();
    require(env.register_event_calls == register_calls_before_tick,
        "TaskHandler must NOT spam RegisterEvent on every game tick");

    oblivion_adapter::Observation obs{};
    require(plugin.get_obs_fn(&obs), "Observation should be retrievable in degraded mode");
    require(obs.damage_event_count == 0, "Damage events are not captured in degraded mode");
    require(obs.tick_count == 2,
        "Two game loop ticks in active session must result in tick_count == 2");
}

void scenario_independent_event_collisions(const char* plugin_path) {
    const auto external_health = [](void*, void*, TESObjectREFR*) {
    };
    const auto external_attack = [](void*, void*, TESObjectREFR*) {
    };

    auto& env = MockEnvironment::instance();
    env.add_external_callback("onhealthdamage", external_health);
    env.add_external_callback("onattack", external_attack);

    const auto plugin = LoadedPlugin::load(plugin_path);
    const auto obse = make_obse_interface();
    require(plugin.load_fn(&obse), "Load succeeds despite two occupied event slots");

    OBSEMessagingInterface::Message msg{};
    msg.sender = "OBSE";
    msg.type = OBSEMessagingInterface::kMessage_GameInitialized;
    env.registered_msg_callback(&msg);

    const std::array<std::string_view, 2> unavailable{"onhealthdamage", "onattack"};
    verify_native_events(env, unavailable);
    require(env.has_function("onhealthdamage", external_health) &&
                env.has_function("onattack", external_attack),
        "Existing third-party handlers remain registered for colliding events");
    require(env.count_callbacks("onhit") == 1 && env.count_callbacks("onmagiccast") == 1 &&
                env.count_callbacks("ondrinkpotion") == 1,
        "Other event families register independently after individual collisions");
    require(env.unregister_event_calls == 0,
        "InputNextOblivion never unregisters callbacks while handling collisions");
}

void scenario_multi_plugin_safety(const char* plugin_path) {
    const auto other_damage_func = [](void*, void*, TESObjectREFR*) {
    };

    auto& env = MockEnvironment::instance();

    const auto plugin = LoadedPlugin::load(plugin_path);
    const auto obse = make_obse_interface();
    require(plugin.load_fn(&obse), "Load succeeds");

    OBSEMessagingInterface::Message msg{};
    msg.type = OBSEMessagingInterface::kMessage_GameInitialized;
    msg.sender = "OBSE";
    env.registered_msg_callback(&msg);
    require(env.count_callbacks("onhealthdamage") == 1, "InputNextOblivion registered");

    env.add_external_callback("onhealthdamage",
        other_damage_func,
        reinterpret_cast<TESObjectREFR*>(0x1234));
    require(env.count_callbacks("onhealthdamage") == 2, "Both callbacks registered");

    msg.type = OBSEMessagingInterface::kMessage_ExitGame;
    env.registered_msg_callback(&msg);

    require(env.unregister_event_calls == 0, "UnregisterEvent must NOT be called on shutdown");
    require(env.has_function("onhealthdamage", other_damage_func),
        "Other plugin's callback must NOT be destroyed by InputNextOblivion");
}

void scenario_newgame_vs_savegame(const char* plugin_path) {
    const auto plugin = LoadedPlugin::load(plugin_path);
    const auto& env = MockEnvironment::instance();
    const auto obse = make_obse_interface();
    require(plugin.load_fn(&obse), "Load succeeds");

    require(plugin.get_state_fn() == 0, "Initial state NoSession");
    const std::uint64_t ep0 = plugin.get_epoch_fn();

    OBSEMessagingInterface::Message msg{};
    msg.sender = "OBSE";
    msg.type = OBSEMessagingInterface::kMessage_GameInitialized;
    env.registered_msg_callback(&msg);

    env.registered_newgame_callback(nullptr);
    require(plugin.get_state_fn() == 2, "State transitions to InSession on genuine new game");
    const std::uint64_t ep_newgame = plugin.get_epoch_fn();
    require(ep_newgame > ep0, "Epoch advances on new game");

    msg.type = OBSEMessagingInterface::kMessage_PreLoadGame;
    env.registered_msg_callback(&msg);
    require(plugin.get_state_fn() == 1, "State transitions to Loading on PreLoadGame");
    const std::uint64_t ep_loading = plugin.get_epoch_fn();
    require(ep_loading > ep_newgame, "Epoch advances on save loading reset");

    // Loading a save without a co-save can invoke the serialization new-game callback.
    env.registered_newgame_callback(nullptr);
    require(plugin.get_state_fn() == 1,
        "State MUST remain Loading when newGame callback fires during save load");
    require(plugin.get_epoch_fn() == ep_loading,
        "Epoch must NOT advance on spurious newGame callback during save load");

    msg.type = OBSEMessagingInterface::kMessage_PostLoadGame;
    msg.data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
    env.registered_msg_callback(&msg);
    require(plugin.get_state_fn() == 2, "State transitions to InSession on PostLoadGame success");
    require(plugin.get_epoch_fn() == ep_loading,
        "Epoch remains consistent for the loaded savegame");

    msg.type = OBSEMessagingInterface::kMessage_PreLoadGame;
    env.registered_msg_callback(&msg);
    require(plugin.get_state_fn() == 1, "State is Loading");
    const std::uint64_t ep_loading2 = plugin.get_epoch_fn();

    env.registered_newgame_callback(nullptr);
    require(plugin.get_state_fn() == 1, "State remains Loading");

    msg.type = OBSEMessagingInterface::kMessage_PostLoadGame;
    msg.data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0));
    env.registered_msg_callback(&msg);
    require(plugin.get_state_fn() == 0, "State transitions to NoSession on failed load");
    require(plugin.get_epoch_fn() > ep_loading2,
        "Epoch advanced to invalidate failed load snapshot");

    oblivion_adapter::Observation obs{};
    plugin.get_obs_fn(&obs);
    require(!obs.player_available, "Player not available after failed load");
}

void scenario_clean_exit_game(const char* plugin_path) {
    const auto plugin = LoadedPlugin::load(plugin_path);
    const auto obse = make_obse_interface();
    const auto& env = MockEnvironment::instance();
    require(plugin.load_fn(&obse), "Load succeeds");
    require(env.active_task_ptr != nullptr, "Active task pointer stored upon successful load");

    OBSEMessagingInterface::Message msg{};
    msg.sender = "OBSE";
    msg.type = OBSEMessagingInterface::kMessage_GameInitialized;
    env.registered_msg_callback(&msg);
    const auto queued_task = env.enqueued_task_func;
    const auto event_callback = env.get_first_function("onhealthdamage");

    msg.type = OBSEMessagingInterface::kMessage_ExitGame;
    env.registered_msg_callback(&msg);

    require(env.remove_task_calls == 1, "Task unregistered on exit game");
    require(env.active_task_ptr == nullptr, "Active task pointer cleared on exit game");
    require(env.enqueued_task_func == nullptr, "Task callback removed from queue on exit game");
    require(env.registered_newgame_callback == nullptr, "New game callback reset on exit game");

    oblivion_adapter::Observation before{};
    require(plugin.get_obs_fn(&before), "Observation available before escaped task check");
    if (queued_task != nullptr) {
        queued_task();
    }
    if (event_callback != nullptr) {
        event_callback(nullptr, nullptr, nullptr);
    }
    oblivion_adapter::Observation after{};
    require(plugin.get_obs_fn(&after), "Observation available after escaped callback checks");
    require(after.tick_count == before.tick_count &&
                after.damage_event_count == before.damage_event_count,
        "Escaped task and retained native callback are inert after ExitGame");
    require(env.unregister_event_calls == 0,
        "ExitGame leaves escaped event callback owned by xOBSE and does not unregister it");
    require(env.enqueue_task_calls == 1 && env.remove_task_calls == 1,
        "A queued task that escaped removal remains safe to invoke after ExitGame");
}

} // namespace

const std::vector<Scenario>& get_scenarios() {
    static const std::vector<Scenario> s_scenarios = {
        Scenario{.name = "compatible_load", .func = scenario_compatible_load},
        Scenario{.name = "invalid_runtime_abi", .func = scenario_invalid_runtime_abi},
        Scenario{.name = "missing_serialization", .func = scenario_missing_serialization},
        Scenario{.name = "outdated_serialization", .func = scenario_outdated_serialization},
        Scenario{.name = "missing_event_manager", .func = scenario_missing_event_manager},
        Scenario{.name = "missing_tasks2", .func = scenario_missing_tasks2},
        Scenario{.name = "outdated_tasks2", .func = scenario_outdated_tasks2},
        Scenario{.name = "missing_messaging", .func = scenario_missing_messaging},
        Scenario{.name = "invalid_plugin_handle", .func = scenario_invalid_plugin_handle},
        Scenario{.name = "load_failure_rollback", .func = scenario_load_failure_rollback},
        Scenario{.name = "damage_event_unavailable", .func = scenario_damage_event_unavailable},
        Scenario{.name = "independent_event_collisions",
            .func = scenario_independent_event_collisions},
        Scenario{.name = "multi_plugin_safety", .func = scenario_multi_plugin_safety},
        Scenario{.name = "newgame_vs_savegame", .func = scenario_newgame_vs_savegame},
        Scenario{.name = "clean_exit_game", .func = scenario_clean_exit_game},
    };
    return s_scenarios;
}

bool run_scenario(const std::string& name, const char* plugin_path) {
    const auto& scenarios = get_scenarios();
    const auto match = std::ranges::find_if(scenarios,
        [&](const auto& scenario) { return scenario.name == name; });
    if (match != scenarios.end()) {
        match->func(plugin_path);
        return true;
    }
    return false;
}

} // namespace oblivion_adapter::test
