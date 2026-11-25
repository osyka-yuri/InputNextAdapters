#include "native/observation.hpp"
#include "native/ui_capture.hpp"
#include "native/ui_capture_abi.hpp"
#include "support.hpp"
#include "ui_capture_test.hpp"

#include <RE/B/BSSpinLock.h>

#include <catch2/catch_test_macros.hpp>
#undef FAIL

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>

namespace fallout4_adapter::tests::ui_capture {

TEST_CASE("fallout4.native_ui_capture.ui_capture_static_projection",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state{100};

    ui_capture_test::reset(100);
    ProductionFenceFixture fixture{};
    fixture.generation = 100;
    fixture.initialize_state();
    UiStaticFields projected{};
    check(ui_capture_test::project_static_fields(fixture.image, fixture_static_offsets, projected),
        "production static-field projection reads the owned raw fixture");
    check(ui_capture_test::static_fields_eligible(fixture.image, projected),
        "production static eligibility accepts exact slot, state zero, shutdown zero, and running "
        "one");
    check(ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "current quiescent production tuple is eligible");
    check(!ui_capture_test::eligible(fixture.image, fixture_static_offsets, 101),
        "stale expected generation is rejected");

    fixture.initialize_state({.state = 2});
    check(!ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "startup stale completion state two is rejected");
    fixture.initialize_state({.state = 1});
    check(!ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "state one is rejected while the startup worker has not reached the completed state");
    fixture.initialize_state({.state = 0, .shutdown = 1, .running = 0});
    check(!ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "shutdown and stopped worker state are rejected");
    fixture.initialize_state({.state = 0, .shutdown = 0, .running = 1});
    ui_capture_test::set_in_flight(1);
    check(!ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "an active helper blocks scalar eligibility");
    ui_capture_test::set_in_flight(0);
    check(ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "reinitialized static state reopens only the current generation");
}

TEST_CASE("fallout4.native_ui_capture.ui_capture_native_lockwords",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state{100};
    ui_capture_test::reset(100);
    ProductionFenceFixture fixture{};
    fixture.generation = 100;
    fixture.initialize_state();
    check(ui_capture_test::try_process_lock(&fixture.locks.process_lock, GetCurrentThreadId()),
        "production process-lock CAS acquires an idle owned lockword");
    check(inspect_spin_lock(fixture.locks.process_lock).owner_thread == GetCurrentThreadId(),
        "process-lock owner word is published after acquisition");
    check(!ui_capture_test::try_process_lock(&fixture.locks.process_lock, GetCurrentThreadId()),
        "recursive or already-owned process lock declines without spinning");
    ui_capture_test::release_process_lock(&fixture.locks.process_lock, GetCurrentThreadId());
    const auto released_process_words = inspect_spin_lock(fixture.locks.process_lock);
    check(released_process_words.owner_thread == 0 && released_process_words.lock_word == 0,
        "production process-lock release clears owner and lock words");

    check(ui_capture_test::try_read_count(&fixture.locks.menu_read_count),
        "production read-subset CAS acquires an idle count");
    check(fixture.locks.menu_read_count == 1,
        "read-subset acquisition performs one zero-to-one CAS");
    ScopedThread joining_reader{[] {},
        [&] {
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(&fixture.locks.menu_read_count));
        }};
    joining_reader.join();
    check(fixture.locks.menu_read_count == 2,
        "an owned joining reader advances the shared count from one to two");
    ui_capture_test::release_read_count(&fixture.locks.menu_read_count);
    check(fixture.locks.menu_read_count == 1,
        "production release drops only its reader from two back to one");
    ui_capture_test::release_read_count(&fixture.locks.menu_read_count);
    check(fixture.locks.menu_read_count == 0,
        "the final production reader release decrements one to zero");
    fixture.locks.menu_read_count = 0x80000000U;
    check(!ui_capture_test::try_read_count(&fixture.locks.menu_read_count),
        "an owned writer count is declined");
    fixture.locks.menu_read_count = 2;
    check(!ui_capture_test::try_read_count(&fixture.locks.menu_read_count),
        "an existing reader count is declined");
    fixture.locks.menu_read_count = 0;
}

