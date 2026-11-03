#pragma once

#include "native/animation_subscription.hpp"
#include "native/bindings.hpp"
#include "native/observation.hpp"
#include "native/owner_pipeline.hpp"
#include "native/owner_services.hpp"

#include <fallout4_adapter/config.hpp>
#include <inputnext_adapters/output.hpp>

#include <F4SE/Interfaces.h>
#include <RE/B/BSAnimationGraphEvent.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/T/TESHitEvent.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>

namespace fallout4_adapter::native {
enum class OwnerPhaseAdmission : std::uint8_t;
enum class OwnerStart : std::uint8_t { failed_before_callbacks, active, retained_inert };

class NativeOwner final : public RE::BSTEventSink<WeaponFiredEvent>,
                          public RE::BSTEventSink<RE::TESHitEvent>,
                          public RE::BSTEventSink<RE::BSAnimationGraphEvent>,
                          public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    explicit NativeOwner(Config config,
        NativeOwnerServices* services = nullptr,
        std::uint64_t initial_request_id = 0,
        std::mutex* custom_inbox_mutex = nullptr) noexcept;
    ~NativeOwner() override;

    NativeOwner(const NativeOwner&) = delete;
    NativeOwner& operator=(const NativeOwner&) = delete;
    NativeOwner(NativeOwner&&) = delete;
    NativeOwner& operator=(NativeOwner&&) = delete;

    [[nodiscard]] OwnerStart start(const std::filesystem::path& absolute_runtime_path) noexcept;

    RE::BSEventNotifyControl ProcessEvent(const WeaponFiredEvent& event,
        RE::BSTEventSource<WeaponFiredEvent>* event_source) override;
    RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent& event,
        RE::BSTEventSource<RE::TESHitEvent>* event_source) override;
    RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent& event,
        RE::BSTEventSource<RE::BSAnimationGraphEvent>* event_source) override;
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>* event_source) override;

private:
    friend struct NativeOwnerTestAccess;

    [[nodiscard]] NativeOwnerPipeline& pipeline() noexcept { return pipeline_; }

    [[nodiscard]] const NativeOwnerPipeline& pipeline() const noexcept { return pipeline_; }

    [[nodiscard]] NativeOwnerServices& services() noexcept { return services_; }

    [[nodiscard]] bool callbacks_escaped() const noexcept {
        return callbacks_escaped_.load(std::memory_order_acquire);
    }

    static void F4SEAPI message_callback(F4SE::MessagingInterface::Message* message);
    static void main_observation_callback() noexcept;

    void on_message(const F4SE::MessagingInterface::Message& message) noexcept;
    void worker_main() noexcept;

    ProductionOwnerServices default_services_;
    NativeOwnerServices& services_;
    std::thread worker_;
    std::atomic<bool> callbacks_escaped_;
    NativeOwnerPipeline pipeline_;
};
} // namespace fallout4_adapter::native
