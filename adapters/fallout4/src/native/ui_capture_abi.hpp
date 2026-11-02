#pragma once

#include <REL/ASM.h>
#include <REL/Version.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace fallout4_adapter::native {
inline constexpr std::size_t ui_capture_branch_pool_size = 64;

[[nodiscard]] constexpr bool supported_ui_capture_host(REL::Version runtime,
    REL::Version f4se,
    std::uint32_t messaging_interface,
    std::uint32_t trampoline_interface) noexcept {
    return runtime == REL::Version{1, 11, 240, 0} && f4se == REL::Version{0, 7, 9, 0} &&
           messaging_interface == 1 && trampoline_interface == 1;
}

struct UiCaptureShimLayout {
    std::uintptr_t pointer_cell{};
    std::uintptr_t original_gateway{};
    std::uintptr_t entry_shim{};
    std::size_t bytes_used{};
};

[[nodiscard]] inline bool emit_ui_capture_shims(std::span<std::byte> allocation,
    std::uintptr_t allocation_address,
    std::uintptr_t interceptor,
    std::uintptr_t continuation,
    UiCaptureShimLayout& layout) noexcept {
    layout = {};
    if (allocation.size() < ui_capture_branch_pool_size || allocation_address == 0 ||
        interceptor == 0 || continuation == 0 ||
        allocation_address > UINTPTR_MAX - ui_capture_branch_pool_size) {
        return false;
    }

    const auto pool_end = allocation_address + ui_capture_branch_pool_size;
    const auto aligned_cell = (allocation_address + 7U) & ~std::uintptr_t{7U};
    if (aligned_cell < allocation_address || aligned_cell > pool_end ||
        pool_end - aligned_cell < 48U) {
        return false;
    }

    const auto gateway = aligned_cell + sizeof(std::uint64_t);
    const auto entry_shim = gateway + 23U;
    const auto used = static_cast<std::size_t>(entry_shim + 17U - allocation_address);
    if (used > allocation.size() || used > ui_capture_branch_pool_size) {
        return false;
    }

    const auto pointer_cell_offset = static_cast<std::size_t>(aligned_cell - allocation_address);
    const auto gateway_offset = static_cast<std::size_t>(gateway - allocation_address);
    const auto entry_offset = static_cast<std::size_t>(entry_shim - allocation_address);
    const auto shim_address = static_cast<std::uint64_t>(entry_shim);
    std::memcpy(allocation.data() + pointer_cell_offset, &shim_address, sizeof(shim_address));

    constexpr std::array<std::uint8_t, 3> move_rax_to_rdx{0x48, 0x89, 0xC2};
    constexpr std::array<std::uint8_t, 3> move_rdx_to_rax{0x48, 0x89, 0xD0};
    constexpr std::array<std::uint8_t, 6> original_prologue{0x48, 0x83, 0xEC, 0x28, 0x84, 0xC9};
    const REL::ASM::JMP14 entry_jump{interceptor};
    const REL::ASM::JMP14 continuation_jump{continuation};

    auto* gateway_bytes = allocation.data() + gateway_offset;
    std::memcpy(gateway_bytes, move_rdx_to_rax.data(), move_rdx_to_rax.size());
    std::memcpy(gateway_bytes + move_rdx_to_rax.size(),
        original_prologue.data(),
        original_prologue.size());
    std::memcpy(gateway_bytes + move_rdx_to_rax.size() + original_prologue.size(),
        &continuation_jump,
        sizeof(continuation_jump));

    auto* entry_bytes = allocation.data() + entry_offset;
    std::memcpy(entry_bytes, move_rax_to_rdx.data(), move_rax_to_rdx.size());
    std::memcpy(entry_bytes + move_rax_to_rdx.size(), &entry_jump, sizeof(entry_jump));

    layout.pointer_cell = aligned_cell;
    layout.original_gateway = gateway;
    layout.entry_shim = entry_shim;
    layout.bytes_used = used;
    return true;
}

[[nodiscard]] inline bool emit_ui_capture_entry_jump(std::span<std::byte> output,
    std::uintptr_t entry,
    std::uintptr_t pointer_cell) noexcept {
    if (output.size() < sizeof(REL::ASM::JMP6) || entry == 0 || pointer_cell == 0 ||
        entry > UINTPTR_MAX - sizeof(REL::ASM::JMP6)) {
        return false;
    }
    const auto next_instruction = entry + sizeof(REL::ASM::JMP6);
    const auto forward = pointer_cell >= next_instruction;
    const auto magnitude =
        forward ? pointer_cell - next_instruction : next_instruction - pointer_cell;
    constexpr auto negative_limit = static_cast<std::uintptr_t>(INT32_MAX) + 1U;
    if ((forward && magnitude > static_cast<std::uintptr_t>(INT32_MAX)) ||
        (!forward && magnitude > negative_limit)) {
        return false;
    }
    const REL::ASM::JMP6 jump{entry, pointer_cell};
    std::memcpy(output.data(), &jump, sizeof(jump));
    return true;
}

[[nodiscard]] inline std::uintptr_t decode_ui_capture_jump6_target(std::span<const std::byte> bytes,
    std::uintptr_t instruction_address) noexcept {
    if (bytes.size() < sizeof(REL::ASM::JMP6)) {
        return 0;
    }
    REL::ASM::JMP6 jump{0};
    std::memcpy(&jump, bytes.data(), sizeof(jump));
    if (jump.op != 0xFF || jump.rm != 0x25) {
        return 0;
    }
    if (instruction_address > UINTPTR_MAX - sizeof(jump)) {
        return 0;
    }
    const auto next_instruction = instruction_address + sizeof(jump);
    if (jump.disp >= 0) {
        const auto displacement = static_cast<std::uintptr_t>(jump.disp);
        return next_instruction > UINTPTR_MAX - displacement ? 0 : next_instruction + displacement;
    }
    const auto displacement = static_cast<std::uintptr_t>(-(static_cast<std::int64_t>(jump.disp)));
    return displacement > next_instruction ? 0 : next_instruction - displacement;
}

[[nodiscard]] inline std::uintptr_t decode_ui_capture_jump14_target(
    std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < sizeof(REL::ASM::JMP14)) {
        return 0;
    }
    REL::ASM::JMP14 jump{std::uintptr_t{0}};
    std::memcpy(&jump, bytes.data(), sizeof(jump));
    if (jump.jmp.op != 0xFF || jump.jmp.rm != 0x25 || jump.jmp.disp != 0) {
        return 0;
    }
    return static_cast<std::uintptr_t>(jump.addr);
}
} // namespace fallout4_adapter::native