TEST_CASE("fallout4.native_ui_capture.ui_capture_production_fences",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state{100};
    ui_capture_test::reset(100);
    ProductionFenceFixture fixture{};
    fixture.generation = 100;
    fixture.initialize_state();
    check(with_ui_capture_fences(fixture,
              [&] {
                  ++fixture.copy_count;
                  return true;
              }),
        "production fence orchestration runs against the actual process, private gate, and native "
        "lock operations");
    check(fixture.copy_count == 1 && fixture.release_count == 4,
        "production fence tuple copies once and releases all four locks");
    check(inspect_spin_lock(fixture.locks.process_lock).lock_word == 0 &&
              fixture.locks.menu_read_count == 0 && fixture.locks.context_read_count == 0,
        "all owned lockword fixtures are free after the production tuple");

    fixture.locks.context_read_count = 1;
    fixture.attempt_count = 0;
    fixture.release_count = 0;
    fixture.copy_count = 0;
    check(!with_ui_capture_fences(fixture,
              [&] {
                  ++fixture.copy_count;
                  return true;
              }),
        "busy context read count declines the production tuple after a single CAS");
    check(fixture.attempt_count == 4 && fixture.copy_count == 0,
        "busy context path performs no retry or scalar copy");
    check(inspect_spin_lock(fixture.locks.process_lock).lock_word == 0 &&
              fixture.locks.menu_read_count == 0,
        "earlier acquired locks release in reverse on context contention");
    fixture.locks.context_read_count = 0;
}

TEST_CASE("fallout4.native_ui_capture.ui_capture_fence_order",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    constexpr std::array expected_order{
        UiCaptureFence::process_messages,
        UiCaptureFence::transition_gate,
        UiCaptureFence::menu_stack,
        UiCaptureFence::input_context,
    };
    constexpr std::array expected_release{
        UiCaptureFence::input_context,
        UiCaptureFence::menu_stack,
        UiCaptureFence::transition_gate,
        UiCaptureFence::process_messages,
    };

    FenceProbe complete{};
    check(with_ui_capture_fences(complete,
              [&] {
                  ++complete.copy_count;
                  return true;
              }),
        "all four available fences permit one scalar copy");
    check(complete.attempt_count == 4 && complete.eligibility_count == 1 &&
              complete.copy_count == 1,
        "successful fence tuple tries each fence once and copies once");
    check(complete.attempts == expected_order, "fences acquire in the mapped compatibility order");
    check(complete.release_count == 4 && complete.releases == expected_release,
        "fences release in reverse order");

    for (std::size_t fail_at = 0; fail_at < expected_order.size(); ++fail_at) {
        FenceProbe failed{};
        failed.fail_at = fail_at;
        check(!with_ui_capture_fences(failed,
                  [&] {
                      ++failed.copy_count;
                      return true;
                  }),
            "one contended fence declines the tuple");
        check(failed.attempt_count == fail_at + 1U,
            "contended fence is attempted once with no retry");
        check(failed.eligibility_count == 0 && failed.copy_count == 0,
            "failed acquisition skips eligibility and copy");
        check(failed.release_count == fail_at,
            "only acquired fences are released after contention");
        for (std::size_t index = 0; index < fail_at; ++index) {
            check(failed.releases[index] == expected_release[4U - fail_at + index],
                "partial fence release unwinds in reverse order");
        }
    }

    FenceProbe ineligible{};
    ineligible.state_eligible = false;
    check(!with_ui_capture_fences(ineligible,
              [&] {
                  ++ineligible.copy_count;
                  return true;
              }),
        "ineligible UI state declines after all fences");
    check(ineligible.eligibility_count == 1 && ineligible.copy_count == 0,
        "ineligible tuple releases fences without copying");
}

