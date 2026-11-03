#pragma once

#include "native/observation.hpp"
#include "native/ui_capture.hpp"

#include <fallout4_adapter/session.hpp>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output.hpp>

#include <F4SE/Interfaces.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <thread>

namespace fallout4_adapter::native {
using MainObservationCallback = void (*)() noexcept;

class NativeOwnerServices {
public:
    NativeOwnerServices() = default;
    virtual ~NativeOwnerServices() = default;
    NativeOwnerServices(const NativeOwnerServices&) = delete;
    NativeOwnerServices& operator=(const NativeOwnerServices&) = delete;
    NativeOwnerServices(NativeOwnerServices&&) = delete;
    NativeOwnerServices& operator=(NativeOwnerServices&&) = delete;

    // OutputPort connection
    virtual inputnext::adapters::ConnectResult start_output(
        const std::filesystem::path& runtime_path) = 0;
    virtual inputnext::adapters::OutputPort& output_port() noexcept = 0;

    // F4SE / Win32 startup services
    virtual bool interfaces_available() noexcept = 0;
    virtual bool pin_module(const void* module_anchor) noexcept = 0;
    virtual bool start_worker(std::function<void()> worker_func,
        std::thread& worker_thread) noexcept = 0;
    virtual bool register_listener(F4SE::MessagingInterface::EventCallback callback) noexcept = 0;
    virtual bool activate_main_observation(MainObservationCallback callback) noexcept = 0;

    // Native observations & game state
    virtual std::uint64_t current_ui_generation() noexcept = 0;
    virtual NativeObservation
    sample_observation(MonoTime sampled_at, bool focused, std::uint64_t ui_generation) noexcept = 0;
    virtual std::uintptr_t current_player_address() noexcept = 0;
    virtual bool foreground_is_game() noexcept = 0;
    virtual OwnerPhaseAdmission admit_owner_phase(std::uint64_t expected_generation) noexcept = 0;

    // Diagnostics & logging
    virtual void log_error(const char* message) noexcept = 0;
    virtual void log_info(const char* message) noexcept = 0;
};

class ProductionOwnerServices final : public NativeOwnerServices {
public:
    explicit ProductionOwnerServices(inputnext::adapters::LedSettings led_settings = {}) noexcept;
    ~ProductionOwnerServices() override;

    ProductionOwnerServices(const ProductionOwnerServices&) = delete;
    ProductionOwnerServices& operator=(const ProductionOwnerServices&) = delete;
    ProductionOwnerServices(ProductionOwnerServices&&) = delete;
    ProductionOwnerServices& operator=(ProductionOwnerServices&&) = delete;

    inputnext::adapters::ConnectResult start_output(
        const std::filesystem::path& runtime_path) override;
    inputnext::adapters::OutputPort& output_port() noexcept override;

    bool interfaces_available() noexcept override;
    bool pin_module(const void* module_anchor) noexcept override;
    bool start_worker(std::function<void()> worker_func,
        std::thread& worker_thread) noexcept override;
    bool register_listener(F4SE::MessagingInterface::EventCallback callback) noexcept override;
    bool activate_main_observation(MainObservationCallback callback) noexcept override;

    std::uint64_t current_ui_generation() noexcept override;
    NativeObservation sample_observation(MonoTime sampled_at,
        bool focused,
        std::uint64_t ui_generation) noexcept override;
    std::uintptr_t current_player_address() noexcept override;
    bool foreground_is_game() noexcept override;
    OwnerPhaseAdmission admit_owner_phase(std::uint64_t expected_generation) noexcept override;

    void log_error(const char* message) noexcept override;
    void log_info(const char* message) noexcept override;

private:
    const inputnext::adapters::LedSettings led_settings_;
    inputnext::adapters::RuntimeOutput output_{};
    const F4SE::MessagingInterface* messaging_interface_{nullptr};
};
} // namespace fallout4_adapter::native
