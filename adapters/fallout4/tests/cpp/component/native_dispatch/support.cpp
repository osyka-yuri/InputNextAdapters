#include "support.hpp"
#include "cpp/component/native_ui_capture/ui_capture_test.hpp"
#include "native/dispatch.hpp"
#include "native/dispatch_pipeline.hpp"

#include <RE/M/Main.h>

#include <Windows.h>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#undef FAIL

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace fallout4_adapter::native::native_dispatch_test {

namespace {
std::atomic<std::uint32_t> g_fatal_control_flow_count{};
std::atomic<MainMessageLoopOriginal> g_test_original_target{nullptr};
std::atomic<NativeDispatchPipeline*> g_test_active_pipeline{nullptr};
std::unique_ptr<NativeDispatchPipeline> g_test_default_pipeline{};

void dispatch_test_main_trampoline(RE::Main* main) {
    if (const auto target = g_test_original_target.load(std::memory_order_acquire)) {
        target(main);
    }
}

struct TestReadyOperations {
    static bool preflight() noexcept { return true; }

    static bool prepare() noexcept { return true; }

    static bool pin_and_publish() noexcept { return true; }

    static bool precommit() noexcept { return true; }

    static NativeDispatchAttemptInspection attempt_once([[maybe_unused]] NativeDispatchSite site,
        [[maybe_unused]] std::uint8_t attempt) noexcept {
        return {
            .all_sites_safe = true,
            .expected_prefix = true,
            .write_succeeded = true,
            .instruction_cache_flushed = true,
        };
    }

    void retain_disabled() noexcept {}

    void fatal_control_flow() noexcept {}
};

void test_main_message_loop_wrapper(RE::Main* main) {
    active_pipeline().invoke_main(main);
}
} // namespace

NativeDispatchPipeline& active_pipeline() noexcept {
    if (auto* pipeline = g_test_active_pipeline.load(std::memory_order_acquire)) {
        return *pipeline;
    }
    if (!g_test_default_pipeline) {
        g_test_default_pipeline = std::make_unique<NativeDispatchPipeline>();
    }
    return *g_test_default_pipeline;
}

void reset() noexcept {
    g_fatal_control_flow_count.store(0, std::memory_order_release);
    g_test_original_target.store(nullptr, std::memory_order_release);
    g_test_active_pipeline.store(nullptr, std::memory_order_release);
    g_test_default_pipeline = std::make_unique<NativeDispatchPipeline>();
    ui_capture_test::clear_originals();
}

void set_install_ready() noexcept {
    TestReadyOperations operations{};
    static_cast<void>(active_pipeline().install(operations));
}

void set_main_message_loop_original(MainMessageLoopOriginal original) noexcept {
    g_test_original_target.store(original, std::memory_order_release);
    static_cast<void>(active_pipeline().publish_main_original(&dispatch_test_main_trampoline));
}

bool activate_main_observation(MainObservationCallback callback) noexcept {
    return active_pipeline().activate_main_observation(callback);
}

void set_active_pipeline(NativeDispatchPipeline* pipeline) noexcept {
    g_test_active_pipeline.store(pipeline, std::memory_order_release);
}

std::uintptr_t main_wrapper_address() noexcept {
    return reinterpret_cast<std::uintptr_t>(&test_main_message_loop_wrapper);
}

void invoke_main(RE::Main* main) {
    active_pipeline().invoke_main(main);
}

bool callback_active() noexcept {
    return active_pipeline().is_main_observation_active();
}

std::uint32_t fatal_control_flow_count() noexcept {
    return g_fatal_control_flow_count.load(std::memory_order_acquire);
}

void record_fatal_control_flow() noexcept {
    g_fatal_control_flow_count.fetch_add(1, std::memory_order_relaxed);
}

} // namespace fallout4_adapter::native::native_dispatch_test

namespace fallout4_adapter::tests::dispatch {

void check(bool condition, std::string_view message) {
    INFO(message);
    CHECK(condition);
}

ScopedDispatchState::ScopedDispatchState() noexcept {
    reset();
}

ScopedDispatchState::~ScopedDispatchState() noexcept {
    reset();
}

void ScopedDispatchState::reset() noexcept {
    native::native_dispatch_test::reset();
    native::ui_capture_test::reset(1);
}

ScopedVirtualAllocation::ScopedVirtualAllocation(std::size_t size,
    unsigned long protection) noexcept
    : allocation_(static_cast<std::byte*>(
          VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, protection))) {
}

ScopedVirtualAllocation::~ScopedVirtualAllocation() noexcept {
    if (allocation_) {
        (void)VirtualFree(allocation_, 0, MEM_RELEASE);
    }
}

} // namespace fallout4_adapter::tests::dispatch