TEST_CASE("fallout4.native_ui_capture.ui_owner_phase_admission",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    OwnerPhaseProbe ready{};
    ready.facts = {
        .generation = 70,
        .in_flight = 0,
        .closed = false,
        .static_fields_eligible = true,
    };
    check(admit_owner_phase(ready, 70) == OwnerPhaseAdmission::ready,
        "production owner phase admission accepts the exact idle state-zero tuple");
    check(ready.attempts == 1 && ready.reads == 1 && ready.releases == 1,
        "owner phase admission attempts the gate once and releases it");

    OwnerPhaseProbe busy_gate{};
    busy_gate.gate_available = false;
    check(admit_owner_phase(busy_gate, 70) == OwnerPhaseAdmission::deferred &&
              busy_gate.attempts == 1 && busy_gate.reads == 0 && busy_gate.releases == 0,
        "private-gate contention defers without reading native phase fields");

    OwnerPhaseProbe transition{};
    transition.facts = {
        .generation = 71,
        .in_flight = 1,
        .closed = false,
        .static_fields_eligible = true,
    };
    check(admit_owner_phase(transition, 70) == OwnerPhaseAdmission::retired,
        "a changed generation retires the old request during a transition");

    OwnerPhaseProbe state_two{};
    state_two.facts = {
        .generation = 70,
        .in_flight = 0,
        .closed = false,
        .static_fields_eligible = false,
    };
    check(admit_owner_phase(state_two, 70) == OwnerPhaseAdmission::deferred,
        "unready UI processing state defers a request without waiting");

    OwnerPhaseProbe closed{};
    closed.facts = {
        .generation = 0,
        .in_flight = 0,
        .closed = true,
        .static_fields_eligible = false,
    };
    check(admit_owner_phase(closed, 70) == OwnerPhaseAdmission::retired,
        "permanently closed capture retires a pending request");
    OwnerPhaseProbe zero{};
    check(admit_owner_phase(zero, 0) == OwnerPhaseAdmission::retired && zero.attempts == 0,
        "zero generation retires without touching the phase gate");
}

TEST_CASE("fallout4.native_ui_capture.ui_scalar_count_bounds",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;

    check(ui_scalar_counts_valid(64, 64, 256), "maximum context/menu/mapping counts are accepted");
    check(!ui_scalar_counts_valid(0, 0, 0), "empty context stack is rejected");
    check(!ui_scalar_counts_valid(65, 0, 0), "oversized context stack is rejected");
    check(!ui_scalar_counts_valid(1, 65, 0), "oversized menu stack is rejected");
    check(!ui_scalar_counts_valid(1, 0, 257), "oversized mapping list is rejected");
}

TEST_CASE("fallout4.native_ui_capture.ui_mapping_selection",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    constexpr auto attack_name = std::to_array("PrimaryAttack");
    constexpr auto aim_name = std::to_array("SecondaryAttack");
    constexpr auto same_attack_text = std::to_array("PrimaryAttack");
    constexpr auto other_name = std::to_array("Reload");
    const std::array mappings{
        UiMappingValue{.event_name = other_name.data(), .input_key = 0x01},
        UiMappingValue{.event_name = attack_name.data(), .input_key = 0x0A},
        UiMappingValue{.event_name = same_attack_text.data(), .input_key = 0x09},
        UiMappingValue{.event_name = aim_name.data(), .input_key = -1},
    };
    const auto selected = select_ui_mapped_keys(mappings.size(),
        attack_name.data(),
        aim_name.data(),
        [&mappings](std::size_t index) { return mappings[index]; });
    check(selected.valid, "bounded mapping selection accepts a valid list");
    check(selected.attack == 0x0AU, "mapping selection preserves first matching interned action");
    check(selected.aim == invalid_mapped_key,
        "negative mapped key remains invalid rather than guessed");
    check(!select_ui_mapped_keys(257,
              attack_name.data(),
              aim_name.data(),
              [](std::size_t) { return UiMappingValue{}; })
              .valid,
        "mapping selector rejects oversized lists before reading an entry");
}

