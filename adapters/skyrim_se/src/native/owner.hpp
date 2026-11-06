#pragma once

#include <skyrim_adapter/native/config.hpp>
#include <skyrim_adapter/native/task_schedule.hpp>

#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output.hpp>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>

#include <RE/B/BGSFootstepEvent.h>
#include <RE/B/BSTEvent.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/T/TESEquipEvent.h>
#include <RE/T/TESHitEvent.h>
#include <RE/T/TESPlayerBowShotEvent.h>
#include <SKSE/Events.h>
#include <SKSE/Interfaces.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>

namespace skyrim_adapter::native {
enum class OwnerStart : std::uint8_t { failed_before_callbacks, active, retained_inert };

class NativeOwner final : public RE::BSTEventSink<SKSE::ActionEvent>,
                          public RE::BSTEventSink<RE::TESHitEvent>,
                          public RE::BSTEventSink<RE::TESEquipEvent>,
                          public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
                          public RE::BSTEventSink<RE::BGSFootstepEvent>,
                          public RE::BSTEventSink<RE::TESPlayerBowShotEvent> {
public:
    explicit NativeOwner(NativeConfig config) noexcept;
    ~NativeOwner() override;

    NativeOwner(const NativeOwner&) = delete;
    NativeOwner& operator=(const NativeOwner&) = delete;
    NativeOwner(NativeOwner&&) = delete;
    NativeOwner& operator=(NativeOwner&&) = delete;

    [[nodiscard]] OwnerStart start(const std::filesystem::path& absolute_runtime_path) noexcept;
    void on_message(const SKSE::MessagingInterface::Message& message) noexcept;

    RE::BSEventNotifyControl ProcessEvent(const SKSE::ActionEvent* event,
        [[maybe_unused]] RE::BSTEventSource<SKSE::ActionEvent>* event_source) override;
    RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event,
        [[maybe_unused]] RE::BSTEventSource<RE::TESHitEvent>* event_source) override;
    RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event,
        [[maybe_unused]] RE::BSTEventSource<RE::TESEquipEvent>* event_source) override;
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
        [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* event_source) override;
    RE::BSEventNotifyControl ProcessEvent(const RE::BGSFootstepEvent* event,
        [[maybe_unused]] RE::BSTEventSource<RE::BGSFootstepEvent>* event_source) override;
    RE::BSEventNotifyControl ProcessEvent(const RE::TESPlayerBowShotEvent* event,
        [[maybe_unused]] RE::BSTEventSource<RE::TESPlayerBowShotEvent>* event_source) override;

private:
    static void fallback_diagnostic(const char* message) noexcept;
    static void message_callback(SKSE::MessagingInterface::Message* message);
    static void scheduled_task(NativeOwner* owner, NativeTicket ticket) noexcept;

    void notify_output() noexcept;
    void notify_urgent() noexcept;
    void worker_main() noexcept;
    void run_native_task(NativeTicket ticket);
    void complete_task() noexcept;
    void install_sinks();
    void install_footstep_sink();
    void enter_inert() noexcept;
    [[nodiscard]] bool update_menu_state(const Observation& observation,
        std::uint64_t read_menu_revision) noexcept;
    [[nodiscard]] static bool foreground_is_game() noexcept;

    inputnext::adapters::RuntimeOutput output_;
    Coordinator coordinator_;
    const inputnext::adapters::LedSettings led_settings_;
    const SKSE::TaskInterface* task_interface_{};
    const SKSE::MessagingInterface* messaging_interface_{};

    std::mutex schedule_mutex_;
    std::condition_variable schedule_cv_;
    TaskSchedule schedule_;
    std::thread worker_;
    std::atomic<bool> stopping_;
    std::atomic<bool> inert_;
    std::atomic<bool> callbacks_escaped_;
    std::atomic<bool> focused_;
    std::atomic<bool> install_pending_;
    std::atomic<bool> ticket_admitted_;

    std::mutex menu_mutex_;
    std::uint64_t menu_revision_{};
    bool menu_suppressed_{};

    bool action_source_attempted_{};
    bool action_registered_{};
    bool hit_registered_{};
    bool equip_registered_{};
    bool bow_shot_registered_{};
    bool menu_registered_{};
    bool footstep_registered_{};
};
} // namespace skyrim_adapter::native
