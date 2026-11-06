#include "owner.hpp"

#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/native/config.hpp>

#include <SKSE/API.h>
#include <SKSE/Logger.h>

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>

namespace skyrim_adapter::native {
namespace {
std::atomic<NativeOwner*> active_owner{};
char module_anchor{};

} // namespace

void NativeOwner::fallback_diagnostic(const char* message) noexcept {
    OutputDebugStringA(message);
}

NativeOwner::NativeOwner(NativeConfig config) noexcept
    : coordinator_(output_,
          CoordinatorConfig{config.slot},
          PolicyConfig{
              .trigger_scale = config.trigger_scale,
              .rumble_scale = config.rumble_scale,
              .effects = config.effects,
          }),
      led_settings_(config.effects.leds),
      schedule_(std::chrono::steady_clock::now()) {
}

NativeOwner::~NativeOwner() {
    stopping_.store(true, std::memory_order_release);
    schedule_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

OwnerStart NativeOwner::start(const std::filesystem::path& absolute_runtime_path) noexcept {
    try {
        if (!absolute_runtime_path.is_absolute()) {
            SKSE::log::error("Runtime DLL path is not absolute; plugin startup rejected.");
            return OwnerStart::failed_before_callbacks;
        }

        const auto connected = output_.start(absolute_runtime_path);
        if (!connected) {
            SKSE::log::error("Runtime client connection failed (error {}, native {}).",
                static_cast<unsigned>(connected.error),
                connected.native_error);
            return OwnerStart::failed_before_callbacks;
        }

        task_interface_ = SKSE::GetTaskInterface();
        messaging_interface_ = SKSE::GetMessagingInterface();
        if (!task_interface_ || task_interface_->Version() < SKSE::TaskInterface::kVersion ||
            !messaging_interface_ ||
            messaging_interface_->Version() < SKSE::MessagingInterface::kVersion) {
            SKSE::log::error(
                "Required SKSE task or messaging interface is unavailable or too old.");
            return OwnerStart::failed_before_callbacks;
        }

        focused_.store(true, std::memory_order_relaxed);
        worker_ = std::thread([this] { worker_main(); });

        HMODULE module{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&module_anchor),
                &module)) {
            SKSE::log::error(
                "Unable to pin the plugin module before callback registration (Win32 {}).",
                GetLastError());
            stopping_.store(true, std::memory_order_release);
            schedule_cv_.notify_all();
            return OwnerStart::failed_before_callbacks;
        }

        active_owner.store(this, std::memory_order_release);
        callbacks_escaped_.store(true, std::memory_order_release);
        if (!messaging_interface_->RegisterListener(&NativeOwner::message_callback)) {
            try {
                SKSE::log::error(
                    "SKSE message listener registration failed; retaining pinned owner inert.");
            } catch (...) {
                fallback_diagnostic("InputNext Skyrim: SKSE message listener registration failed; "
                                    "owner retained inert.\n");
            }
            enter_inert();
            return OwnerStart::retained_inert;
        }

        try {
            SKSE::log::info("Native owner started; runtime output is connected and callbacks are "
                            "lifetime-pinned.");
        } catch (...) {
            fallback_diagnostic(
                "InputNext Skyrim: native owner started; informational log write failed.\n");
        }
        return OwnerStart::active;
    } catch (...) {
        if (callbacks_escaped_.load(std::memory_order_acquire)) {
            enter_inert();
            return OwnerStart::retained_inert;
        }
        stopping_.store(true, std::memory_order_release);
        schedule_cv_.notify_all();
        return OwnerStart::failed_before_callbacks;
    }
}

void NativeOwner::message_callback(SKSE::MessagingInterface::Message* message) {
    auto* owner = active_owner.load(std::memory_order_acquire);
    if (owner && message) {
        owner->on_message(*message);
    }
}

void NativeOwner::enter_inert() noexcept {
    if (inert_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    coordinator_.fault();
    stopping_.store(true, std::memory_order_release);
    schedule_cv_.notify_all();
}
} // namespace skyrim_adapter::native
