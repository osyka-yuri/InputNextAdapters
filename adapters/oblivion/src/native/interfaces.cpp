#include "interfaces.hpp"
#include "diagnostics.hpp"
#include "effect_worker.hpp"
#include "event_handler.hpp"
#include "native/options_input.hpp"
#include "observation.hpp"
#include "player_state.hpp"
#include "task_handler.hpp"
#include "xobse_sdk.hpp"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

namespace oblivion_adapter::native {
namespace {
char module_anchor{};

struct ResolvedInterfaces {
    OBSEEventManagerInterface& event_manager;
    OBSETasks2Interface& tasks;
    OBSEMessagingInterface& messaging;
    OBSESerializationInterface& serialization;
    PluginHandle plugin_handle;
    const OBSEConsoleInterface* pause_console;
};

std::optional<ResolvedInterfaces> check_compatibility(const OBSEInterface* obse) noexcept {
    if (!obse) {
        diagnostics::write("check_compatibility: null OBSEInterface");
        return std::nullopt;
    }
    if (obse->isEditor != 0) {
        diagnostics::write("check_compatibility: editor mode rejected");
        return std::nullopt;
    }
    if (obse->oblivionVersion != OBLIVION_VERSION_1_2_416) {
        diagnostics::write("check_compatibility: oblivionVersion mismatch");
        return std::nullopt;
    }
    // obseVersion reports the major version. QueryInterface checks capabilities.
    if (obse->obseVersion < 22) {
        diagnostics::write("check_compatibility: unsupported OBSE major version (expected >= 22)");
        return std::nullopt;
    }
    if (!obse->QueryInterface || !obse->GetPluginHandle) {
        diagnostics::write("check_compatibility: missing QueryInterface or GetPluginHandle");
        return std::nullopt;
    }

    const PluginHandle plugin_handle = obse->GetPluginHandle();
    if (plugin_handle == static_cast<PluginHandle>(kPluginHandle_Invalid)) {
        diagnostics::write("check_compatibility: invalid plugin handle");
        return std::nullopt;
    }

    auto* event_mgr =
        static_cast<OBSEEventManagerInterface*>(obse->QueryInterface(kInterface_EventManager));
    if (!event_mgr || !event_mgr->RegisterEvent) {
        diagnostics::write("check_compatibility: missing OBSEEventManagerInterface");
        return std::nullopt;
    }

    auto* tasks2 = static_cast<OBSETasks2Interface*>(obse->QueryInterface(kInterface_Tasks2));
    if (!tasks2 || !tasks2->EnqueueTask || !tasks2->RemoveTask ||
        tasks2->version < OBSETasks2Interface::kVersion) {
        diagnostics::write("check_compatibility: missing or outdated OBSETasks2Interface");
        return std::nullopt;
    }

    auto* messaging =
        static_cast<OBSEMessagingInterface*>(obse->QueryInterface(kInterface_Messaging));
    if (!messaging || !messaging->RegisterListener ||
        messaging->version < OBSEMessagingInterface::kVersion) {
        diagnostics::write("check_compatibility: missing or outdated OBSEMessagingInterface");
        return std::nullopt;
    }

    auto* serialization =
        static_cast<OBSESerializationInterface*>(obse->QueryInterface(kInterface_Serialization));
    if (!serialization || !serialization->SetNewGameCallback ||
        serialization->version < OBSESerializationInterface::kVersion) {
        diagnostics::write("check_compatibility: missing or outdated OBSESerializationInterface");
        return std::nullopt;
    }

    const auto* console =
        static_cast<const OBSEConsoleInterface*>(obse->QueryInterface(kInterface_Console));
    if (console &&
        (console->version < OBSEConsoleInterface::kVersion || !console->RunScriptLine2)) {
        console = nullptr;
    }
    return ResolvedInterfaces{.event_manager = *event_mgr,
        .tasks = *tasks2,
        .messaging = *messaging,
        .serialization = *serialization,
        .plugin_handle = plugin_handle,
        .pause_console = console};
}
} // namespace

PluginInterfaceManager& PluginInterfaceManager::instance() noexcept {
    static PluginInterfaceManager s_instance;
    return s_instance;
}

bool PluginInterfaceManager::on_game_thread() const noexcept {
    return game_thread_id_.load(std::memory_order_acquire) == GetCurrentThreadId();
}

bool PluginInterfaceManager::initialize(const OBSEInterface* obse,
    const std::filesystem::path& game_dir) noexcept {
    if (loaded_.load(std::memory_order_acquire)) {
        return false;
    }
    const auto resolved = check_compatibility(obse);
    if (!resolved) {
        return false;
    }

    std::unique_ptr<EffectWorker> worker;
    try {
        worker = EffectWorker::prepare(game_dir, resolved->pause_console != nullptr);
    } catch (...) {
        diagnostics::write("initialize: failed to prepare controller output");
        return false;
    }
    if (!worker) {
        return false;
    }

    // Keep the DLL loaded while callbacks are registered. Release it on rollback.
    HMODULE retained_module{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&module_anchor),
            &retained_module) == FALSE) {
        diagnostics::write("initialize: unable to retain callback module");
        return false;
    }

    // Serialization indexes new-game callbacks by plugin handle, avoiding EventManager collisions.
    resolved->serialization.SetNewGameCallback(resolved->plugin_handle,
        &PluginInterfaceManager::on_serialization_new_game);
    diagnostics::write(
        "initialize: registered isolated new game callback via OBSESerializationInterface");

    if (!TaskHandler::register_task(&resolved->tasks)) {
        diagnostics::write("initialize: failed to enqueue game thread task");
        resolved->serialization.SetNewGameCallback(resolved->plugin_handle, nullptr);
        worker.reset();
        FreeLibrary(retained_module);
        return false;
    }

    // Register the listener last because xOBSE provides no way to unregister it.
    if (!resolved->messaging.RegisterListener(resolved->plugin_handle,
            "OBSE",
            &PluginInterfaceManager::on_obse_message)) {
        diagnostics::write("initialize: failed to register OBSE message listener");
        resolved->serialization.SetNewGameCallback(resolved->plugin_handle, nullptr);
        TaskHandler::unregister_task(&resolved->tasks);
        worker.reset();
        FreeLibrary(retained_module);
        return false;
    }
    diagnostics::write("initialize: registered OBSE message listener");

    context_.event_manager = &resolved->event_manager;
    context_.tasks = &resolved->tasks;
    context_.serialization = &resolved->serialization;
    context_.plugin_handle = resolved->plugin_handle;
    if (worker->options_available()) {
        context_.pause_console = resolved->pause_console;
        diagnostics::write("Options shortcut enabled");
    }
    // The retained module and worker keep the callbacks valid until process exit.
    session_state_ = SessionState::NoSession;
    context_.effects = &EffectWorker::activate_for_process(std::move(worker));
    loaded_.store(true, std::memory_order_release);

    diagnostics::write("initialize: successfully initialized all interfaces");
    return true;
}

