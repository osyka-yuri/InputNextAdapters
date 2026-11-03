#include "native/owner_services.hpp"
#include "native/dispatch.hpp"
#include "native/observation.hpp"
#include "native/ui_capture.hpp"

#include <fallout4_adapter/model.hpp>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output.hpp>
#include <inputnext_adapters/runtime_client.hpp>

#include <F4SE/API.h>
#include <RE/P/PlayerCharacter.h>
#include <REX/LOG.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <thread>
#include <utility>

#include <Windows.h>
#ifdef ERROR
#    undef ERROR
#endif

namespace fallout4_adapter::native {
namespace {
void log_fallback(const char* message) noexcept {
    OutputDebugStringA(message);
}
} // namespace

ProductionOwnerServices::ProductionOwnerServices(
    inputnext::adapters::LedSettings led_settings) noexcept
    : led_settings_(led_settings) {
}

ProductionOwnerServices::~ProductionOwnerServices() = default;

inputnext::adapters::ConnectResult ProductionOwnerServices::start_output(
    const std::filesystem::path& runtime_path) {
    return output_.start(runtime_path);
}

inputnext::adapters::OutputPort& ProductionOwnerServices::output_port() noexcept {
    return output_;
}

bool ProductionOwnerServices::interfaces_available() noexcept {
    messaging_interface_ = F4SE::GetMessagingInterface();
    return messaging_interface_ != nullptr &&
           messaging_interface_->Version() == F4SE::MessagingInterface::kVersion;
}

bool ProductionOwnerServices::pin_module(const void* anchor) noexcept {
    HMODULE module{};
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_PIN,
               reinterpret_cast<LPCWSTR>(anchor),
               &module) != FALSE;
}

bool ProductionOwnerServices::start_worker(std::function<void()> worker_func,
    std::thread& worker_thread) noexcept {
    try {
        worker_thread = std::thread(std::move(worker_func));
        return true;
    } catch (...) {
        return false;
    }
}

bool ProductionOwnerServices::register_listener(
    F4SE::MessagingInterface::EventCallback callback) noexcept {
    return messaging_interface_ != nullptr && messaging_interface_->RegisterListener(callback);
}

bool ProductionOwnerServices::activate_main_observation(MainObservationCallback callback) noexcept {
    return fallout4_adapter::native::activate_main_observation(callback);
}

std::uint64_t ProductionOwnerServices::current_ui_generation() noexcept {
    return ui_transition_generation();
}

NativeObservation ProductionOwnerServices::sample_observation(MonoTime sampled_at,
    bool /*focused*/,
    std::uint64_t ui_generation) noexcept {
    return read_observation(sampled_at, true, ui_generation, led_settings_);
}

std::uintptr_t ProductionOwnerServices::current_player_address() noexcept {
    const auto* player = RE::PlayerCharacter::GetSingleton();
    return reinterpret_cast<std::uintptr_t>(player);
}

OwnerPhaseAdmission ProductionOwnerServices::admit_owner_phase(
    std::uint64_t expected_generation) noexcept {
    return owner_phase_admission(expected_generation);
}

bool ProductionOwnerServices::foreground_is_game() noexcept {
    auto* const foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }
    DWORD process_id{};
    if (GetWindowThreadProcessId(foreground, &process_id) == 0 || process_id == 0) {
        return false;
    }
    return process_id == GetCurrentProcessId();
}

void ProductionOwnerServices::log_error(const char* message) noexcept {
    try {
        REX::ERROR("{}", message);
    } catch (...) {
        log_fallback(message);
        log_fallback("\n");
    }
}

void ProductionOwnerServices::log_info(const char* message) noexcept {
    try {
        REX::INFO("{}", message);
    } catch (...) {
        log_fallback(message);
        log_fallback("\n");
    }
}
} // namespace fallout4_adapter::native
