#include "cpp/component/native_ui_capture/ui_capture_test.hpp"
#include "native/dispatch.hpp"
#include "native/ui_capture.hpp"
#include "native/ui_capture_abi.hpp"
#include "support.hpp"

#include <REL/ASM.h>

#include <Windows.h>

#include <catch2/catch_test_macros.hpp>
#undef FAIL

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace fallout4_adapter::tests::dispatch {
using namespace fallout4_adapter::native;

TEST_CASE("fallout4.native_dispatch.canonical_guards_rel32_boundaries",
    "[component][fallout4][native_dispatch]") {
    ScopedDispatchState const dispatch_state;
    constexpr std::uintptr_t image = 0x140000000ULL;
    std::array<std::byte, native_dispatch_helper_guard.size()> helper{};
    std::array<std::byte, native_dispatch_remap_guard.size()> remap{};
    std::array<std::byte, native_dispatch_main_arguments.size()> arguments{};
    std::array<std::byte, native_dispatch_main_original_call.size()> call{};
    std::memcpy(helper.data(), native_dispatch_helper_guard.data(), helper.size());
    std::memcpy(remap.data(), native_dispatch_remap_guard.data(), remap.size());
    std::memcpy(arguments.data(), native_dispatch_main_arguments.data(), arguments.size());
    std::memcpy(call.data(), native_dispatch_main_original_call.data(), call.size());
    const auto call_address = image + native_dispatch_main_call_rva;
    check(
        native_dispatch_original_guards_match(helper, remap, arguments, call, call_address, image),
        "canonical production predicate accepts every exact helper, public remap and Main guard");

    auto changed_helper = helper;
    changed_helper[4] ^= std::byte{1};
    check(!native_dispatch_original_guards_match(changed_helper,
              remap,
              arguments,
              call,
              call_address,
              image),
        "canonical production guard predicate rejects a changed helper branch condition");
    auto changed_remap = remap;
    changed_remap[12] ^= std::byte{1};
    check(!native_dispatch_original_guards_match(helper,
              changed_remap,
              arguments,
              call,
              call_address,
              image),
        "canonical production guard predicate rejects a changed public prologue");
    auto changed_arguments = arguments;
    changed_arguments[5] ^= std::byte{1};
    check(!native_dispatch_original_guards_match(helper,
              remap,
              changed_arguments,
              call,
              call_address,
              image),
        "canonical production guard predicate rejects changed Main argument setup");
    auto changed_call = call;
    changed_call[1] ^= std::byte{1};
    check(!native_dispatch_original_guards_match(helper,
              remap,
              arguments,
              changed_call,
              call_address,
              image),
        "canonical production guard predicate rejects a nonmatching original Main call");
    check(!native_dispatch_original_guards_match(helper,
              remap,
              arguments,
              call,
              call_address,
              UINTPTR_MAX),
        "canonical production guard predicate rejects image-relative target overflow");

    constexpr std::uintptr_t instruction = 0x180000000ULL;
    constexpr auto next_instruction = instruction + 5U;
    std::array<std::byte, 5> encoded{};
    check(emit_native_dispatch_call5(encoded,
              instruction,
              next_instruction + static_cast<std::uintptr_t>(INT32_MAX)) &&
              decode_native_dispatch_call5_target(encoded, instruction) ==
                  next_instruction + static_cast<std::uintptr_t>(INT32_MAX),
        "Main CALL5 emitter accepts and decodes the largest positive rel32 displacement");
    check(!emit_native_dispatch_call5(encoded,
              instruction,
              next_instruction + static_cast<std::uintptr_t>(INT32_MAX) + 1U),
        "Main CALL5 emitter rejects positive rel32 overflow");
    constexpr auto negative_limit = static_cast<std::uintptr_t>(INT32_MAX) + 1U;
    check(emit_native_dispatch_call5(encoded, instruction, next_instruction - negative_limit) &&
              decode_native_dispatch_call5_target(encoded, instruction) ==
                  next_instruction - negative_limit,
        "Main CALL5 emitter accepts and decodes the INT32_MIN negative rel32 displacement");
    check(!emit_native_dispatch_call5(encoded, instruction, next_instruction - negative_limit - 1U),
        "Main CALL5 emitter rejects negative rel32 overflow");
    check(!emit_native_dispatch_call5(encoded, UINTPTR_MAX - 3U, 0x1000U) &&
              decode_native_dispatch_call5_target(encoded, UINTPTR_MAX - 3U) == 0,
        "Main CALL5 encode and decode reject instruction-address overflow");
}

TEST_CASE("fallout4.native_dispatch.fixed_pool_layout_emitted_targets",
    "[component][fallout4][native_dispatch]") {
    ScopedDispatchState const dispatch_state;
    constexpr std::size_t page_size = 4096;
    ScopedVirtualAllocation const page_region{page_size, PAGE_EXECUTE_READWRITE};
    auto* page = page_region.get();
    check(page != nullptr, "host fixture allocates an executable page for the emitted fixed pool");
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
        "production emitter prepares every resource in the fixed branch-pool allocation");
    check(layout.bytes_used == 92U,
        "aligned pool emits exactly through the 92-byte fixed resource region");
    check(layout.helper.pointer_cell == address, "helper bridge cell begins at aligned pool base");
    check(layout.helper.original_gateway == address + 8U &&
              layout.helper.entry_shim == address + 31U,
        "helper gateway and seeded shim retain their frozen pool offsets");
    check(layout.main_relay == address + 48U && layout.remap_cell == address + 64U &&
              layout.remap_gateway == address + 72U,
        "Main relay, remap pointer and remap gateway retain their frozen pool offsets");

    std::uint64_t helper_cell{};
    std::memcpy(&helper_cell, page + (layout.helper.pointer_cell - address), sizeof(helper_cell));
    check(helper_cell == layout.helper.entry_shim,
        "helper cell targets its emitted seeded entry shim");
    std::uint64_t remap_cell{};
    std::memcpy(&remap_cell, page + (layout.remap_cell - address), sizeof(remap_cell));
    check(remap_cell == reinterpret_cast<std::uintptr_t>(&remap_button_interceptor),
        "public remap cell targets its production interceptor");
    check(decode_ui_capture_jump14_target(
              std::span<const std::byte>{page + (layout.main_relay - address),
                  sizeof(REL::ASM::JMP14)}) == native_dispatch_test::main_wrapper_address(),
        "Main CALL5 relay uses the fixed absolute-jump resource to reach the production wrapper");
    constexpr std::array<std::uint8_t, 6> remap_replay{0x48, 0x89, 0x5C, 0x24, 0x10, 0x56};
    check(std::memcmp(page + (layout.remap_gateway - address),
              remap_replay.data(),
              remap_replay.size()) == 0 &&
              decode_ui_capture_jump14_target(std::span<const std::byte>{
                  page + (layout.remap_gateway - address) + remap_replay.size(),
                  sizeof(REL::ASM::JMP14)}) == address + 1536U,
        "remap gateway replays the exact six-byte prologue then continues at its emitted original "
        "instruction");

    NativeDispatchLayout unaligned{};
    const auto* unaligned_pool = page + 513U;
    const auto unaligned_address = reinterpret_cast<std::uintptr_t>(unaligned_pool);
    check(make_native_dispatch_layout(unaligned_address, native_dispatch_pool_size, unaligned) &&
              unaligned.bytes_used == 92U + ((8U - (unaligned_address & 7U)) & 7U) &&
              unaligned.bytes_used <= native_dispatch_pool_size,
        "unaligned fixed pool maintains aligned cells and remains within 128 bytes");
    check(!make_native_dispatch_layout(UINTPTR_MAX - 16U, native_dispatch_pool_size, unaligned) &&
              !make_native_dispatch_layout(address, native_dispatch_pool_size - 1U, unaligned),
        "fixed pool layout rejects address overflow and undersized allocations");
}

} // namespace fallout4_adapter::tests::dispatch
