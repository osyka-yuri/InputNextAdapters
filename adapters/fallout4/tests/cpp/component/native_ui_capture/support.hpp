#pragma once

#include "native/ui_capture.hpp"
#include "native/ui_capture_abi.hpp"
#include "native/ui_capture_pipeline.hpp"
#include "ui_capture_test.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#undef FAIL

namespace fallout4_adapter::tests::ui_capture {
using namespace fallout4_adapter::native;

inline std::uint64_t ui_transition_generation() noexcept {
    return native::ui_capture_test::generation();
}

inline void check(bool condition, const char* message) {
    INFO(message);
    CHECK(condition);
}

inline const RE::BSFixedString& empty_remap_event_argument() {
    // This empty fixture avoids the game-only string-pool release relocation at shutdown.
    alignas(RE::BSFixedString) static std::array<std::byte, sizeof(RE::BSFixedString)> storage{};
    static const auto* value =
        std::construct_at(reinterpret_cast<RE::BSFixedString*>(storage.data()));
    return *value;
}

constexpr std::uint64_t callback_xor = 0xA91E7C3355D20F68ULL;
constexpr std::uint64_t exit_clobber_value = 0xCFD1E2A394857667ULL;
using ui_capture_test::ExitClobber;
inline std::atomic<std::uint32_t> exit_clobber_calls{};
inline std::atomic<bool> block_original{};
inline std::atomic<bool> release_original{};
inline std::atomic<bool> original_entered{};
inline std::atomic<bool> nested_original{};
inline std::atomic<std::uint8_t> last_original_cl{};
inline std::atomic<std::uint64_t> last_original_rax{};
inline std::atomic<std::uint64_t> last_original_in_flight{};
inline std::atomic<std::uint64_t> last_nested_result{};
inline std::atomic<std::uint32_t> original_calls{};
inline thread_local bool inside_nested_original{};
inline std::atomic<std::uint32_t> remap_calls{};
inline std::atomic<RE::ControlMap*> remap_last_map{};
inline std::atomic<const RE::BSFixedString*> remap_last_event{};
inline std::atomic<RE::INPUT_DEVICE> remap_last_device{};
inline std::atomic<std::int32_t> remap_last_button{};
inline std::atomic<std::uint64_t> remap_original_generation{};
inline std::atomic<std::uint64_t> remap_original_in_flight{};
inline std::atomic<bool> remap_result{true};
inline std::atomic<bool> block_remap{};
inline std::atomic<std::uint32_t> remap_entered{};
inline std::atomic<bool> release_remap{};
inline std::atomic<bool> nested_remap{};
inline std::atomic<bool> nested_remap_result{};
inline thread_local bool inside_nested_remap{};

inline void count_exit_clobber() {
    exit_clobber_calls.fetch_add(1, std::memory_order_relaxed);
}

// SeededOriginal models the fixed Windows x64 helper trampoline: CL and seeded RAX
// occupy distinct ABI arguments whose positions are part of the emitted instruction contract.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): preserve the fixed SeededOriginal ABI.
inline std::uint64_t controlled_original(std::uint8_t original_cl, std::uint64_t entry_rax) {
    original_calls.fetch_add(1, std::memory_order_relaxed);
    last_original_cl.store(original_cl, std::memory_order_relaxed);
    last_original_rax.store(entry_rax, std::memory_order_relaxed);
    last_original_in_flight.store(ui_capture_test::in_flight(), std::memory_order_relaxed);

    if (nested_original.load(std::memory_order_acquire) && !inside_nested_original) {
        inside_nested_original = true;
        last_nested_result.store(ui_capture_test::invoke(0xA5U, 0xE781239ABCD456F0ULL),
            std::memory_order_release);
        inside_nested_original = false;
    }

    if (block_original.load(std::memory_order_acquire)) {
        original_entered.store(true, std::memory_order_release);
        while (!release_original.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
    return entry_rax ^ callback_xor;
}

static_assert(std::is_same_v<decltype(&controlled_original), SeededOriginal>);

inline std::uint64_t throwing_original(std::uint8_t /*flag*/, std::uint64_t /*entry_rax*/) {
    throw std::runtime_error("owned original-call escape fixture");
}

static_assert(std::is_same_v<decltype(&throwing_original), SeededOriginal>);

inline bool controlled_remap_original(RE::ControlMap* control_map,
    const RE::BSFixedString& event_name,
    RE::INPUT_DEVICE device,
    std::int32_t button) {
    remap_calls.fetch_add(1, std::memory_order_relaxed);
    remap_last_map.store(control_map, std::memory_order_relaxed);
    remap_last_event.store(std::addressof(event_name), std::memory_order_relaxed);
    remap_last_device.store(device, std::memory_order_relaxed);
    remap_last_button.store(button, std::memory_order_relaxed);
    remap_original_generation.store(ui_transition_generation(), std::memory_order_relaxed);
    remap_original_in_flight.store(ui_capture_test::in_flight(), std::memory_order_relaxed);
    if (nested_remap.load(std::memory_order_acquire) && !inside_nested_remap) {
        inside_nested_remap = true;
        nested_remap_result.store(
            ui_capture_test::invoke_remap(control_map, event_name, device, button + 1),
            std::memory_order_release);
        inside_nested_remap = false;
    }
    if (block_remap.load(std::memory_order_acquire)) {
        remap_entered.fetch_add(1, std::memory_order_acq_rel);
        while (!release_remap.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
    return remap_result.load(std::memory_order_acquire);
}

inline bool throwing_remap_original(RE::ControlMap* /*control_map*/,
    const RE::BSFixedString& /*event_name*/,
    RE::INPUT_DEVICE /*device*/,
    std::int32_t /*button*/) {
    throw std::runtime_error("owned RemapButton original-call escape fixture");
}

struct CaptureLockFixture {
    alignas(8) std::array<volatile long, 2> process_words{};
    alignas(4) volatile long menu_read_count{};
    alignas(4) volatile long context_read_count{};
    std::array<std::byte, 64> static_fields{};
};

constexpr UiStaticOffsets fixture_static_offsets{
    .manager_slot = 8,
    .state = 24,
    .shutdown = 32,
    .running = 33,
};

struct UiStateFixture {
    std::uint32_t state{};
    std::uint8_t shutdown{};
    std::uint8_t running{1};
};

inline RE::ControlMap* fake_control_map_address() noexcept {
    static std::byte storage{};
    return reinterpret_cast<RE::ControlMap*>(std::addressof(storage));
}

inline void
put_static_field(std::array<std::byte, 64>& storage, std::size_t offset, const auto& value) {
    std::memcpy(storage.data() + offset, &value, sizeof(value));
}

struct ProductionFenceFixture {
    CaptureLockFixture locks{};
    std::uintptr_t image{};
    std::uint64_t generation{};
    std::size_t attempt_count{};
    std::size_t release_count{};
    std::size_t copy_count{};
    std::array<UiCaptureFence, 4> attempts{};
    std::array<UiCaptureFence, 4> releases{};

    void initialize_state(UiStateFixture state = {}) {
        image = reinterpret_cast<std::uintptr_t>(locks.static_fields.data());
        const auto slot = image + 0x32A1320U;
        put_static_field(locks.static_fields, fixture_static_offsets.manager_slot, slot);
        put_static_field(locks.static_fields, fixture_static_offsets.state, state.state);
        put_static_field(locks.static_fields, fixture_static_offsets.shutdown, state.shutdown);
        put_static_field(locks.static_fields, fixture_static_offsets.running, state.running);
    }

    bool try_acquire(UiCaptureFence fence) noexcept {
        attempts[attempt_count++] = fence;
        switch (fence) {
        case UiCaptureFence::process_messages:
            return ui_capture_test::try_process_lock(locks.process_words.data(),
                GetCurrentThreadId());
        case UiCaptureFence::transition_gate:
            return ui_capture_test::try_gate();
        case UiCaptureFence::menu_stack:
            return ui_capture_test::try_read_count(&locks.menu_read_count);
        case UiCaptureFence::input_context:
            return ui_capture_test::try_read_count(&locks.context_read_count);
        }
        return false;
    }

    [[nodiscard]] bool eligible() const noexcept {
        return ui_capture_test::eligible(image, fixture_static_offsets, generation);
    }

    void release(UiCaptureFence fence) noexcept {
        releases[release_count++] = fence;
        switch (fence) {
        case UiCaptureFence::process_messages:
            ui_capture_test::release_process_lock(locks.process_words.data());
            break;
        case UiCaptureFence::transition_gate:
            ui_capture_test::release_gate();
            break;
        case UiCaptureFence::menu_stack:
            ui_capture_test::release_read_count(&locks.menu_read_count);
            break;
        case UiCaptureFence::input_context:
            ui_capture_test::release_read_count(&locks.context_read_count);
            break;
        }
    }
};

struct OwnedSuccessfulProtocolFixture {
    ProductionFenceFixture capture{};
    bool completion_signaled{};
    bool startup_reset_preceded_state_load{};
    bool trailing_false_rejected_while_counted{};

    void reset_stale_completion_before_loading_state() noexcept {
        completion_signaled = true;  // owned scalar stand-in for a stale signaled completion event
        completion_signaled = false; // startup reset occurs before the state load
        startup_reset_preceded_state_load = !completion_signaled;
        capture.initialize_state({.state = 2});
    }
};

inline OwnedSuccessfulProtocolFixture* active_successful_protocol{};

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): preserve the fixed SeededOriginal ABI.
inline std::uint64_t successful_protocol_original(std::uint8_t original_cl,
    std::uint64_t entry_rax) {
    auto& fixture = *active_successful_protocol;
    fixture.capture.generation = ui_transition_generation();
    if (original_cl == 0) {
        fixture.capture.initialize_state({.state = 0});
        fixture.trailing_false_rejected_while_counted = !fixture.capture.eligible();
        fixture.completion_signaled =
            true; // the owned worker has completed the trailing-false operation
    } else {
        fixture.capture.initialize_state({.state = 2});
        fixture.completion_signaled = true;
    }
    return entry_rax ^ callback_xor;
}

static_assert(std::is_same_v<decltype(&successful_protocol_original), SeededOriginal>);

template <class Predicate>
inline bool wait_until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    return predicate();
}

struct FenceProbe {
    std::size_t fail_at{std::numeric_limits<std::size_t>::max()};
    std::size_t attempt_count{};
    std::size_t eligibility_count{};
    std::size_t copy_count{};
    std::array<UiCaptureFence, 4> attempts{};
    std::array<UiCaptureFence, 4> releases{};
    std::size_t release_count{};
    bool state_eligible{true};
    bool require_quiescent{};

    [[nodiscard]] bool try_acquire(UiCaptureFence fence) noexcept {
        const auto index = attempt_count++;
        attempts[index] = fence;
        return index != fail_at;
    }

    [[nodiscard]] bool eligible() noexcept {
        ++eligibility_count;
        return state_eligible && (!require_quiescent || ui_capture_test::in_flight() == 0);
    }

    void release(UiCaptureFence fence) noexcept { releases[release_count++] = fence; }
};

struct OwnerPhaseProbe {
    OwnerPhaseFacts facts{};
    bool gate_available{true};
    std::size_t attempts{};
    std::size_t reads{};
    std::size_t releases{};

    [[nodiscard]] bool try_gate() noexcept {
        ++attempts;
        return gate_available;
    }

    [[nodiscard]] OwnerPhaseFacts read_facts() noexcept {
        ++reads;
        return facts;
    }

    void release_gate() noexcept { ++releases; }
};

class ScopedExecutablePage {
public:
    explicit ScopedExecutablePage(std::size_t size) noexcept
        : page_(static_cast<std::byte*>(
              VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))) {}

    ScopedExecutablePage(const ScopedExecutablePage&) = delete;
    ScopedExecutablePage& operator=(const ScopedExecutablePage&) = delete;
    ScopedExecutablePage(ScopedExecutablePage&&) = delete;
    ScopedExecutablePage& operator=(ScopedExecutablePage&&) = delete;

    ~ScopedExecutablePage() noexcept {
        if (page_) {
            static_cast<void>(VirtualFree(page_, 0, MEM_RELEASE));
        }
    }

    [[nodiscard]] std::byte* get() const noexcept { return page_; }

private:
    std::byte* page_{};
};

class ScopedThread {
public:
    template <class Release, class Function>
    ScopedThread(Release&& release, Function&& function)
        : release_(std::forward<Release>(release)),
          thread_(std::forward<Function>(function)) {}

    ScopedThread(const ScopedThread&) = delete;
    ScopedThread& operator=(const ScopedThread&) = delete;
    ScopedThread(ScopedThread&&) = delete;
    ScopedThread& operator=(ScopedThread&&) = delete;

    ~ScopedThread() noexcept {
        if (thread_.joinable()) {
            release_();
            join();
        }
    }

    void join() noexcept {
        if (thread_.joinable()) {
            try {
                thread_.join();
            } catch (...) {
                std::terminate();
            }
        }
    }

private:
    std::function<void()> release_;
    std::thread thread_;
};

class ScopedTransitionGate {
public:
    explicit ScopedTransitionGate(bool acquired = false) noexcept : acquired_(acquired) {}

    ScopedTransitionGate(const ScopedTransitionGate&) = delete;
    ScopedTransitionGate& operator=(const ScopedTransitionGate&) = delete;
    ScopedTransitionGate(ScopedTransitionGate&&) = delete;
    ScopedTransitionGate& operator=(ScopedTransitionGate&&) = delete;

    ~ScopedTransitionGate() noexcept { release(); }

    void release() noexcept {
        if (acquired_) {
            ui_capture_test::release_gate();
            acquired_ = false;
        }
    }

private:
    bool acquired_{};
};

inline void reset_callback_controls() noexcept {
    exit_clobber_calls.store(0, std::memory_order_relaxed);
    block_original.store(false, std::memory_order_relaxed);
    release_original.store(true, std::memory_order_relaxed);
    original_entered.store(false, std::memory_order_relaxed);
    nested_original.store(false, std::memory_order_relaxed);
    last_original_cl.store(0, std::memory_order_relaxed);
    last_original_rax.store(0, std::memory_order_relaxed);
    last_original_in_flight.store(0, std::memory_order_relaxed);
    last_nested_result.store(0, std::memory_order_relaxed);
    original_calls.store(0, std::memory_order_relaxed);
    inside_nested_original = false;
    remap_calls.store(0, std::memory_order_relaxed);
    remap_last_map.store(nullptr, std::memory_order_relaxed);
    remap_last_event.store(nullptr, std::memory_order_relaxed);
    remap_last_device.store(RE::INPUT_DEVICE::kKeyboard, std::memory_order_relaxed);
    remap_last_button.store(0, std::memory_order_relaxed);
    remap_original_generation.store(0, std::memory_order_relaxed);
    remap_original_in_flight.store(0, std::memory_order_relaxed);
    remap_result.store(true, std::memory_order_relaxed);
    block_remap.store(false, std::memory_order_relaxed);
    remap_entered.store(0, std::memory_order_relaxed);
    release_remap.store(true, std::memory_order_relaxed);
    nested_remap.store(false, std::memory_order_relaxed);
    nested_remap_result.store(false, std::memory_order_relaxed);
    inside_nested_remap = false;
}

class ScopedCaptureState {
public:
    explicit ScopedCaptureState(std::uint64_t generation = 1) noexcept {
        reset_callback_controls();
        active_successful_protocol = nullptr;
        ui_capture_test::clear_originals();
        ui_capture_test::set_exit_clobber(nullptr);
        ui_capture_test::reset(generation);
    }

    ScopedCaptureState(const ScopedCaptureState&) = delete;
    ScopedCaptureState& operator=(const ScopedCaptureState&) = delete;
    ScopedCaptureState(ScopedCaptureState&&) = delete;
    ScopedCaptureState& operator=(ScopedCaptureState&&) = delete;

    ~ScopedCaptureState() noexcept {
        block_original.store(false, std::memory_order_release);
        release_original.store(true, std::memory_order_release);
        block_remap.store(false, std::memory_order_release);
        release_remap.store(true, std::memory_order_release);
        active_successful_protocol = nullptr;
        ui_capture_test::set_exit_clobber(nullptr);
        ui_capture_test::clear_originals();
        ui_capture_test::reset();
        reset_callback_controls();
    }
};

} // namespace fallout4_adapter::tests::ui_capture