TEST_CASE("fallout4.native_ui_capture.ui_host_admission",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    check(supported_ui_capture_host(REL::Version{1, 11, 240, 0}, REL::Version{0, 7, 9, 0}, 1, 1),
        "production host-admission predicate accepts only the pinned runtime and F4SE interfaces");
    check(!supported_ui_capture_host(REL::Version{1, 11, 221, 0}, REL::Version{0, 7, 9, 0}, 1, 1),
        "wrong runtime is rejected");
    check(!supported_ui_capture_host(REL::Version{1, 11, 240, 0}, REL::Version{0, 7, 8, 0}, 1, 1),
        "wrong F4SE version is rejected");
    check(!supported_ui_capture_host(REL::Version{1, 11, 240, 0}, REL::Version{0, 7, 9, 0}, 2, 1),
        "wrong messaging interface is rejected");
    check(!supported_ui_capture_host(REL::Version{1, 11, 240, 0}, REL::Version{0, 7, 9, 0}, 1, 2),
        "wrong trampoline interface is rejected");
}

namespace {
std::atomic<std::size_t> g_lock_failure_callbacks{0};

void record_lock_failure() noexcept {
    g_lock_failure_callbacks.fetch_add(1U, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("fallout4.native_ui_capture.ui_lock_protocol_failure_and_high_bits",
    "[component][fallout4][native_ui_capture]") {
    g_lock_failure_callbacks.store(0U, std::memory_order_relaxed);
    RE::BSSpinLock spin_lock{};
    constexpr std::uint32_t high_bit_tid = 0x80000001U;
    check(ui_capture_test::try_process_lock(&spin_lock, high_bit_tid) &&
              inspect_spin_lock(spin_lock).owner_thread == high_bit_tid &&
              inspect_spin_lock(spin_lock).lock_word == 1U,
        "high-bit unsigned thread IDs are preserved in owner_thread without signed truncation");
    ui_capture_test::release_process_lock(&spin_lock, high_bit_tid, &record_lock_failure);
    check(inspect_spin_lock(spin_lock).owner_thread == 0U &&
              inspect_spin_lock(spin_lock).lock_word == 0U &&
              g_lock_failure_callbacks.load(std::memory_order_relaxed) == 0U,
        "valid process-lock release clears both words without triggering failure");

    seed_spin_lock(spin_lock, high_bit_tid, 0U);
    ui_capture_test::release_process_lock(&spin_lock, high_bit_tid, &record_lock_failure);
    check(inspect_spin_lock(spin_lock).owner_thread == high_bit_tid &&
              inspect_spin_lock(spin_lock).lock_word == 0U &&
              g_lock_failure_callbacks.load(std::memory_order_relaxed) == 1U,
        "process-lock release preserves owner_thread and triggers on_failure when lock_word is "
        "zero");

    seed_spin_lock(spin_lock, high_bit_tid, 2U);
    ui_capture_test::release_process_lock(&spin_lock, high_bit_tid, &record_lock_failure);
    check(inspect_spin_lock(spin_lock).owner_thread == high_bit_tid &&
              inspect_spin_lock(spin_lock).lock_word == 2U &&
              g_lock_failure_callbacks.load(std::memory_order_relaxed) == 2U,
        "recursive process lock preserves both owner_thread and lock_word on invalid release");

    seed_spin_lock(spin_lock, high_bit_tid, 1U);
    ui_capture_test::release_process_lock(&spin_lock, high_bit_tid + 1U, &record_lock_failure);
    check(inspect_spin_lock(spin_lock).owner_thread == high_bit_tid &&
              inspect_spin_lock(spin_lock).lock_word == 1U &&
              g_lock_failure_callbacks.load(std::memory_order_relaxed) == 3U,
        "foreign thread release refuses to clear another thread's process lock");

    seed_spin_lock(spin_lock, high_bit_tid, 1U);
    ui_capture_test::release_process_lock(&spin_lock, 0U, &record_lock_failure);
    check(inspect_spin_lock(spin_lock).owner_thread == high_bit_tid &&
              inspect_spin_lock(spin_lock).lock_word == 1U &&
              g_lock_failure_callbacks.load(std::memory_order_relaxed) == 4U,
        "zero expected_thread_id refuses to release an owned process lock and invokes on_failure");

    volatile std::uint32_t read_count = 0U;
    ui_capture_test::release_read_count(&read_count, &record_lock_failure);
    check(read_count == 0U && g_lock_failure_callbacks.load(std::memory_order_relaxed) == 5U,
        "read-count release from zero refuses to underflow and invokes on_failure");

    read_count = 0x80000000U;
    ui_capture_test::release_read_count(&read_count, &record_lock_failure);
    check(read_count == 0x80000000U &&
              g_lock_failure_callbacks.load(std::memory_order_relaxed) == 6U,
        "read-count release with writer bit active (0x80000000) preserves writer bit and invokes "
        "on_failure");

    read_count = 0x80000001U;
    ui_capture_test::release_read_count(&read_count, &record_lock_failure);
    check(read_count == 0x80000001U &&
              g_lock_failure_callbacks.load(std::memory_order_relaxed) == 7U,
        "read-count release with recursive writer lock (0x80000001) preserves writer state and "
        "invokes on_failure");

    ui_capture_test::release_process_lock(nullptr, 1U, &record_lock_failure);
    ui_capture_test::release_read_count(nullptr, &record_lock_failure);
    alignas(4) volatile std::uint32_t aligned_word{};
    auto* misaligned = reinterpret_cast<volatile std::uint32_t*>(
        reinterpret_cast<volatile std::byte*>(&aligned_word) + 1U);
    ui_capture_test::release_read_count(misaligned, &record_lock_failure);
    check(!ui_capture_test::try_process_lock(nullptr, 1U) &&
              !ui_capture_test::try_process_lock(&spin_lock, 0U) &&
              !ui_capture_test::try_read_count(nullptr) &&
              !ui_capture_test::try_read_count(misaligned) &&
              g_lock_failure_callbacks.load(std::memory_order_relaxed) == 10U,
        "null, zero-tid, and misaligned lock parameters fail safely and trigger release "
        "diagnostics");
}

namespace {
[[nodiscard]] bool wait_for_flag(const std::atomic<bool>& flag,
    const std::stop_token& stop) noexcept {
    while (!flag.load(std::memory_order_acquire)) {
        if (stop.stop_requested()) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

[[nodiscard]] bool wait_for_phase(const std::atomic<int>& phase,
    int target_phase,
    const std::stop_token& stop) noexcept {
    while (phase.load(std::memory_order_acquire) != target_phase) {
        if (stop.stop_requested()) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

struct RwContentionState {
    alignas(4) volatile std::uint32_t word{0};
    std::atomic<int> phase{0};
    std::atomic<bool> stress_done{false};
    std::atomic<std::size_t> engine_reader_joins{0};
    std::atomic<std::uint32_t> joined_word_observed{0};
    std::atomic<bool> writer_blocked_by_readers{false};
    std::atomic<bool> writer_acquired_exclusive{false};
};

[[nodiscard]] std::uint32_t load_rw_word(volatile std::uint32_t* rw_word) noexcept {
    return static_cast<std::uint32_t>(
        InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(rw_word), 0, 0));
}

void run_engine_reader(RwContentionState& state, const std::stop_token& stop) noexcept {
    auto* const rw_word = &state.word;
    if (!wait_for_phase(state.phase, 1, stop)) {
        return;
    }
    const auto joined_prev = InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(rw_word),
        static_cast<LONG>(2U),
        static_cast<LONG>(1U));
    if (joined_prev == 1L) {
        state.engine_reader_joins.fetch_add(1U, std::memory_order_relaxed);
        state.joined_word_observed.store(load_rw_word(rw_word), std::memory_order_release);
    }
    state.phase.store(2, std::memory_order_release);
    if (!wait_for_phase(state.phase, 4, stop)) {
        if (joined_prev == 1L) {
            InterlockedDecrement(reinterpret_cast<volatile LONG*>(rw_word));
        }
        return;
    }
    if (joined_prev == 1L) {
        InterlockedDecrement(reinterpret_cast<volatile LONG*>(rw_word));
    }
    state.phase.store(5, std::memory_order_release);
    if (!wait_for_phase(state.phase, 8, stop)) {
        return;
    }
    while (!state.stress_done.load(std::memory_order_acquire) && !stop.stop_requested()) {
        const auto current =
            InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(rw_word), 0, 0);
        const auto unsigned_current = static_cast<std::uint32_t>(current);
        if ((unsigned_current & 0x80000000U) == 0U && (unsigned_current & 0x0FFFFFFFU) > 0U &&
            InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(rw_word),
                current + 1,
                current) == current) {
            state.engine_reader_joins.fetch_add(1U, std::memory_order_relaxed);
            InterlockedDecrement(reinterpret_cast<volatile LONG*>(rw_word));
        }
    }
}

void run_engine_writer(RwContentionState& state, const std::stop_token& stop) noexcept {
    auto* const rw_word = &state.word;
    if (!wait_for_phase(state.phase, 2, stop)) {
        return;
    }
    const auto contended = InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(rw_word),
        static_cast<LONG>(0x80000001U),
        0L);
    state.writer_blocked_by_readers.store(contended == 2L, std::memory_order_release);
    state.phase.store(3, std::memory_order_release);
    if (!wait_for_phase(state.phase, 5, stop)) {
        return;
    }
    const bool writer_locked = InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(rw_word),
                                   static_cast<LONG>(0x80000001U),
                                   0L) == 0L;
    state.writer_acquired_exclusive.store(writer_locked, std::memory_order_release);
    state.phase.store(6, std::memory_order_release);
    if (!wait_for_phase(state.phase, 7, stop)) {
        if (writer_locked) {
            InterlockedExchange(reinterpret_cast<volatile LONG*>(rw_word), 0L);
        }
        return;
    }
    if (writer_locked) {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(rw_word), 0L);
    }
    state.phase.store(8, std::memory_order_release);
    while (!state.stress_done.load(std::memory_order_acquire) && !stop.stop_requested()) {
        if (InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(rw_word),
                static_cast<LONG>(0x80000001U),
                0L) == 0L) {
            InterlockedExchange(reinterpret_cast<volatile LONG*>(rw_word), 0L);
        }
    }
}

