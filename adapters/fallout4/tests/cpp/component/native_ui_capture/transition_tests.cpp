#include "native/ui_capture.hpp"
#include "support.hpp"
#include "ui_capture_test.hpp"

#include <catch2/catch_test_macros.hpp>
#undef FAIL

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>

namespace fallout4_adapter::tests::ui_capture {

TEST_CASE("fallout4.native_ui_capture.ui_transition_count_trailing_false",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    ui_capture_test::reset(10);
    ui_capture_test::set_original(&controlled_original);
    block_original.store(true, std::memory_order_release);
    release_original.store(false, std::memory_order_release);
    original_entered.store(false, std::memory_order_release);
    original_calls.store(0, std::memory_order_relaxed);

    std::uint64_t result{};
    ScopedThread delayed_true{[] { release_original.store(true, std::memory_order_release); },
        [&] { result = ui_capture_test::invoke(0xFEU, 0x12AABBCCDDEEFF00ULL); }};
    check(wait_until([] { return original_entered.load(std::memory_order_acquire); }),
        "delayed true helper reaches its original body");
    check(ui_capture_test::in_flight() == 1,
        "transition count remains held throughout a delayed true helper");
    check(ui_transition_generation() == 11,
        "true helper entry advances generation before calling the original body");
    check(last_original_cl.load(std::memory_order_relaxed) == 0xFEU,
        "noncanonical CL is forwarded unchanged to original");
    check(last_original_rax.load(std::memory_order_relaxed) == 0x12AABBCCDDEEFF00ULL,
        "seeded full-width RAX reaches original");
    check(last_original_in_flight.load(std::memory_order_relaxed) == 1,
        "original helper runs outside the private gate while counted");

    FenceProbe while_true{};
    while_true.require_quiescent = true;
    check(!with_ui_capture_fences(while_true, [] { return true; }),
        "scalar copy declines a delayed true transition");
    check(while_true.eligibility_count == 1 && while_true.copy_count == 0,
        "true-transition rejection reaches eligibility but never copies");

    release_original.store(true, std::memory_order_release);
    delayed_true.join();
    check(result == (0x12AABBCCDDEEFF00ULL ^ callback_xor),
        "full-width original helper result survives exit bookkeeping");
    check(ui_capture_test::in_flight() == 0, "normal true return releases the transition count");

    block_original.store(true, std::memory_order_release);
    release_original.store(false, std::memory_order_release);
    original_entered.store(false, std::memory_order_release);
    ScopedThread delayed_false{[] { release_original.store(true, std::memory_order_release); },
        [&] { result = ui_capture_test::invoke(0x00U, 0x8F723A19D0C4B65EULL); }};
    check(wait_until([] { return original_entered.load(std::memory_order_acquire); }),
        "delayed false helper reaches its trailing original body");
    check(ui_capture_test::in_flight() == 1,
        "false helper stays counted after its simulated state-zero publication");
    check(ui_transition_generation() == 12, "false helper entry also advances generation");

    FenceProbe trailing_false{};
    trailing_false.require_quiescent = true;
    check(!with_ui_capture_fences(trailing_false, [] { return true; }),
        "reader rejects trailing false wait while helper is counted");
    check(trailing_false.copy_count == 0, "trailing false path cannot admit a scalar copy");
    release_original.store(true, std::memory_order_release);
    delayed_false.join();
    check(result == (0x8F723A19D0C4B65EULL ^ callback_xor),
        "false helper return preserves the callback's full-width result");
    check(ui_capture_test::in_flight() == 0,
        "false transition count releases only after original normal return");
    block_original.store(false, std::memory_order_release);

    static_cast<void>(ui_capture_test::invoke(0x02U, 0x0102030405060708ULL));
    static_cast<void>(ui_capture_test::invoke(0xFFU, 0xFFEEDDCCBBAA9988ULL));
    check(ui_transition_generation() == 14, "no-op helper entries each advance generation");
    check(original_calls.load(std::memory_order_relaxed) == 4,
        "both delayed calls and both no-op calls forward exactly once");
}

TEST_CASE("fallout4.native_ui_capture.ui_transition_nested_calls",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;

    ui_capture_test::reset(40);
    ui_capture_test::set_original(&controlled_original);
    nested_original.store(true, std::memory_order_release);
    last_nested_result.store(0, std::memory_order_relaxed);
    const auto outer_result = ui_capture_test::invoke(0x01U, 0xF1E2D3C4B5A69788ULL);
    nested_original.store(false, std::memory_order_release);
    check(outer_result == (0xF1E2D3C4B5A69788ULL ^ callback_xor),
        "outer nested transition preserves its own full-width return");
    check(last_nested_result.load(std::memory_order_acquire) ==
              (0xE781239ABCD456F0ULL ^ callback_xor),
        "nested transition preserves its distinct full-width return");
    check(ui_transition_generation() == 42, "nested helper entries each increment the generation");
    check(ui_capture_test::in_flight() == 0,
        "nested transition count drains to zero on normal returns");
}

TEST_CASE("fallout4.native_ui_capture.ui_transition_generation_overflow",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    ui_capture_test::reset(std::numeric_limits<std::uint64_t>::max() - 1U);
    ui_capture_test::set_original(&controlled_original);
    const auto first = ui_capture_test::invoke(0x02U, 0x1122334455667788ULL);
    check(first == (0x1122334455667788ULL ^ callback_xor),
        "last representable transition still forwards normally");
    check(ui_transition_generation() == std::numeric_limits<std::uint64_t>::max(),
        "generation reaches maximum without wrapping");
    const auto second = ui_capture_test::invoke(0x00U, 0x8877665544332211ULL);
    check(second == (0x8877665544332211ULL ^ callback_xor),
        "generation overflow closes capture while preserving original forwarding");
    check(ui_transition_generation() == 0, "generation overflow becomes permanently closed zero");
    check(ui_capture_test::in_flight() == 0,
        "generation overflow does not leave a wrapped or counted transition");
}

TEST_CASE("fallout4.native_ui_capture.ui_transition_concurrent_calls",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;

    ui_capture_test::reset(70);
    ui_capture_test::set_original(&controlled_original);
    block_original.store(true, std::memory_order_release);
    release_original.store(false, std::memory_order_release);
    original_entered.store(false, std::memory_order_release);
    original_calls.store(0, std::memory_order_relaxed);
    std::uint64_t first_result{};
    std::uint64_t second_result{};
    ScopedThread first{[] { release_original.store(true, std::memory_order_release); },
        [&] { first_result = ui_capture_test::invoke(0x01U, 0x1020304050607080ULL); }};
    check(wait_until([] { return original_entered.load(std::memory_order_acquire); }),
        "first overlapping original call enters");
    ScopedThread second{[] { release_original.store(true, std::memory_order_release); },
        [&] { second_result = ui_capture_test::invoke(0xFEU, 0x8070605040302010ULL); }};
    check(wait_until([] { return original_calls.load(std::memory_order_acquire) == 2; }),
        "second overlapping original call enters");
    check(ui_capture_test::in_flight() == 2,
        "overlapping calls share the monotonic in-flight count");
    check(ui_transition_generation() == 72,
        "both overlapping entries advance the generation independently");
    release_original.store(true, std::memory_order_release);
    first.join();
    second.join();
    block_original.store(false, std::memory_order_release);
    check(first_result == (0x1020304050607080ULL ^ callback_xor),
        "first overlapping return is preserved");
    check(second_result == (0x8070605040302010ULL ^ callback_xor),
        "second overlapping return is preserved");
    check(ui_capture_test::in_flight() == 0,
        "overlapping normal returns drain the production count");
}

TEST_CASE("fallout4.native_ui_capture.ui_transition_exception_closure",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    ui_capture_test::reset(80);
    ui_capture_test::set_original(&throwing_original);
    bool escaped{};
    try {
        static_cast<void>(ui_capture_test::invoke(0x00U, 0x9988776655443322ULL));
    } catch (const std::runtime_error&) {
        escaped = true;
    }
    check(escaped, "exception from the seeded original remains visible to its caller");
    check(ui_capture_test::closed() && ui_transition_generation() == 0,
        "exception escape closes capture permanently");
    check(ui_capture_test::in_flight() == 1, "exception escape never performs an unwind decrement");
}

TEST_CASE("fallout4.native_ui_capture.ui_transition_count_saturation",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    ui_capture_test::reset(90);
    ui_capture_test::set_in_flight(std::numeric_limits<std::uint64_t>::max());
    ui_capture_test::set_original(&controlled_original);
    const auto saturated = ui_capture_test::invoke(0x00U, 0x0101010101010101ULL);
    check(saturated == (0x0101010101010101ULL ^ callback_xor),
        "count saturation still forwards the original exactly once");
    check(ui_capture_test::closed() && ui_transition_generation() == 0,
        "count saturation permanently closes capture");
    check(ui_capture_test::in_flight() == std::numeric_limits<std::uint64_t>::max(),
        "saturated count never wraps");
}

TEST_CASE("fallout4.native_ui_capture.ui_transition_gate_serialization",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    ui_capture_test::reset(60);
    ui_capture_test::set_original(&controlled_original);
    block_original.store(true, std::memory_order_release);
    release_original.store(false, std::memory_order_release);
    original_entered.store(false, std::memory_order_release);

    const bool gate_owned = ui_capture_test::try_gate();
    check(gate_owned, "test thread holds the actual private transition gate exclusively");
    if (!gate_owned) {
        return;
    }
    ScopedTransitionGate held_gate{gate_owned};

    std::atomic<bool> helper_attempting{};
    std::uint64_t result{};
    ScopedThread helper{[&] {
                            release_original.store(true, std::memory_order_release);
                            held_gate.release();
                        },
        [&] {
            helper_attempting.store(true, std::memory_order_release);
            result = ui_capture_test::invoke(0x7BU, 0xDFE183BCA9652047ULL);
        }};
    check(wait_until([&] { return helper_attempting.load(std::memory_order_acquire); }),
        "helper thread reaches the invocation immediately before production transition entry");
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    check(!original_entered.load(std::memory_order_acquire),
        "held production gate delays entry into the helper body");
    held_gate.release();
    check(wait_until([] { return original_entered.load(std::memory_order_acquire); }),
        "helper enters the original body after the owning thread releases the gate");
    release_original.store(true, std::memory_order_release);
    helper.join();
    check(result == (0xDFE183BCA9652047ULL ^ callback_xor),
        "delayed helper returns the forwarded full-width original result");
    check(ui_capture_test::in_flight() == 0,
        "delayed gate acquisition leaves no counted transition");
    // Deterministic thread interleaving:
    // Case A: Gate acquisition on pipeline instance A, reset() on another thread, and release_gate()
    // on the original instance.
    const bool interleave_gate_owned = ui_capture_test::try_gate();
    check(interleave_gate_owned, "interleaving gate acquisition succeeds on initial pipeline");
    if (interleave_gate_owned) {
        std::atomic<bool> reset_done{};
        ScopedThread reset_worker{[] {},
            [&] {
                ui_capture_test::reset(120);
                reset_done.store(true, std::memory_order_release);
            }};
        check(wait_until([&] { return reset_done.load(std::memory_order_acquire); }),
            "worker completes reset");
        reset_worker.join();

        check(ui_capture_test::generation() == 120,
            "active pipeline reflects generation from reset");

        // release_gate() must release the ORIGINAL pipeline instance (where try_gate was acquired)
        // rather than the newly installed active pipeline, preserving Win32 SRWLOCK thread affinity.
        ui_capture_test::release_gate();

        // The new active pipeline's gate must be free and acquirable.
        const bool new_gate_available = ui_capture_test::try_gate();
        check(new_gate_available, "new pipeline transition gate is free and acquirable");
        if (new_gate_available) {
            ui_capture_test::release_gate();
        }
    }

    // Case B: In-flight invocation on pipeline instance A, reset() on another thread, and completion
    // of invocation on the original instance.
    ui_capture_test::set_original(&controlled_original);
    block_original.store(true, std::memory_order_release);
    release_original.store(false, std::memory_order_release);
    original_entered.store(false, std::memory_order_release);

    std::atomic<bool> invoke_started{};
    std::uint64_t interleave_invoke_result{};
    ScopedThread invoke_worker{[&] { release_original.store(true, std::memory_order_release); },
        [&] {
            invoke_started.store(true, std::memory_order_release);
            interleave_invoke_result = ui_capture_test::invoke(0x3CU, 0x1122334455667788ULL);
        }};

    check(wait_until([&] { return original_entered.load(std::memory_order_acquire); }),
        "worker enters the original body on the old pipeline instance");

    // Concurrently reset the active pipeline from the main thread while worker is inside the callback
    ui_capture_test::reset(250);
    check(ui_capture_test::generation() == 250,
        "active pipeline generation updated to 250 during concurrent execution");

    // Unblock the worker thread to finish execution on the old pipeline instance
    release_original.store(true, std::memory_order_release);
    invoke_worker.join();

    check(interleave_invoke_result == (0x1122334455667788ULL ^ callback_xor),
        "in-flight invocation completes safely on the original pipeline despite concurrent reset");
    check(ui_capture_test::in_flight() == 0, "active pipeline retains no in-flight transitions");
}

TEST_CASE("fallout4.native_ui_capture.ui_transition_owned_protocol",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    constexpr std::uint64_t initial_generation = 300;
    ui_capture_test::reset(initial_generation);
    OwnedSuccessfulProtocolFixture fixture{};
    fixture.capture.generation = initial_generation;
    fixture.reset_stale_completion_before_loading_state();
    check(fixture.startup_reset_preceded_state_load && !fixture.completion_signaled,
        "owned startup fixture resets a stale completion signal before loading state");
    check(!fixture.capture.eligible(),
        "state two observed after the startup reset remains ineligible until normal completion");

    active_successful_protocol = &fixture;
    ui_capture_test::set_original(&successful_protocol_original);
    constexpr std::uint64_t seed = 0x123456789ABCDEF0ULL;
    check(ui_capture_test::invoke(1U, seed) == (seed ^ callback_xor),
        "owned successful true protocol forwards the original exactly once");
    check(fixture.completion_signaled && ui_transition_generation() == initial_generation + 1U,
        "owned true completion follows the counted transition generation advance");
    check(!fixture.capture.eligible(),
        "completed true protocol state two remains outside scalar capture eligibility");

    fixture.completion_signaled = false;
    check(ui_capture_test::invoke(0U, seed) == (seed ^ callback_xor),
        "owned successful trailing-false protocol forwards the original exactly once");
    check(fixture.trailing_false_rejected_while_counted,
        "state zero published by trailing false is rejected until the in-flight transition drains");
    check(fixture.completion_signaled && ui_transition_generation() == initial_generation + 2U &&
              ui_capture_test::in_flight() == 0,
        "trailing false signals its owned completion only after state zero publication and count "
        "release");
    check(fixture.capture.eligible(),
        "state zero becomes eligible after the successful trailing-false transition completes");

    fixture.capture.initialize_state({.state = 0, .shutdown = 1, .running = 0});
    check(!fixture.capture.eligible(),
        "owned teardown closes scalar eligibility before process-life reinitialization");
    constexpr std::uint64_t reinitialized_generation = 400;
    ui_capture_test::reset(reinitialized_generation);
    fixture.capture.generation = reinitialized_generation;
    fixture.capture.initialize_state({.state = 0, .shutdown = 0, .running = 1});
    fixture.completion_signaled = false;
    check(fixture.capture.eligible(),
        "owned teardown/reinitialization restores only the reset generation and clean worker "
        "state");
    active_successful_protocol = nullptr;
    ui_capture_test::set_original(&controlled_original);
}

} // namespace fallout4_adapter::tests::ui_capture
