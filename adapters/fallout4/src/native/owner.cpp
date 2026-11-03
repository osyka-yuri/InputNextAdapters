#include "owner.hpp"
#include "native/owner_services.hpp"

#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/session.hpp>

#include <F4SE/API.h>
#include <F4SE/Interfaces.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>

namespace fallout4_adapter::native {
namespace {
constexpr auto worker_interval = std::chrono::milliseconds{20};
std::atomic<NativeOwner*> active_owner{};
char module_anchor{};

[[nodiscard]] constexpr bool f4se_bool_payload(std::uintptr_t value,
    std::uint32_t length) noexcept {
    return length == 1 && value == 1;
}

static_assert(f4se_bool_payload(1, 1));
static_assert(!f4se_bool_payload(0, 1));
static_assert(!f4se_bool_payload(0x1000, 1));
static_assert(!f4se_bool_payload(1, 0));
} // namespace

NativeOwner::NativeOwner(Config config,
    NativeOwnerServices* services,
    std::uint64_t initial_request_id,
    std::mutex* custom_inbox_mutex) noexcept
    : default_services_(config.policy.effects.leds),
      services_(services ? *services : default_services_),
      pipeline_(services_, config, this, this, this, this, initial_request_id, custom_inbox_mutex) {
}

NativeOwner::~NativeOwner() {
    pipeline_.request_stop();
    if (worker_.joinable()) {
        worker_.join();
    }
    auto* expected = this;
    static_cast<void>(
        active_owner.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel));
}

OwnerStart NativeOwner::start(const std::filesystem::path& absolute_runtime_path) noexcept {
    try {
        if (!absolute_runtime_path.is_absolute()) {
            services_.log_error(
                "InputNext Fallout 4 runtime DLL path is not absolute; startup rejected.");
            return OwnerStart::failed_before_callbacks;
        }

        const auto connected = services_.start_output(absolute_runtime_path);
        if (!connected) {
            services_.log_error("InputNext Fallout 4 runtime connection failed.");
            return OwnerStart::failed_before_callbacks;
        }

        if (!services_.interfaces_available()) {
            services_.log_error("Required F4SE messaging interface is unavailable or too old.");
            return OwnerStart::failed_before_callbacks;
        }

        NativeOwner* expected = nullptr;
        if (!active_owner.compare_exchange_strong(expected, this, std::memory_order_acq_rel)) {
            services_.log_error(
                "A Fallout 4 native owner is already active; duplicate startup rejected.");
            return OwnerStart::failed_before_callbacks;
        }

        if (!services_.pin_module(&module_anchor)) {
            expected = this;
            static_cast<void>(
                active_owner.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel));
            services_.log_error(
                "Unable to pin the Fallout 4 plugin module before callback registration.");
            return OwnerStart::failed_before_callbacks;
        }

        const bool worker_started = services_.start_worker([this] { worker_main(); }, worker_);
        if (!worker_started) {
            expected = this;
            static_cast<void>(
                active_owner.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel));
            services_.log_error("Unable to start the Fallout 4 output worker.");
            return OwnerStart::failed_before_callbacks;
        }

        callbacks_escaped_.store(true, std::memory_order_release);
        if (!services_.activate_main_observation(&NativeOwner::main_observation_callback)) {
            pipeline_.enter_inert(
                "Main observation dispatch activation failed; the pinned owner remains inert.");
            return OwnerStart::retained_inert;
        }

        if (!services_.register_listener(&NativeOwner::message_callback)) {
            pipeline_.enter_inert(
                "F4SE message listener registration failed; the pinned owner remains inert.");
            return OwnerStart::retained_inert;
        }

        services_.log_info("Fallout 4 native owner started; runtime output is connected and "
                           "callbacks are lifetime-pinned.");
        return OwnerStart::active;
    } catch (...) {
        if (callbacks_escaped_.load(std::memory_order_acquire)) {
            pipeline_.enter_inert("Unexpected startup failure after callback lifetime was pinned.");
            return OwnerStart::retained_inert;
        }
        services_.log_error("Unexpected startup failure before callback lifetime was pinned.");
        NativeOwner* expected = this;
        static_cast<void>(
            active_owner.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel));
        pipeline_.request_stop();
        return OwnerStart::failed_before_callbacks;
    }
}

void F4SEAPI NativeOwner::message_callback(F4SE::MessagingInterface::Message* message) {
    auto* owner = active_owner.load(std::memory_order_acquire);
    if (owner && message && !owner->pipeline().inert()) {
        owner->on_message(*message);
    }
}

void NativeOwner::main_observation_callback() noexcept {
    if (auto* owner = active_owner.load(std::memory_order_acquire)) {
        owner->pipeline().consume_main_observation();
    }
}

void NativeOwner::on_message(const F4SE::MessagingInterface::Message& message) noexcept {
    const auto now = std::chrono::steady_clock::now();
    switch (message.type) {
    case F4SE::MessagingInterface::kGameDataReady: {
        const auto payload = reinterpret_cast<std::uintptr_t>(message.data);
        if (f4se_bool_payload(payload, message.dataLen)) {
            pipeline_.set_data_ready(true);
            pipeline_.set_install_pending(true);
            pipeline_.queue_control(Control::data_ready, now);
        } else {
            pipeline_.set_data_ready(false);
            pipeline_.set_install_pending(false);
            pipeline_.queue_control(Control::pre_load, now);
        }
        break;
    }
    case F4SE::MessagingInterface::kPreLoadGame:
        pipeline_.queue_control(Control::pre_load, now);
        break;
    case F4SE::MessagingInterface::kPostLoadGame: {
        const auto result = reinterpret_cast<std::uintptr_t>(message.data);
        const bool loaded = f4se_bool_payload(result, message.dataLen);
        pipeline_.queue_control(loaded ? Control::load_succeeded : Control::load_failed, now);
        break;
    }
    case F4SE::MessagingInterface::kNewGame:
        pipeline_.queue_control(Control::new_game, now);
        break;
    case F4SE::MessagingInterface::kGameLoaded:
    default:
        break;
    }
}

void NativeOwner::worker_main() noexcept {
    auto next_sample = std::chrono::steady_clock::now();
    bool event_overflow_reported = false;

    while (!pipeline_.stopping()) {
        const auto now = std::chrono::steady_clock::now();
        const bool focused = services_.foreground_is_game();
        if (!pipeline_.run_worker_iteration(now, focused, next_sample, event_overflow_reported) ||
            pipeline_.stopping()) {
            break;
        }

        pipeline_.wait_for_work(now + worker_interval);
    }

    pipeline_.finish_worker();
}
} // namespace fallout4_adapter::native
