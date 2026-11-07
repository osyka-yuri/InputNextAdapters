#include "owner.hpp"
#include "diagnostics.hpp"
#include "skse_sdk.hpp"
#include "translators.hpp"

#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/native/config.hpp>

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>

namespace skyrim_adapter::native {
namespace {
std::atomic<NativeOwner*> active_owner{};

char module_anchor{};

} // namespace

NativeOwner::NativeOwner(NativeConfig config) noexcept
    : coordinator_(output_,
          CoordinatorConfig{.slot = config.slot},
          PolicyConfig{
              .trigger_scale = config.trigger_scale,
              .rumble_scale = config.rumble_scale,
              .effects = config.effects,
          }),
      schedule_(std::chrono::steady_clock::now()) {
}

NativeOwner::~NativeOwner() {
    stopping_.store(true, std::memory_order_release);
    schedule_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

OwnerStart NativeOwner::start(const std::filesystem::path& absolute_runtime_path,
    PluginHandle plugin_handle,
    const SKSETaskInterface& task_interface,
    const SKSEMessagingInterface& messaging_interface) noexcept {
    try {
        if (!absolute_runtime_path.is_absolute() ||
            plugin_handle == static_cast<PluginHandle>(kPluginHandle_Invalid) ||
            task_interface.interfaceVersion < SKSETaskInterface::kInterfaceVersion ||
            !task_interface.AddTask ||
            messaging_interface.interfaceVersion < SKSEMessagingInterface::kInterfaceVersion ||
            !messaging_interface.RegisterListener || !messaging_interface.GetEventDispatcher) {
            diagnostics::write("native_start=failed reason=invalid SKSE interface or runtime path");
            return OwnerStart::failed_before_callbacks;
        }

        const auto connected = output_.start(absolute_runtime_path);
        if (!connected) {
            diagnostics::write("runtime_api=bind_failed");
            OutputDebugStringW(
                L"InputNext Skyrim LE: runtime connection failed; startup rejected.\n");
            return OwnerStart::failed_before_callbacks;
        }
        diagnostics::write("runtime_api=bound");

        task_interface_ = &task_interface;
        messaging_interface_ = &messaging_interface;
        plugin_handle_ = plugin_handle;
        focused_.store(true, std::memory_order_relaxed);
        worker_ = std::thread([this] { worker_main(); });

        HMODULE module{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&module_anchor),
                &module)) {
            diagnostics::write("native_start=failed reason=unable to pin plugin module");
            OutputDebugStringW(L"InputNext Skyrim LE: unable to pin the plugin module.\n");
            stopping_.store(true, std::memory_order_release);
            schedule_cv_.notify_all();
            return OwnerStart::failed_before_callbacks;
        }

        active_owner.store(this, std::memory_order_release);
        callbacks_escaped_.store(true, std::memory_order_release);
        if (!messaging_interface_->RegisterListener(plugin_handle_,
                "SKSE",
                &NativeOwner::message_callback)) {
            diagnostics::write("skse_listener=registration_failed");
            OutputDebugStringW(
                L"InputNext Skyrim LE: SKSE listener registration failed; owner retained inert.\n");
            enter_inert("SKSE listener registration failed");
            return OwnerStart::retained_inert;
        }

        diagnostics::write("skse_listener=registered");
        diagnostics::write("native_owner=active");
        OutputDebugStringW(L"InputNext Skyrim LE: native owner started.\n");
        return OwnerStart::active;
    } catch (...) {
        if (callbacks_escaped_.load(std::memory_order_acquire)) {
            enter_inert("startup exception after callbacks were registered");
            return OwnerStart::retained_inert;
        }
        diagnostics::write("native_start=failed reason=startup exception");
        stopping_.store(true, std::memory_order_release);
        schedule_cv_.notify_all();
        return OwnerStart::failed_before_callbacks;
    }
}

void NativeOwner::message_callback(SKSEMessagingInterface::Message* message) {
    auto* owner = active_owner.load(std::memory_order_acquire);
    if (owner && message) {
        owner->on_message(*message);
    }
}

void NativeOwner::on_message(const SKSEMessagingInterface::Message& message) noexcept {
    if (inert_.load(std::memory_order_acquire)) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    switch (message.type) {
    case SKSEMessagingInterface::kMessage_DataLoaded:
        diagnostics::write("skse_message=data_loaded");
        coordinator_.control(Control::data_loaded, now);
        ticket_admitted_.store(true, std::memory_order_release);
        notify_urgent();
        break;
    case SKSEMessagingInterface::kMessage_PreLoadGame: {
        {
            std::scoped_lock lock(menu_mutex_);
            coordinator_.control(Control::pre_load, now);
            menu_snapshot_ = {};
            ++menu_revision_;
        }
        diagnostics::write("skse_message=pre_load");
        notify_urgent();
        break;
    }
    case SKSEMessagingInterface::kMessage_PostLoadGame: {
        const auto encoded = reinterpret_cast<std::uintptr_t>(message.data);
        const bool succeeded = detail::post_load_succeeded(message.dataLen, encoded);
        diagnostics::write("skse_message=post_load data_len=",
            static_cast<unsigned>(message.dataLen),
            " encoded_success=",
            succeeded ? 1U : 0U);
        coordinator_.control(succeeded ? Control::load_succeeded : Control::load_failed, now);
        notify_urgent();
        break;
    }
    case SKSEMessagingInterface::kMessage_NewGame: {
        {
            std::scoped_lock lock(menu_mutex_);
            coordinator_.control(Control::new_game, now);
            menu_snapshot_ = {};
            ++menu_revision_;
        }
        diagnostics::write("skse_message=new_game");
        notify_urgent();
        break;
    }
    default:
        break;
    }
}

void NativeOwner::enter_inert(const char* reason) noexcept {
    if (inert_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    diagnostics::write("native_owner=inert reason=", reason ? reason : "unknown");
    coordinator_.fault();
    stopping_.store(true, std::memory_order_release);
    schedule_cv_.notify_all();
}

} // namespace skyrim_adapter::native
