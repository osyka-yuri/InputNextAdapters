#include "cpp/component/native_ui_capture/ui_capture_test.hpp"
#include "native/dispatch.hpp"
#include "native/ui_capture.hpp"
#include "support.hpp"

#include <RE/B/BSFixedString.h>
#include <RE/C/ControlMap.h>
#include <RE/I/INPUT_DEVICE.h>

#include <Windows.h>

#include <catch2/catch_test_macros.hpp>
#undef FAIL

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace fallout4_adapter::tests::dispatch {
using namespace fallout4_adapter::native;

namespace {
const RE::BSFixedString& empty_remap_event_argument() {
    // This empty fixture avoids the game-only string-pool release relocation at shutdown.
    alignas(RE::BSFixedString) static std::array<std::byte, sizeof(RE::BSFixedString)> storage{};
    static const auto* value =
        std::construct_at(reinterpret_cast<RE::BSFixedString*>(storage.data()));
    return *value;
}

[[maybe_unused]] RE::ControlMap* fixture_control_map_address() noexcept {
    static std::byte storage{};
    return reinterpret_cast<RE::ControlMap*>(std::addressof(storage));
}

RE::Main* fixture_main_address() noexcept {
    static std::byte storage{};
    return reinterpret_cast<RE::Main*>(std::addressof(storage));
}

std::atomic<std::uint32_t> main_order{};
std::atomic<std::uint32_t> callback_count{};
std::atomic<std::uint32_t> original_count{};
std::atomic<RE::Main*> original_main{};
std::atomic<std::uint32_t> callback_seen_original_calls{};

void observation_callback() noexcept {
    auto expected = 1U;
    static_cast<void>(main_order.compare_exchange_strong(expected, 2U, std::memory_order_acq_rel));
    callback_seen_original_calls.store(original_count.load(std::memory_order_acquire),
        std::memory_order_release);
    callback_count.fetch_add(1, std::memory_order_relaxed);
}

void controlled_main_original(RE::Main* main) {
    original_main.store(main, std::memory_order_relaxed);
    original_count.fetch_add(1, std::memory_order_release);
    main_order.store(1, std::memory_order_release);
}

static_assert(std::is_same_v<decltype(&controlled_main_original), MainMessageLoopOriginal>);

void throwing_main_original(RE::Main* main) {
    original_main.store(main, std::memory_order_relaxed);
    original_count.fetch_add(1, std::memory_order_release);
    throw std::runtime_error{"message-pump fixture unwind"};
}

static_assert(std::is_same_v<decltype(&throwing_main_original), MainMessageLoopOriginal>);

class ScopedGatewayCallbackState final {
public:
    ScopedGatewayCallbackState() noexcept = default;

    ~ScopedGatewayCallbackState() noexcept {
        native_dispatch_test::reset();
        ui_capture_test::reset(1);
        reset();
    }

    ScopedGatewayCallbackState(const ScopedGatewayCallbackState&) = delete;
    ScopedGatewayCallbackState& operator=(const ScopedGatewayCallbackState&) = delete;
    ScopedGatewayCallbackState(ScopedGatewayCallbackState&&) = delete;
    ScopedGatewayCallbackState& operator=(ScopedGatewayCallbackState&&) = delete;

private:
    static void reset() noexcept {
        main_order.store(0, std::memory_order_relaxed);
        callback_count.store(0, std::memory_order_relaxed);
        original_count.store(0, std::memory_order_relaxed);
        original_main.store(nullptr, std::memory_order_relaxed);
        callback_seen_original_calls.store(0, std::memory_order_relaxed);
    }
};
} // namespace

TEST_CASE("fallout4.native_dispatch.production_main_relay_remap_gateway",
    "[component][fallout4][native_dispatch]") {
    ScopedDispatchState const dispatch_state;
    constexpr std::size_t page_size = 4096;
    ScopedVirtualAllocation const page_region{page_size, PAGE_EXECUTE_READWRITE};
    ScopedGatewayCallbackState const callback_state;
    auto* page = page_region.get();
    check(page != nullptr, "host fixture allocates an executable page for production relays");
    if (!page) {
        return;
    }

    const auto address = reinterpret_cast<std::uintptr_t>(page);
    NativeDispatchLayout layout{};
    check(emit_native_dispatch_resources(std::span<std::byte>{page, native_dispatch_pool_size},
              address,
              NativeDispatchTargets{
                  .helper_interceptor = ui_capture_test::interceptor_address(),
                  .helper_continuation = address + 512U,
                  .main_wrapper = native_dispatch_test::main_wrapper_address(),
                  .remap_interceptor = reinterpret_cast<std::uintptr_t>(&remap_button_interceptor),
                  .remap_continuation = address + 1536U,
              },
              layout),
        "production emitter builds executable Main and public remap paths");
    if (layout.bytes_used == 0 ||
        !FlushInstructionCache(GetCurrentProcess(), page, native_dispatch_pool_size)) {
        check(false, "host fixture flushes emitted production relay instructions");
        return;
    }

    native_dispatch_test::reset();
    ui_capture_test::reset(9);
    native_dispatch_test::set_main_message_loop_original(&controlled_main_original);
    callback_count.store(0, std::memory_order_relaxed);
    original_count.store(0, std::memory_order_relaxed);
    main_order.store(0, std::memory_order_relaxed);
    callback_seen_original_calls.store(0, std::memory_order_release);

    using MainRelay = void (*)(RE::Main*);
    auto* main_relay = reinterpret_cast<MainRelay>(page + (layout.main_relay - address));
    auto* const main = fixture_main_address();
    main_relay(main);
    check(callback_count.load(std::memory_order_relaxed) == 0,
        "Main observation callback remains inactive until all native writes commit");
    check(original_count.load(std::memory_order_relaxed) == 1,
        "inactive wrapper forwards the original Main call exactly once");

    native_dispatch_test::set_install_ready();
    check(activate_main_observation(&observation_callback),
        "Main callback activates only after a ready install");
    check(!activate_main_observation(&observation_callback),
        "Main callback activation is one-shot");
    main_order.store(0, std::memory_order_release);
    main_relay(main);
    check(callback_count.load(std::memory_order_relaxed) == 1 &&
              original_count.load(std::memory_order_relaxed) == 2,
        "active Main relay invokes the observer and original exactly once each");
    check(main_order.load(std::memory_order_acquire) == 2 &&
              callback_seen_original_calls.load(std::memory_order_acquire) == 2,
        "production Main wrapper forwards the original exactly once before observation");
    check(original_main.load(std::memory_order_relaxed) == main,
        "production Main wrapper forwards the exact Main pointer");

    main_relay(main);
    check(callback_count.load(std::memory_order_relaxed) == 2 &&
              original_count.load(std::memory_order_relaxed) == 3,
        "repeated Main relay calls forward the original and observation exactly once each");

    native_dispatch_test::set_main_message_loop_original(&throwing_main_original);
    callback_count.store(0, std::memory_order_relaxed);
    original_count.store(0, std::memory_order_relaxed);
    bool original_unwound{};
    try {
        main_relay(main);
    } catch (const std::runtime_error&) {
        original_unwound = true;
    }
    check(original_unwound && original_count.load(std::memory_order_relaxed) == 1 &&
              callback_count.load(std::memory_order_relaxed) == 0,
        "original message-pump unwind propagates without manufacturing an observation callback");

    constexpr std::array<std::uint8_t, 23> continuation{
        0x57,
        0x41,
        0x56,
        0x48,
        0x83,
        0xEC,
        0x40,
        0x48,
        0x83,
        0xC4,
        0x40,
        0x41,
        0x5E,
        0x5F,
        0x5E,
        0x48,
        0x8B,
        0x5C,
        0x24,
        0x10,
        0xB0,
        0x01,
        0xC3,
    };
    static_assert(continuation.size() == 23);
    std::memcpy(page + 1536U, continuation.data(), continuation.size());
    check(FlushInstructionCache(GetCurrentProcess(), page + 1536U, continuation.size()) != FALSE,
        "host fixture flushes the original remap continuation stub");
    ui_capture_test::set_remap_original(
        reinterpret_cast<RemapOriginal>(page + (layout.remap_gateway - address)));
    auto* control_map = reinterpret_cast<RE::ControlMap*>(std::uintptr_t{0x1000U});
    const auto& event_name = empty_remap_event_argument();
    check(ui_capture_test::invoke_remap(control_map, event_name, RE::INPUT_DEVICE::kGamepad, 7),
        "production RemapButton wrapper returns the result from its emitted original gateway");
    check(ui_capture_test::in_flight() == 0 && ui_transition_generation() == 10,
        "emitted remap gateway execution is tracked and releases the synchronous transition fence");
}

} // namespace fallout4_adapter::tests::dispatch
