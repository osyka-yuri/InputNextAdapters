#include "native/ui_capture.hpp"
#include "support.hpp"
#include "ui_capture_test.hpp"

#include <catch2/catch_test_macros.hpp>
#undef FAIL

#include <RE/I/INPUT_DEVICE.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>

namespace fallout4_adapter::tests::ui_capture {

TEST_CASE("fallout4.native_ui_capture.ui_remap_forwarding_generation",
    "[component][fallout4][native_ui_capture]") {
    const ScopedCaptureState capture_state;
    constexpr std::uint64_t initial_generation = 80;
    ui_capture_test::reset(initial_generation);
    ui_capture_test::set_remap_original(&controlled_remap_original);
    remap_calls.store(0, std::memory_order_relaxed);
    remap_result.store(true, std::memory_order_release);
    nested_remap.store(false, std::memory_order_release);
    block_remap.store(false, std::memory_order_release);

    auto* control_map = fake_control_map_address();
    const auto& event_name = empty_remap_event_argument();
    const auto device = RE::INPUT_DEVICE::kGamepad;
    check(ui_capture_test::invoke_remap(control_map, event_name, device, 0x24),
        "production RemapButton wrapper preserves a true native return");
    check(remap_calls.load(std::memory_order_relaxed) == 1,
        "RemapButton original executes exactly once");
    check(remap_last_map.load(std::memory_order_relaxed) == control_map,
        "RemapButton wrapper forwards the exact this pointer");
    check(remap_last_event.load(std::memory_order_relaxed) == std::addressof(event_name),
        "RemapButton wrapper forwards the same fixed-string reference");
    check(remap_last_device.load(std::memory_order_relaxed) == device &&
              remap_last_button.load(std::memory_order_relaxed) == 0x24,
        "RemapButton wrapper preserves device and button arguments");
    check(remap_original_generation.load(std::memory_order_relaxed) == initial_generation + 1U &&
              remap_original_in_flight.load(std::memory_order_relaxed) == 1U,
        "RemapButton advances generation and marks itself in flight before native mutation");
    check(ui_capture_test::in_flight() == 0,
        "normal RemapButton return releases its tracked writer count");

    remap_result.store(false, std::memory_order_release);
    check(!ui_capture_test::invoke_remap(control_map, event_name, device, 0x25),
        "production RemapButton wrapper preserves a false native return");
    check(remap_calls.load(std::memory_order_relaxed) == 2,
        "false native return still forwards exactly once");
    check(ui_transition_generation() == initial_generation + 2U,
        "each normal remap advances generation once");
}

TEST_CASE("fallout4.native_ui_capture.ui_remap_nested_forwarding",
    "[component][fallout4][native_ui_capture]") {
    const ScopedCaptureState capture_state;
    auto* control_map = fake_control_map_address();
    const auto& event_name = empty_remap_event_argument();
    constexpr auto device = RE::INPUT_DEVICE::kKeyboard;

    ui_capture_test::reset(100);
    ui_capture_test::set_remap_original(&controlled_remap_original);
    remap_calls.store(0, std::memory_order_relaxed);
    remap_result.store(true, std::memory_order_release);
    nested_remap_result.store(false, std::memory_order_release);
    nested_remap.store(true, std::memory_order_release);
    check(ui_capture_test::invoke_remap(control_map, event_name, device, 7),
        "outer nested RemapButton preserves its native return");
    check(remap_calls.load(std::memory_order_relaxed) == 2,
        "nested RemapButton forwards both original calls once");
    check(nested_remap_result.load(std::memory_order_acquire),
        "nested RemapButton preserves its own return value");
    check(ui_capture_test::in_flight() == 0,
        "nested RemapButton counts drain only after both returns");
    check(ui_transition_generation() == 102, "nested remaps independently advance the generation");
}

TEST_CASE("fallout4.native_ui_capture.ui_remap_concurrent_forwarding",
    "[component][fallout4][native_ui_capture]") {
    const ScopedCaptureState capture_state;
    auto* control_map = fake_control_map_address();
    const auto& event_name = empty_remap_event_argument();
    constexpr auto device = RE::INPUT_DEVICE::kKeyboard;
    ui_capture_test::reset(200);
    ui_capture_test::set_remap_original(&controlled_remap_original);
    nested_remap.store(false, std::memory_order_release);
    remap_calls.store(0, std::memory_order_relaxed);
    block_remap.store(true, std::memory_order_release);
    remap_entered.store(0, std::memory_order_release);
    release_remap.store(false, std::memory_order_release);
    bool first_result{};
    bool second_result{};
    ScopedThread first{[] { release_remap.store(true, std::memory_order_release); },
        [&] { first_result = ui_capture_test::invoke_remap(control_map, event_name, device, 10); }};
    check(wait_until([] { return remap_entered.load(std::memory_order_acquire) >= 1; }),
        "first remap reaches its blocked original");
    ScopedThread second{[] { release_remap.store(true, std::memory_order_release); },
        [&] {
            second_result = ui_capture_test::invoke_remap(control_map, event_name, device, 11);
        }};
    check(wait_until([] { return remap_entered.load(std::memory_order_acquire) >= 2; }),
        "second remap overlaps the first native call");
    check(ui_capture_test::in_flight() == 2,
        "concurrent public remaps remain ineligible while both originals run");
    const bool gate_available = ui_capture_test::try_gate();
    check(gate_available, "private transition gate is free during native RemapButton work");
    if (gate_available) {
        ui_capture_test::release_gate();
    }
    ProductionFenceFixture fixture{};
    fixture.initialize_state();
    check(!ui_capture_test::eligible(fixture.image, fixture_static_offsets, 200),
        "the actual capture eligibility tuple rejects the old generation during remapping");
    release_remap.store(true, std::memory_order_release);
    first.join();
    second.join();
    block_remap.store(false, std::memory_order_release);
    check(first_result && second_result,
        "overlapping remap wrappers preserve both original results");
    check(ui_capture_test::in_flight() == 0 && ui_transition_generation() == 202,
        "concurrent remap counts drain with monotonic generation");
}

TEST_CASE("fallout4.native_ui_capture.ui_remap_closed_forwarding",
    "[component][fallout4][native_ui_capture]") {
    const ScopedCaptureState capture_state;
    auto* control_map = fake_control_map_address();
    const auto& event_name = empty_remap_event_argument();
    constexpr auto device = RE::INPUT_DEVICE::kKeyboard;
    ui_capture_test::reset(300);
    ui_capture_test::set_remap_original(&controlled_remap_original);
    remap_calls.store(0, std::memory_order_relaxed);
    remap_result.store(false, std::memory_order_release);
    ui_capture_test::set_generation(0);
    check(!ui_capture_test::invoke_remap(control_map, event_name, device, 12),
        "closed capture still forwards the public native writer and preserves false");
    check(remap_calls.load(std::memory_order_relaxed) == 1,
        "refused transition entry never skips the native remap");
    check(ui_capture_test::closed() && ui_transition_generation() == 0,
        "closed remap entry remains permanently neutral");
}

TEST_CASE("fallout4.native_ui_capture.ui_remap_generation_overflow",
    "[component][fallout4][native_ui_capture]") {
    const ScopedCaptureState capture_state;
    auto* control_map = fake_control_map_address();
    const auto& event_name = empty_remap_event_argument();

    ui_capture_test::reset(std::numeric_limits<std::uint64_t>::max());
    ui_capture_test::set_remap_original(&controlled_remap_original);
    remap_calls.store(0, std::memory_order_relaxed);
    remap_result.store(true, std::memory_order_release);
    check(ui_capture_test::invoke_remap(control_map, event_name, RE::INPUT_DEVICE::kGamepad, 4),
        "generation-overflow refusal still forwards the native RemapButton call");
    check(remap_calls.load(std::memory_order_relaxed) == 1,
        "overflow refusal preserves exactly-once forwarding");
    check(ui_transition_generation() == 0 && ui_capture_test::closed(),
        "remap generation overflow closes capture permanently");
}

TEST_CASE("fallout4.native_ui_capture.ui_remap_exception_closure",
    "[component][fallout4][native_ui_capture]") {
    const ScopedCaptureState capture_state;
    auto* control_map = fake_control_map_address();
    const auto& event_name = empty_remap_event_argument();
    ui_capture_test::reset(600);
    ui_capture_test::set_remap_original(&throwing_remap_original);
    bool threw{};
    try {
        static_cast<void>(
            ui_capture_test::invoke_remap(control_map, event_name, RE::INPUT_DEVICE::kGamepad, 4));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "RemapButton original exception propagates through the typed wrapper");
    check(ui_transition_generation() == 0 && ui_capture_test::closed(),
        "RemapButton exception closes capture permanently");
    check(ui_capture_test::in_flight() == 1,
        "abnormal RemapButton return conservatively retains the tracked count");
}

} // namespace fallout4_adapter::tests::ui_capture