void PluginInterfaceManager::start_new_game_session() noexcept {
    session_state_ = SessionState::InSession;
    pause_press_.reset(input_clock());
    context_.effects->invalidate();
    ObservationManager::instance().reset();
    TaskHandler::reset_diagnostics();
    diagnostics::write("start_new_game_session: started fresh game session with new epoch");
}

void PluginInterfaceManager::on_serialization_new_game(void* /*reserved*/) noexcept {
    auto& self = instance();
    if (!self.on_game_thread() || !self.context_.effects) {
        return;
    }

    // xOBSE also calls HandleNewGame when loading a save without an .obse co-save.
    if (self.session_state_ == SessionState::Loading) {
        diagnostics::write(
            "on_serialization_new_game: ignored during save loading (save without .obse co-save)");
        return;
    }

    diagnostics::write("on_serialization_new_game: genuine new game confirmed via isolated "
                       "serialization callback");
    self.start_new_game_session();
}

void PluginInterfaceManager::set_session_state(SessionState state) noexcept {
    if (session_state_ == state && state != SessionState::InSession) {
        return;
    }
    const SessionState old_state = session_state_;
    session_state_ = state;
    pause_press_.reset(input_clock());

    if (state != SessionState::InSession || old_state != SessionState::Loading) {
        // A save load already reset state on entry to Loading.
        context_.effects->invalidate();
        ObservationManager::instance().reset();
        TaskHandler::reset_diagnostics();
    }
}

