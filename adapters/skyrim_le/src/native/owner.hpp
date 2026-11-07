#pragma once

#include "observation.hpp"

#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/native/config.hpp>
#include <skyrim_adapter/native/task_schedule.hpp>

#include <inputnext_adapters/output.hpp>

#include "skse_sdk.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>

namespace skyrim_adapter::native {
enum class WorkerDispatchResult : std::uint8_t;

enum class OwnerStart : std::uint8_t { failed_before_callbacks, active, retained_inert };

// NOLINTNEXTLINE(misc-multiple-inheritance) -- SKSE sinks provide default virtual handlers.
class NativeOwner final : public BSTEventSink<SKSEActionEvent>,
                          public BSTEventSink<TESHitEvent>,
                          public BSTEventSink<MenuOpenCloseEvent> {
public:
    explicit NativeOwner(NativeConfig config) noexcept;
    ~NativeOwner() override;

    NativeOwner(const NativeOwner&) = delete;
    NativeOwner& operator=(const NativeOwner&) = delete;
    NativeOwner(NativeOwner&&) = delete;
    NativeOwner& operator=(NativeOwner&&) = delete;

    [[nodiscard]] OwnerStart start(const std::filesystem::path& absolute_runtime_path,
        PluginHandle plugin_handle,
        const SKSETaskInterface& task_interface,
        const SKSEMessagingInterface& messaging_interface) noexcept;
    void on_message(const SKSEMessagingInterface::Message& message) noexcept;

    EventResult ReceiveEvent(SKSEActionEvent* event,
        EventDispatcher<SKSEActionEvent>* dispatcher) override;
    EventResult ReceiveEvent(TESHitEvent* event, EventDispatcher<TESHitEvent>* dispatcher) override;
    EventResult ReceiveEvent(MenuOpenCloseEvent* event,
        EventDispatcher<MenuOpenCloseEvent>* dispatcher) override;

private:
    class ScheduledTask;
    static void message_callback(SKSEMessagingInterface::Message* message);
    void notify_output() noexcept;
    void notify_urgent() noexcept;
    void worker_main() noexcept;
    void update_focus(MonoTime now, bool is_focused);
    void report_task_pool_unavailable();
    void wait_for_task_pool(std::unique_lock<std::mutex>& lock);
    void report_task_pool_available();
    [[nodiscard]] WorkerDispatchResult dispatch_reserved_task(
        const TaskSchedule::Reservation& reservation);
    void wait_for_worker_wake(std::unique_lock<std::mutex>& lock);
    void scheduled_task(NativeTicket ticket) noexcept;
    void run_native_task(NativeTicket ticket);
    void complete_task() noexcept;
    void install_sinks();
    void enter_inert(const char* reason) noexcept;
    void record_output_diagnostics() noexcept;
    void record_observation_diagnostics(const detail::ObservationDiagnostics& current) noexcept;
    [[nodiscard]] bool update_menu_state(const Observation& observation,
        std::uint64_t read_menu_revision) noexcept;
    [[nodiscard]] static bool foreground_is_game() noexcept;

    inputnext::adapters::RuntimeOutput output_;
    Coordinator coordinator_;
    const SKSETaskInterface* task_interface_{};
    const SKSEMessagingInterface* messaging_interface_{};
    PluginHandle plugin_handle_{static_cast<PluginHandle>(kPluginHandle_Invalid)};
    EventDispatcher<SKSEActionEvent>* action_dispatcher_{};

    std::mutex schedule_mutex_;
    std::condition_variable schedule_cv_;
    TaskSchedule schedule_;
    std::thread worker_;
    std::atomic<bool> stopping_;
    std::atomic<bool> inert_;
    std::atomic<bool> callbacks_escaped_;
    std::atomic<bool> focused_;
    std::atomic<bool> ticket_admitted_;
    std::atomic<std::uint32_t> task_pool_states_seen_;
    std::atomic<bool> first_task_offered_logged_;
    std::atomic<bool> first_task_run_logged_;
    std::atomic<std::uint32_t> observation_gate_states_seen_;
    detail::MenuReadDiagnostics last_menu_diagnostics_{};
    std::uint8_t menu_diagnostics_logged_{};
    std::uint8_t vibration_states_seen_{};
    bool menu_diagnostics_seen_{};
    std::atomic<bool> first_action_logged_;
    std::atomic<bool> first_hit_logged_;
    std::atomic<std::uint32_t> output_status_seen_;
    std::atomic<std::uint32_t> output_route_seen_;
    std::atomic<std::uint32_t> output_fallback_seen_;
    std::atomic<std::uint32_t> output_owner_seen_;

    std::mutex menu_mutex_;
    detail::MenuSnapshot menu_snapshot_{};
    std::uint64_t menu_revision_{};
    bool menu_suppressed_{};

    bool action_registered_{};
    bool hit_registered_{};
    bool menu_registered_{};
    bool hit_sink_unavailable_logged_{};
    bool menu_sink_unavailable_logged_{};
};
} // namespace skyrim_adapter::native