struct ProcessContentionState {
    RE::BSSpinLock spin_lock{};
    std::atomic<bool> holder_acquired{false};
    std::atomic<bool> contender_checked{false};
    std::atomic<bool> holder_released{false};
    std::atomic<bool> busy_declined{false};
    std::atomic<bool> worker_reacquired{false};
    std::atomic<bool> start_stress{false};
    std::atomic<int> active_holders{0};
    std::atomic<std::size_t> stress_iterations{0};
    std::atomic<std::size_t> lock_successes{0};
    std::atomic<bool> mutual_exclusion_ok{true};
};

void run_process_lock_contender(ProcessContentionState& state,
    const std::stop_token& stop) noexcept {
    const auto worker_tid = GetCurrentThreadId();
    if (!wait_for_flag(state.holder_acquired, stop)) {
        return;
    }
    state.busy_declined.store(!ui_capture_test::try_process_lock(&state.spin_lock, worker_tid),
        std::memory_order_relaxed);
    state.contender_checked.store(true, std::memory_order_release);
    if (!wait_for_flag(state.holder_released, stop)) {
        return;
    }
    if (ui_capture_test::try_process_lock(&state.spin_lock, worker_tid)) {
        const auto snapshot = inspect_spin_lock(state.spin_lock);
        state.worker_reacquired.store(snapshot.owner_thread == worker_tid &&
                                          snapshot.lock_word == 1U,
            std::memory_order_relaxed);
        ui_capture_test::release_process_lock(&state.spin_lock, worker_tid, &record_lock_failure);
    }
}

