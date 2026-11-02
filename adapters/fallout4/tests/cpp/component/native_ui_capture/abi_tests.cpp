#include "native/ui_capture.hpp"
#include "native/ui_capture_abi.hpp"
#include "support.hpp"
#include "ui_capture_test.hpp"

#include <catch2/catch_test_macros.hpp>
#undef FAIL

#include <REL/ASM.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace fallout4_adapter::tests::ui_capture {

TEST_CASE("fallout4.native_ui_capture.ui_shim_layout_encoding",
    "[component][fallout4][native_ui_capture]") {
    constexpr std::size_t page_size = 4096;
    constexpr std::size_t entry_offset = 128;
    constexpr std::size_t continuation_offset = 256;

    ScopedExecutablePage const executable_page{page_size};
    ScopedCaptureState const capture_state;
    auto* page = executable_page.get();
    check(page != nullptr, "host fixture allocates a private executable page");
    if (!page) {
        return;
    }

    const auto page_address = reinterpret_cast<std::uintptr_t>(page);
    const auto entry_address = page_address + entry_offset;
    const auto continuation_address = page_address + continuation_offset;
    UiCaptureShimLayout layout{};
    const auto emitted =
        emit_ui_capture_shims(std::span<std::byte>{page, ui_capture_branch_pool_size},
            page_address,
            ui_capture_test::interceptor_address(),
            continuation_address,
            layout);
    check(emitted, "production emitter prepares the actual entry and original gateway shims");
    if (!emitted) {
        return;
    }
    check(layout.bytes_used <= 55,
        "production emitter stays within the 55-byte worst-case branch-pool budget");
    check((layout.pointer_cell & 7U) == 0, "emitted bridge pointer cell is eight-byte aligned");
    check(layout.original_gateway == layout.pointer_cell + sizeof(std::uint64_t),
        "gateway starts immediately after the bridge cell");
    check(layout.entry_shim == layout.original_gateway + 23U,
        "entry shim follows the 23-byte seeded original gateway");
    check(layout.bytes_used == static_cast<std::size_t>(layout.entry_shim + 17U - page_address),
        "reported byte length includes the complete 17-byte entry shim");

    UiCaptureShimLayout worst_alignment{};
    auto* unaligned_pool = page + 1025U;
    check(emit_ui_capture_shims(std::span<std::byte>{unaligned_pool, ui_capture_branch_pool_size},
              reinterpret_cast<std::uintptr_t>(unaligned_pool),
              ui_capture_test::interceptor_address(),
              continuation_address,
              worst_alignment) &&
              worst_alignment.bytes_used == 55U,
        "unaligned production allocation remains bounded by exactly 55 emitted bytes");

    std::uint64_t bridge_target{};
    std::memcpy(&bridge_target, page + (layout.pointer_cell - page_address), sizeof(bridge_target));
    check(bridge_target == layout.entry_shim,
        "aligned pointer cell targets the emitted production entry shim");

    const auto entry_shim_offset = static_cast<std::size_t>(layout.entry_shim - page_address);
    const auto gateway_offset = static_cast<std::size_t>(layout.original_gateway - page_address);
    constexpr std::array<std::uint8_t, 3> move_rax_to_rdx{0x48, 0x89, 0xC2};
    constexpr std::array<std::uint8_t, 3> move_rdx_to_rax{0x48, 0x89, 0xD0};
    constexpr std::array<std::uint8_t, 6> original_prologue{0x48, 0x83, 0xEC, 0x28, 0x84, 0xC9};
    check(std::memcmp(page + entry_shim_offset, move_rax_to_rdx.data(), move_rax_to_rdx.size()) ==
              0,
        "entry shim seeds the incoming full-width RAX in RDX");
    check(decode_ui_capture_jump14_target(
              std::span<const std::byte>{page + entry_shim_offset + move_rax_to_rdx.size(),
                  sizeof(REL::ASM::JMP14)}) == ui_capture_test::interceptor_address(),
        "entry shim reaches the production interceptor through its decoded JMP14");
    check(std::memcmp(page + gateway_offset, move_rdx_to_rax.data(), move_rdx_to_rax.size()) == 0 &&
              std::memcmp(page + gateway_offset + move_rdx_to_rax.size(),
                  original_prologue.data(),
                  original_prologue.size()) == 0,
        "gateway restores seeded RAX before replaying the exact six-byte helper prologue");
    check(decode_ui_capture_jump14_target(std::span<const std::byte>{page + gateway_offset + 9U,
              sizeof(REL::ASM::JMP14)}) == continuation_address,
        "gateway continues immediately after the copied prologue");

    std::array<std::byte, sizeof(REL::ASM::JMP6)> entry_patch{};
    check(emit_ui_capture_entry_jump(entry_patch, entry_address, layout.pointer_cell),
        "production emitter creates the entry JMP6");
    std::memcpy(page + entry_offset, entry_patch.data(), entry_patch.size());
    check(decode_ui_capture_jump6_target(entry_patch, entry_address) == layout.pointer_cell,
        "native entry JMP6 decodes to the owned aligned pointer cell");
    check(!emit_ui_capture_entry_jump(entry_patch,
              entry_address,
              entry_address + sizeof(REL::ASM::JMP6) +
                  static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max()) + 1U),
        "entry emitter rejects a bridge-cell displacement outside signed rel32 bounds");
}

