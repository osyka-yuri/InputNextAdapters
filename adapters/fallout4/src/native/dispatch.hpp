#pragma once

#include "native/ui_capture.hpp"
#include "native/ui_capture_abi.hpp"

#include <REL/ASM.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace RE {
class Main;
}

namespace fallout4_adapter::native {
inline constexpr std::size_t native_dispatch_pool_size = 128;
inline constexpr std::uintptr_t native_dispatch_helper_rva = 0xBD50C0;
inline constexpr std::uintptr_t native_dispatch_manager_slot_rva = 0x32A12F0;
inline constexpr std::uintptr_t native_dispatch_main_call_rva = 0xC3151E;
inline constexpr std::uintptr_t native_dispatch_main_message_loop_rva = 0xC336B0;
inline constexpr std::uintptr_t native_dispatch_remap_button_rva = 0x166ED30;
inline constexpr std::array<std::uint8_t, 8> native_dispatch_helper_guard{
    0x48,
    0x83,
    0xEC,
    0x28,
    0x84,
    0xC9,
    0x74,
    0x27,
};
inline constexpr std::array<std::uint8_t, 13> native_dispatch_remap_guard{
    0x48,
    0x89,
    0x5C,
    0x24,
    0x10,
    0x56,
    0x57,
    0x41,
    0x56,
    0x48,
    0x83,
    0xEC,
    0x40,
};
inline constexpr std::array<std::uint8_t, 7> native_dispatch_main_arguments{
    0x48,
    0x8B,
    0x0D,
    0x12,
    0xBC,
    0x6A,
    0x02,
};
inline constexpr std::array<std::uint8_t, 5> native_dispatch_main_original_call{
    0xE8,
    0x8D,
    0x21,
    0x00,
    0x00,
};

enum class NativeDispatchInstall : std::uint8_t {
    unavailable_before_attempt,
    ready,
    retained_disabled
};
enum class NativeDispatchSite : std::uint8_t { helper, remap_button, main_message_loop };
enum class NativeDispatchAttemptResult : std::uint8_t {
    ready,
    retained_disabled,
    fatal_control_flow
};

using MainObservationCallback = void (*)() noexcept;

// Observed machine-call contract on Fallout 4 executable 1.11.240.0:
// Main::Run loop-head CALL RVA 0xC3151E -> message-pump callee RVA 0xC336B0.
// RCX contains RE::Main*. The callee has no consumed return value at this site.
// This is a verified call-site ABI, not a reconstructed CommonLib declaration.
using MainMessageLoopOriginal = void (*)(RE::Main* main);

struct NativeDispatchLayout {
    UiCaptureShimLayout helper{};
    std::uintptr_t main_relay{};
    std::uintptr_t remap_cell{};
    std::uintptr_t remap_gateway{};
    std::size_t bytes_used{};
};

struct NativeDispatchTargets {
    std::uintptr_t helper_interceptor{};
    std::uintptr_t helper_continuation{};
    std::uintptr_t main_wrapper{};
    std::uintptr_t remap_interceptor{};
    std::uintptr_t remap_continuation{};
};

[[nodiscard]] inline bool make_native_dispatch_layout(std::uintptr_t allocation_address,
    std::size_t allocation_size,
    NativeDispatchLayout& layout) noexcept {
    layout = {};
    if (allocation_address == 0 || allocation_size < native_dispatch_pool_size ||
        allocation_address > UINTPTR_MAX - native_dispatch_pool_size ||
        allocation_address > UINTPTR_MAX - 7U) {
        return false;
    }
    const auto aligned_cell = (allocation_address + 7U) & ~std::uintptr_t{7U};
    if (aligned_cell < allocation_address || aligned_cell > UINTPTR_MAX - 92U) {
        return false;
    }
    const auto used = static_cast<std::size_t>(aligned_cell - allocation_address) + 92U;
    if (used > allocation_size || used > native_dispatch_pool_size) {
        return false;
    }
    layout.main_relay = aligned_cell + 48U;
    layout.remap_cell = aligned_cell + 64U;
    layout.remap_gateway = aligned_cell + 72U;
    layout.bytes_used = used;
    return true;
}

[[nodiscard]] inline bool emit_native_dispatch_resources(std::span<std::byte> allocation,
    std::uintptr_t allocation_address,
    const NativeDispatchTargets& targets,
    NativeDispatchLayout& layout) noexcept {
    if (allocation.size() < native_dispatch_pool_size ||
        !make_native_dispatch_layout(allocation_address, allocation.size(), layout) ||
        targets.main_wrapper == 0 || targets.remap_interceptor == 0 ||
        targets.remap_continuation == 0) {
        return false;
    }

    std::fill_n(allocation.begin(), native_dispatch_pool_size, std::byte{});
    if (!emit_ui_capture_shims(allocation.first(ui_capture_branch_pool_size),
            allocation_address,
            targets.helper_interceptor,
            targets.helper_continuation,
            layout.helper)) {
        layout = {};
        return false;
    }

    const REL::ASM::JMP14 main_jump{targets.main_wrapper};
    const auto main_offset = static_cast<std::size_t>(layout.main_relay - allocation_address);
    std::memcpy(allocation.data() + main_offset, &main_jump, sizeof(main_jump));

    const auto remap_cell_offset = static_cast<std::size_t>(layout.remap_cell - allocation_address);
    const auto remap_gateway_offset =
        static_cast<std::size_t>(layout.remap_gateway - allocation_address);
    const auto remap_target = static_cast<std::uint64_t>(targets.remap_interceptor);
    std::memcpy(allocation.data() + remap_cell_offset, &remap_target, sizeof(remap_target));

    constexpr std::array<std::uint8_t, 6> remap_prologue{0x48, 0x89, 0x5C, 0x24, 0x10, 0x56};
    const REL::ASM::JMP14 remap_jump{targets.remap_continuation};
    std::memcpy(allocation.data() + remap_gateway_offset,
        remap_prologue.data(),
        remap_prologue.size());
    std::memcpy(allocation.data() + remap_gateway_offset + remap_prologue.size(),
        &remap_jump,
        sizeof(remap_jump));
    return true;
}

[[nodiscard]] inline bool emit_native_dispatch_call5(std::span<std::byte> output,
    std::uintptr_t instruction_address,
    std::uintptr_t target) noexcept {
    if (output.size() < 5U || instruction_address == 0 || target == 0 ||
        instruction_address > UINTPTR_MAX - 5U) {
        return false;
    }
    const auto next_instruction = instruction_address + 5U;
    const auto forward = target >= next_instruction;
    const auto magnitude = forward ? target - next_instruction : next_instruction - target;
    constexpr auto negative_limit = static_cast<std::uintptr_t>(INT32_MAX) + 1U;
    if ((forward && magnitude > static_cast<std::uintptr_t>(INT32_MAX)) ||
        (!forward && magnitude > negative_limit)) {
        return false;
    }
    std::int32_t displacement{};
    if (forward) {
        displacement = static_cast<std::int32_t>(magnitude);
    } else if (magnitude == negative_limit) {
        displacement = INT32_MIN;
    } else {
        displacement = -static_cast<std::int32_t>(magnitude);
    }
    output[0] = std::byte{0xE8};
    std::memcpy(output.data() + 1U, &displacement, sizeof(displacement));
    return true;
}

[[nodiscard]] inline std::uintptr_t decode_native_dispatch_call5_target(
    std::span<const std::byte> bytes,
    std::uintptr_t instruction_address) noexcept {
    if (bytes.size() < 5U || bytes[0] != std::byte{0xE8} ||
        instruction_address > UINTPTR_MAX - 5U) {
        return 0;
    }
    std::int32_t displacement{};
    std::memcpy(&displacement, bytes.data() + 1U, sizeof(displacement));
    const auto next_instruction = instruction_address + 5U;
    if (displacement >= 0) {
        const auto distance = static_cast<std::uintptr_t>(displacement);
        return next_instruction > UINTPTR_MAX - distance ? 0 : next_instruction + distance;
    }
    const auto distance = static_cast<std::uintptr_t>(-(static_cast<std::int64_t>(displacement)));
    return distance > next_instruction ? 0 : next_instruction - distance;
}

[[nodiscard]] inline bool native_dispatch_original_guards_match(std::span<const std::byte> helper,
    std::span<const std::byte> remap,
    std::span<const std::byte> main_arguments,
    std::span<const std::byte> main_call,
    std::uintptr_t main_call_address,
    std::uintptr_t image) noexcept {
    if (helper.size() < native_dispatch_helper_guard.size() ||
        remap.size() < native_dispatch_remap_guard.size() ||
        main_arguments.size() < native_dispatch_main_arguments.size() ||
        main_call.size() < native_dispatch_main_original_call.size() || image == 0) {
        return false;
    }

    const auto original_guards_match = [&] {
        return std::memcmp(helper.data(),
                   native_dispatch_helper_guard.data(),
                   native_dispatch_helper_guard.size()) == 0 &&
               std::memcmp(remap.data(),
                   native_dispatch_remap_guard.data(),
                   native_dispatch_remap_guard.size()) == 0 &&
               std::memcmp(main_arguments.data(),
                   native_dispatch_main_arguments.data(),
                   native_dispatch_main_arguments.size()) == 0;
    };
    const auto main_call_matches = [&] {
        return main_call_address <= UINTPTR_MAX - 5U &&
               image <= UINTPTR_MAX - native_dispatch_main_message_loop_rva &&
               std::memcmp(main_call.data(),
                   native_dispatch_main_original_call.data(),
                   native_dispatch_main_original_call.size()) == 0 &&
               decode_native_dispatch_call5_target(main_call, main_call_address) ==
                   image + native_dispatch_main_message_loop_rva;
    };
    return original_guards_match() && main_call_matches();
}

struct NativeDispatchAttemptInspection {
    bool all_sites_safe{};
    bool expected_prefix{};
    bool write_succeeded{};
    bool instruction_cache_flushed{};
};

[[nodiscard]] constexpr NativeDispatchAttemptResult decide_native_dispatch_attempt(
    NativeDispatchAttemptInspection inspection) noexcept {
    if (!inspection.all_sites_safe) {
        return NativeDispatchAttemptResult::fatal_control_flow;
    }
    if (!inspection.expected_prefix || !inspection.write_succeeded ||
        !inspection.instruction_cache_flushed) {
        return NativeDispatchAttemptResult::retained_disabled;
    }
    return NativeDispatchAttemptResult::ready;
}

template <class Operations>
[[nodiscard]] NativeDispatchInstall perform_native_dispatch_install(
    Operations& operations) noexcept {
    if (!operations.preflight() || !operations.prepare() || !operations.pin_and_publish() ||
        !operations.precommit()) {
        return NativeDispatchInstall::unavailable_before_attempt;
    }

    constexpr std::array sites{
        NativeDispatchSite::helper,
        NativeDispatchSite::remap_button,
        NativeDispatchSite::main_message_loop,
    };
    std::uint8_t expected_owned_mask{};
    for (const auto site : sites) {
        expected_owned_mask =
            static_cast<std::uint8_t>(expected_owned_mask | (1U << std::to_underlying(site)));
        const auto result =
            decide_native_dispatch_attempt(operations.attempt_once(site, expected_owned_mask));
        if (result == NativeDispatchAttemptResult::ready) {
            continue;
        }
        operations.retain_disabled();
        if (result == NativeDispatchAttemptResult::fatal_control_flow) {
            operations.fatal_control_flow();
            return NativeDispatchInstall::retained_disabled;
        }
        return NativeDispatchInstall::retained_disabled;
    }
    return NativeDispatchInstall::ready;
}

[[nodiscard]] NativeDispatchInstall install_native_dispatch() noexcept;
[[nodiscard]] bool activate_main_observation(MainObservationCallback callback) noexcept;
void main_message_loop_wrapper(RE::Main* main);
[[nodiscard]] bool is_main_observation_active() noexcept;
[[nodiscard]] NativeDispatchInstall native_dispatch_status() noexcept;

} // namespace fallout4_adapter::native