void run_process_lock_stress_worker(ProcessContentionState& state,
    std::size_t iterations_per_thread,
    const std::stop_token& stop) noexcept {
    if (!wait_for_flag(state.start_stress, stop)) {
        return;
    }
    const auto worker_tid = GetCurrentThreadId();
    for (std::size_t step = 0; step < iterations_per_thread && !stop.stop_requested(); ++step) {
        if (ui_capture_test::try_process_lock(&state.spin_lock, worker_tid)) {
            if (state.active_holders.fetch_add(1, std::memory_order_acq_rel) != 0) {
                state.mutual_exclusion_ok.store(false, std::memory_order_relaxed);
            }
            state.lock_successes.fetch_add(1U, std::memory_order_relaxed);
            if (state.active_holders.fetch_sub(1, std::memory_order_acq_rel) != 1) {
                state.mutual_exclusion_ok.store(false, std::memory_order_relaxed);
            }
            ui_capture_test::release_process_lock(&state.spin_lock,
                worker_tid,
                &record_lock_failure);
        }
        state.stress_iterations.fetch_add(1U, std::memory_order_relaxed);
    }
}
} // namespace

TEST_CASE("fallout4.native_ui_capture.ui_process_lock_concurrent_contention",
    "[component][fallout4][native_ui_capture]") {
    g_lock_failure_callbacks.store(0U, std::memory_order_relaxed);

    ProcessContentionState state{};
    const auto main_thread_id = GetCurrentThreadId();
    bool held_state_preserved{false};
    {
        std::jthread contender{
            [&](const std::stop_token& stop) noexcept { run_process_lock_contender(state, stop); }};
        const bool locked = ui_capture_test::try_process_lock(&state.spin_lock, main_thread_id);
        state.holder_acquired.store(true, std::memory_order_release);
        if (wait_for_flag(state.contender_checked, std::stop_token{})) {
            const auto held = inspect_spin_lock(state.spin_lock);
            held_state_preserved =
                locked && held.owner_thread == main_thread_id && held.lock_word == 1U;
        }
        if (locked) {
            ui_capture_test::release_process_lock(&state.spin_lock,
                main_thread_id,
                &record_lock_failure);
        }
        state.holder_released.store(true, std::memory_order_release);
        // Join without requesting cancellation.
        contender.join();
    }

    constexpr std::size_t thread_count = 4;
    constexpr std::size_t iterations_per_thread = 1500;
    {
        std::vector<std::jthread> workers;
        workers.reserve(thread_count);
        for (std::size_t worker = 0; worker < thread_count; ++worker) {
            workers.emplace_back([&](const std::stop_token& stop) noexcept {
                run_process_lock_stress_worker(state, iterations_per_thread, stop);
            });
        }
        state.start_stress.store(true, std::memory_order_release);
        for (auto& worker : workers) {
            worker.join();
        }
    }
    const auto final_spin = inspect_spin_lock(state.spin_lock);
    check(held_state_preserved && state.busy_declined.load(std::memory_order_relaxed) &&
              state.worker_reacquired.load(std::memory_order_relaxed) &&
              state.mutual_exclusion_ok.load(std::memory_order_relaxed) &&
              state.stress_iterations.load(std::memory_order_relaxed) ==
                  thread_count * iterations_per_thread &&
              state.lock_successes.load(std::memory_order_relaxed) > 0U &&
              final_spin.owner_thread == 0U && final_spin.lock_word == 0U &&
              g_lock_failure_callbacks.load(std::memory_order_relaxed) == 0U,
        "concurrent try_process_lock/release deterministically arbitrates contention, preserves "
        "mutual exclusion, and leaves clean final words");
}

