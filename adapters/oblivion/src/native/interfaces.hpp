#pragma once

#include "options_input.hpp"
#include "xobse_sdk.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>

namespace oblivion_adapter::native {

class EffectWorker;

enum class SessionState : std::uint8_t {
    NoSession,
    Loading,
    InSession,
};

struct PluginContext {
    OBSEEventManagerInterface* event_manager{nullptr};
    OBSETasks2Interface* tasks{nullptr};
    OBSESerializationInterface* serialization{nullptr};
    PluginHandle plugin_handle{static_cast<PluginHandle>(0xFFFFFFFFU)};
    EffectWorker* effects{};
    const OBSEConsoleInterface* pause_console{};
};

// Load may run off the game thread. GameInitialized binds the thread that
// handles session changes, gameplay callbacks and SDK reads.
class PluginInterfaceManager {
public:
    static PluginInterfaceManager& instance() noexcept;

    PluginInterfaceManager(const PluginInterfaceManager&) = delete;
    PluginInterfaceManager& operator=(const PluginInterfaceManager&) = delete;
    PluginInterfaceManager(PluginInterfaceManager&&) = delete;
    PluginInterfaceManager& operator=(PluginInterfaceManager&&) = delete;

    [[nodiscard]] bool initialize(const OBSEInterface* obse,
        const std::filesystem::path& game_dir) noexcept;
    void shutdown() noexcept;
    [[nodiscard]] bool on_game_thread() const noexcept;
    void poll_pause_shortcut() noexcept;

    [[nodiscard]] EffectWorker* effects() const noexcept { return context_.effects; }

    [[nodiscard]] SessionState session_state() const noexcept { return session_state_; }

    [[nodiscard]] bool is_session_active() const noexcept {
        return session_state_ == SessionState::InSession;
    }

    static void on_obse_message(OBSEMessagingInterface::Message* msg) noexcept;
    static void on_serialization_new_game(void* reserved) noexcept;

private:
    PluginInterfaceManager() = default;
    ~PluginInterfaceManager() = default;

    void set_session_state(SessionState state) noexcept;
    void start_new_game_session() noexcept;

    PluginContext context_{};
    OptionsPress pause_press_;
    SessionState session_state_{SessionState::NoSession};
    std::atomic<bool> loaded_;
    std::atomic<std::uint32_t> game_thread_id_;
};

} // namespace oblivion_adapter::native