void PluginInterfaceManager::poll_pause_shortcut() noexcept {
    if (!context_.pause_console) {
        return;
    }
    if (!is_session_active() || !foreground_is_game()) {
        pause_press_.reset(input_clock());
        return;
    }
    if (!pause_press_.take(context_.effects->options_sample(), std::chrono::steady_clock::now())) {
        return;
    }
    const auto* command = escape_command(PlayerStateReader::read_pause_context());
    if (!foreground_is_game()) {
        pause_press_.reset(input_clock());
        return;
    }
    if (command) {
        const bool sent = context_.pause_console->RunScriptLine2(command, nullptr, true);
        diagnostics::write(
            sent ? "Options shortcut sent Escape" : "Options shortcut could not send Escape");
    }
}

void PluginInterfaceManager::shutdown() noexcept {
    session_state_ = SessionState::NoSession;
    auto* const worker = context_.effects;
    if (context_.tasks) {
        TaskHandler::unregister_task(context_.tasks);
    }
    if (context_.serialization && context_.serialization->SetNewGameCallback) {
        context_.serialization->SetNewGameCallback(context_.plugin_handle, nullptr);
    }
    ObservationManager::instance().reset();
    TaskHandler::reset_state();
    context_ = PluginContext{};
    diagnostics::write("shutdown: callbacks disabled and output cleanup requested");
    if (worker) {
        worker->stop();
    }
}

void PluginInterfaceManager::on_obse_message(OBSEMessagingInterface::Message* msg) noexcept {
    auto& self = instance();
    if (!msg || !self.loaded_.load(std::memory_order_acquire)) {
        return;
    }

    switch (msg->type) {
    case OBSEMessagingInterface::kMessage_PostLoad:
        diagnostics::write("on_obse_message: kMessage_PostLoad received");
        return;
    case OBSEMessagingInterface::kMessage_PostPostLoad:
        diagnostics::write("on_obse_message: kMessage_PostPostLoad received");
        return;
    case OBSEMessagingInterface::kMessage_GameInitialized: {
        // xOBSE sends this from HandleMainLoopHook before running Tasks2 callbacks.
        std::uint32_t unbound{};
        if (self.game_thread_id_.compare_exchange_strong(unbound, GetCurrentThreadId())) {
            diagnostics::write(
                "on_obse_message: kMessage_GameInitialized received (main menu ready)");
            EventHandler::register_events(self.context_.event_manager);
        }
        return;
    }
    default:
        break;
    }

    if (!self.on_game_thread() || !self.context_.effects) {
        return;
    }
    switch (msg->type) {
    case OBSEMessagingInterface::kMessage_PreLoadGame:
        diagnostics::write("on_obse_message: kMessage_PreLoadGame received (save loading started)");
        self.set_session_state(SessionState::Loading);
        break;
    case OBSEMessagingInterface::kMessage_LoadGame:
        diagnostics::write("on_obse_message: kMessage_LoadGame received");
        self.set_session_state(SessionState::Loading);
        break;
    case OBSEMessagingInterface::kMessage_PostLoadGame: {
        diagnostics::write("on_obse_message: kMessage_PostLoadGame received");
        const bool load_succeeded = msg->data != nullptr;
        if (load_succeeded) {
            diagnostics::write("on_obse_message: save load succeeded, transitioning to InSession");
            self.set_session_state(SessionState::InSession);
        } else {
            diagnostics::write("on_obse_message: save load failed, returning to NoSession");
            self.set_session_state(SessionState::NoSession);
        }
        break;
    }
    case OBSEMessagingInterface::kMessage_ExitToMainMenu:
        diagnostics::write("on_obse_message: kMessage_ExitToMainMenu received");
        self.set_session_state(SessionState::NoSession);
        break;
    case OBSEMessagingInterface::kMessage_ExitGame:
    case OBSEMessagingInterface::kMessage_ExitGame_Console:
        diagnostics::write("on_obse_message: exit game message received");
        self.shutdown();
        break;
    default:
        break;
    }
}

} // namespace oblivion_adapter::native
