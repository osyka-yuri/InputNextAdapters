#pragma once

#include "cpp/component/native_ui_capture/ui_capture_test.hpp"
#include "native/dispatch.hpp"
#include "native/dispatch_pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fallout4_adapter::native::native_dispatch_test {

void reset() noexcept;
void set_install_ready() noexcept;
void set_main_message_loop_original(MainMessageLoopOriginal original) noexcept;
bool activate_main_observation(MainObservationCallback callback) noexcept;
void set_active_pipeline(NativeDispatchPipeline* pipeline) noexcept;
[[nodiscard]] NativeDispatchPipeline& active_pipeline() noexcept;
[[nodiscard]] std::uintptr_t main_wrapper_address() noexcept;
void invoke_main(RE::Main* main);
[[nodiscard]] bool callback_active() noexcept;
[[nodiscard]] std::uint32_t fatal_control_flow_count() noexcept;
void record_fatal_control_flow() noexcept;

} // namespace fallout4_adapter::native::native_dispatch_test

namespace fallout4_adapter::tests::dispatch {

inline std::uint64_t ui_transition_generation() noexcept {
    return native::ui_capture_test::generation();
}

inline bool activate_main_observation(native::MainObservationCallback callback) noexcept {
    return native::native_dispatch_test::activate_main_observation(callback);
}

void check(bool condition, std::string_view message);

class ScopedDispatchState final {
public:
    ScopedDispatchState() noexcept;
    ~ScopedDispatchState() noexcept;

    ScopedDispatchState(const ScopedDispatchState&) = delete;
    ScopedDispatchState& operator=(const ScopedDispatchState&) = delete;
    ScopedDispatchState(ScopedDispatchState&&) = delete;
    ScopedDispatchState& operator=(ScopedDispatchState&&) = delete;

private:
    static void reset() noexcept;
};

class ScopedVirtualAllocation final {
public:
    ScopedVirtualAllocation(std::size_t size, unsigned long protection) noexcept;
    ~ScopedVirtualAllocation() noexcept;

    ScopedVirtualAllocation(const ScopedVirtualAllocation&) = delete;
    ScopedVirtualAllocation& operator=(const ScopedVirtualAllocation&) = delete;
    ScopedVirtualAllocation(ScopedVirtualAllocation&&) = delete;
    ScopedVirtualAllocation& operator=(ScopedVirtualAllocation&&) = delete;

    [[nodiscard]] std::byte* get() const noexcept { return allocation_; }

private:
    std::byte* allocation_{};
};

} // namespace fallout4_adapter::tests::dispatch