TEST_CASE("fallout4.native_ui_capture.ui_read_count_concurrent_contention",
    "[component][fallout4][native_ui_capture]") {
    g_lock_failure_callbacks.store(0U, std::memory_order_relaxed);

    const auto state = std::make_unique<RwContentionState>();
    auto* const rw_word = &state->word;
    constexpr std::size_t stress_iterations = 3000;
    std::size_t completed_stress_iterations{0U};
    std::atomic<bool> rw_invariant_ok{true};
    bool initial_acquire_ok{false};
    std::uint32_t word_after_adapter_release{0U};
    bool adapter_blocked_by_writer{false};
    {
        std::jthread reader_thread{
            [&](const std::stop_token& stop) noexcept { run_engine_reader(*state, stop); }};
        std::jthread writer_thread{
            [&](const std::stop_token& stop) noexcept { run_engine_writer(*state, stop); }};

        initial_acquire_ok = ui_capture_test::try_read_count(rw_word);
        state->phase.store(1, std::memory_order_release);

        if (wait_for_phase(state->phase, 3, std::stop_token{})) {
            if (initial_acquire_ok) {
                ui_capture_test::release_read_count(rw_word, &record_lock_failure);
            }
            word_after_adapter_release = load_rw_word(rw_word);
            state->phase.store(4, std::memory_order_release);
        }

        if (wait_for_phase(state->phase, 6, std::stop_token{})) {
            adapter_blocked_by_writer =
                !ui_capture_test::try_read_count(rw_word) && load_rw_word(rw_word) == 0x80000001U;
            state->phase.store(7, std::memory_order_release);
        }

        if (wait_for_phase(state->phase, 8, std::stop_token{})) {
            for (std::size_t step = 0; step < stress_iterations; ++step) {
                if (ui_capture_test::try_read_count(rw_word)) {
                    const auto held = load_rw_word(rw_word);
                    if ((held & 0x80000000U) != 0U || (held & 0x0FFFFFFFU) == 0U) {
                        rw_invariant_ok.store(false, std::memory_order_relaxed);
                    }
                    ui_capture_test::release_read_count(rw_word, &record_lock_failure);
                }
                ++completed_stress_iterations;
            }
        }
        state->stress_done.store(true, std::memory_order_release);
        reader_thread.join();
        writer_thread.join();
    }
    const auto final_word = load_rw_word(rw_word);
    check(initial_acquire_ok && state->joined_word_observed.load(std::memory_order_acquire) == 2U &&
              state->writer_blocked_by_readers.load(std::memory_order_acquire) &&
              word_after_adapter_release == 1U &&
              state->writer_acquired_exclusive.load(std::memory_order_acquire) &&
              adapter_blocked_by_writer &&
              state->engine_reader_joins.load(std::memory_order_relaxed) > 0U &&
              completed_stress_iterations == stress_iterations &&
              rw_invariant_ok.load(std::memory_order_relaxed) && final_word == 0U &&
              g_lock_failure_callbacks.load(std::memory_order_relaxed) == 0U,
        "concurrent read-count CAS deterministically proves engine reader joining, writer "
        "exclusion, safe shared release, and linearizable contention");
}