TEST_CASE("fallout4.native_ui_capture.ui_executable_bridge_forwarding",
    "[component][fallout4][native_ui_capture]") {
    constexpr std::size_t page_size = 4096;
    constexpr std::size_t entry_offset = 128;
    constexpr std::size_t continuation_offset = 256;
    constexpr std::size_t caller_offset = 512;
    constexpr std::size_t clobber_offset = 768;
    constexpr std::uint64_t true_seed = 0xD1F2A3958764BC0EULL;
    constexpr std::uint64_t no_op_seed = 0x3A8FC7D10E5B2469ULL;
    constexpr std::uint64_t zero_marker = 0x94D36C2A7B1E805FULL;
    constexpr std::uint64_t mismatch_marker = 0xB62E491D83A7C50FULL;
    ScopedExecutablePage const executable_page{page_size};
    ScopedCaptureState const capture_state;
    auto* page = executable_page.get();
    check(page != nullptr, "host fixture allocates a private executable page");
    if (page == nullptr) {
        return;
    }
    const auto page_address = reinterpret_cast<std::uintptr_t>(page);
    const auto entry_address = page_address + entry_offset;
    const auto continuation_address = page_address + continuation_offset;
    UiCaptureShimLayout layout{};
    const auto emitted =
        emit_ui_capture_shims(std::span<std::byte>{page, ui_capture_branch_pool_size},
            page_address,
            ui_capture_test::interceptor_address(),
            continuation_address,
            layout);
    check(emitted, "production emitter prepares the actual entry and original gateway shims");
    if (!emitted) {
        return;
    }
    std::array<std::byte, sizeof(REL::ASM::JMP6)> entry_patch{};
    const bool entry_jump_emitted =
        emit_ui_capture_entry_jump(entry_patch, entry_address, layout.pointer_cell);
    check(entry_jump_emitted, "executable forwarding fixture installs the production entry JMP6");
    if (!entry_jump_emitted) {
        return;
    }
    std::memcpy(page + entry_offset, entry_patch.data(), entry_patch.size());

    constexpr std::array<std::uint8_t, 43> continuation{
        0x74,
        0x19, // JZ consumes copied TEST flags and branches to the distinct zero-CL result
        0x80,
        0xF9,
        0x00, // cmp cl, expected byte (the test patches byte 4)
        0x75,
        0x05, // jne mismatch result
        0x48,
        0x83,
        0xC4,
        0x28,
        0xC3, // matching nonzero CL preserves the full-width seeded true no-op
        0x48,
        0xB8, // mismatch path: distinct full-width return
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0x48,
        0x83,
        0xC4,
        0x28,
        0xC3,
        0x48,
        0xB8, // zero CL has a distinct full-width result
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0x48,
        0x83,
        0xC4,
        0x28,
        0xC3,
    };
    static_assert(continuation.size() == 43);
    auto executable_continuation = continuation;
    std::memcpy(executable_continuation.data() + 14U, &mismatch_marker, sizeof(mismatch_marker));
    std::memcpy(executable_continuation.data() + 29U, &zero_marker, sizeof(zero_marker));
    std::memcpy(page + continuation_offset,
        executable_continuation.data(),
        executable_continuation.size());

    std::array<std::uint8_t, 32> exit_clobber{0x48, 0x83, 0xEC, 0x28, 0x49, 0xBB};
    const auto count_address = reinterpret_cast<std::uintptr_t>(&count_exit_clobber);
    std::memcpy(exit_clobber.data() + 6U, &count_address, sizeof(count_address));
    exit_clobber[14] = 0x41;
    exit_clobber[15] = 0xFF;
    exit_clobber[16] = 0xD3; // call r11 to count the emitted callback execution
    exit_clobber[17] = 0x48;
    exit_clobber[18] = 0x83;
    exit_clobber[19] = 0xC4;
    exit_clobber[20] = 0x28;
    exit_clobber[21] = 0x48;
    exit_clobber[22] = 0xB8;
    std::memcpy(exit_clobber.data() + 23U, &exit_clobber_value, sizeof(exit_clobber_value));
    exit_clobber.back() = 0xC3;
    std::memcpy(page + clobber_offset, exit_clobber.data(), exit_clobber.size());
    auto* clobber_entry = page + clobber_offset;
    const auto clobber = reinterpret_cast<ExitClobber>(clobber_entry);
    check(std::memcmp(page + clobber_offset, exit_clobber.data(), exit_clobber.size()) == 0 &&
              exit_clobber[21] == 0x48 && exit_clobber[22] == 0xB8 && exit_clobber.back() == 0xC3,
        "emitted exit callback counts its execution then sets all 64 RAX bits immediately before "
        "RET");

    constexpr std::array<std::uint8_t, 25> caller{
        0x48,
        0x83,
        0xEC,
        0x28, // shadow space and call alignment
        0x48,
        0x89,
        0xD0, // seed entry RAX from the second argument
        0x49,
        0xBB, // mov r11, entry address
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0x41,
        0xFF,
        0xD3, // call r11
        0x48,
        0x83,
        0xC4,
        0x28,
        0xC3,
    };
    static_assert(caller.size() == 25);
    std::memcpy(page + caller_offset, caller.data(), caller.size());
    std::memcpy(page + caller_offset + 9U, &entry_address, sizeof(entry_address));
    check(FlushInstructionCache(GetCurrentProcess(), page, page_size) != FALSE,
        "host fixture flushes emitted executable bytes");

    using ProbeCall = std::uint64_t (*)(std::uint8_t, std::uint64_t);
    static_assert(sizeof(ProbeCall) == sizeof(void*));
    const auto probe = reinterpret_cast<ProbeCall>(page + caller_offset);

    ui_capture_test::reset(100);
    ui_capture_test::set_original(
        reinterpret_cast<SeededOriginal>(page + (layout.original_gateway - page_address)));
    exit_clobber_calls.store(0, std::memory_order_relaxed);
    ui_capture_test::set_exit_clobber(clobber);
    std::size_t emitted_probe_count{};
    for (const auto original_cl : std::array<std::uint8_t, 4>{0x00U, 0x01U, 0x02U, 0xFFU}) {
        page[continuation_offset + 4U] = static_cast<std::byte>(original_cl);
        check(FlushInstructionCache(GetCurrentProcess(),
                  page + continuation_offset,
                  executable_continuation.size()) != FALSE,
            "host fixture flushes the expected-CL continuation check");
        const auto actual = probe(original_cl, true_seed);
        ++emitted_probe_count;
        check(actual == (original_cl == 0 ? zero_marker : true_seed),
            "JZ consumes copied TEST flags: zero takes its distinct result while each matching "
            "nonzero CL preserves seeded RAX");
        check(exit_clobber_calls.load(std::memory_order_relaxed) == emitted_probe_count,
            "the emitted full-RAX clobber callback executes once for every completed probe");
    }
    page[continuation_offset + 4U] = std::byte{0x01};
    static_cast<void>(FlushInstructionCache(GetCurrentProcess(),
        page + continuation_offset,
        continuation.size()));
    check(probe(0x01U, no_op_seed) == no_op_seed,
        "nonzero true no-op continuation preserves a second distinctive full-width seeded RAX "
        "value");
    ++emitted_probe_count;
    check(exit_clobber_calls.load(std::memory_order_relaxed) == emitted_probe_count,
        "the clobber executes for the independent nonzero true-no-op probe");
    page[continuation_offset + 4U] = std::byte{0x03};
    static_cast<void>(FlushInstructionCache(GetCurrentProcess(),
        page + continuation_offset,
        continuation.size()));
    check(probe(0x02U, true_seed) == mismatch_marker,
        "mismatching nonzero CL takes a third, distinct full-width result");
    ++emitted_probe_count;
    check(exit_clobber_calls.load(std::memory_order_relaxed) == emitted_probe_count &&
              emitted_probe_count == 6U,
        "the counted full-RAX clobber ran for every emitted zero, 1, 2, FF, seeded true-no-op and "
        "mismatch probe");
    check(ui_capture_test::in_flight() == 0,
        "emitted interceptor leaves no in-flight transition after normal return");
    check(ui_transition_generation() == 106,
        "each emitted helper call advances the production generation exactly once");
}

} // namespace fallout4_adapter::tests::ui_capture
