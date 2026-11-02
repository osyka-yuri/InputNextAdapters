#pragma once

#include "native/dispatch.hpp"

#include <atomic>
#include <cstdint>

namespace RE {
class Main;
}

namespace fallout4_adapter::native {

[[noreturn]] void fatal_unexpected_dispatch() noexcept;

class NativeDispatchPipeline final {
public:
    constexpr NativeDispatchPipeline() noexcept = default;

    [[nodiscard]] NativeDispatchInstall status() const noexcept {
        return install_result_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_main_observation_active() const noexcept {
        return main_observation_.load(std::memory_order_acquire) != nullptr;
    }

    [[nodiscard]] bool publish_main_original(MainMessageLoopOriginal original) noexcept {
        MainMessageLoopOriginal expected{};
        return main_message_loop_original_.compare_exchange_strong(expected,
            original,
            std::memory_order_acq_rel);
    }

    [[nodiscard]] bool activate_main_observation(MainObservationCallback callback) noexcept {
        if (!callback ||
            install_result_.load(std::memory_order_acquire) != NativeDispatchInstall::ready) {
            return false;
        }
        MainObservationCallback expected{};
        return main_observation_.compare_exchange_strong(expected,
            callback,
            std::memory_order_acq_rel);
    }

    template <typename Operations>
    [[nodiscard]] NativeDispatchInstall install(Operations& operations) noexcept {
        bool expected = false;
        if (!install_started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return install_result_.load(std::memory_order_acquire);
        }
        const auto result = perform_native_dispatch_install(operations);
        install_result_.store(result, std::memory_order_release);
        return result;
    }

    void invoke_main(RE::Main* main) const {
        const auto original = main_message_loop_original_.load(std::memory_order_acquire);
        if (!original) {
            fatal_unexpected_dispatch();
        }
        original(main);
        if (const auto callback = main_observation_.load(std::memory_order_acquire)) {
            callback();
        }
    }

private:
    std::atomic<NativeDispatchInstall> install_result_{
        NativeDispatchInstall::unavailable_before_attempt};
    std::atomic<bool> install_started_;
    std::atomic<MainMessageLoopOriginal> main_message_loop_original_;
    std::atomic<MainObservationCallback> main_observation_;
};

[[nodiscard]] NativeDispatchPipeline& dispatch_pipeline() noexcept;

} // namespace fallout4_adapter::native