TEST_CASE("fallout4.native_ui_capture.ui_read_count_corruption_closes_gate",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state{101};
    ui_capture_test::reset(101);
    ScopedTransitionGate gate{ui_capture_test::try_gate()};
    volatile std::uint32_t corrupted_count = 0x80000001U;
    ui_capture_test::release_read_count(&corrupted_count,
        [] noexcept { ui_capture_test::close_capture_without_gate(); });
    check(corrupted_count == 0x80000001U && ui_capture_test::closed() &&
              ui_capture_test::generation() == 0U,
        "invariant violation during lock release closes capture under held transition_gate without "
        "corrupting the engine writer bit");
    gate.release();
}

TEST_CASE("fallout4.native_ui_capture.ui_close_capture_without_gate_under_held_gate",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state{100};
    ui_capture_test::reset(100);
    ProductionFenceFixture fixture{};
    fixture.generation = 100U;
    fixture.initialize_state();
    ui_capture_test::set_original(
        +[](std::uint8_t, std::uint64_t entry_rax) noexcept { return entry_rax ^ callback_xor; });

    ScopedTransitionGate gate{ui_capture_test::try_gate()};
    check(ui_capture_test::generation() == 100U && !ui_capture_test::closed() &&
              ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100U),
        "transition gate is acquired for an open capture generation");

    ui_capture_test::close_capture_without_gate();
    check(ui_capture_test::closed() && ui_capture_test::generation() == 0U &&
              !ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100U),
        "emergency capture closure succeeds while transition_gate is held exclusively without "
        "deadlocking on SRWLOCK");
    gate.release();

    ScopedTransitionGate relock{ui_capture_test::try_gate()};
    check(ui_capture_test::closed() && ui_capture_test::generation() == 0U &&
              !ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100U),
        "outer gate remains unlockable/reacquirable while capture stays closed");
    relock.release();

    check(ui_capture_test::invoke(1U, 0x1234U) == (0x1234U ^ callback_xor) &&
              ui_capture_test::closed() && ui_capture_test::generation() == 0U &&
              ui_capture_test::in_flight() == 0U,
        "subsequent transition entry stays permanently closed after emergency closure");
}

} // namespace fallout4_adapter::tests::ui_capture
