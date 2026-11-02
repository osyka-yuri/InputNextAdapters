#pragma once

#include "native/dispatch.hpp"
#include "native/ui_capture.hpp"
#include "native/ui_capture_abi.hpp"

#include <REL/ASM.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace F4SE {
class TrampolineInterface;
}

namespace fallout4_adapter::native {

[[nodiscard]] inline bool read_bytes(std::uintptr_t address, std::span<std::byte> output) noexcept {
    if (address == 0 || output.empty() || address > UINTPTR_MAX - output.size()) {
        return false;
    }
    std::memcpy(output.data(), reinterpret_cast<const void*>(address), output.size());
    return true;
}

[[nodiscard]] inline bool matches_bytes(std::uintptr_t address,
    std::span<const std::uint8_t> expected) noexcept {
    if (address == 0 || expected.empty() || address > UINTPTR_MAX - expected.size()) {
        return false;
    }
    return std::memcmp(reinterpret_cast<const void*>(address), expected.data(), expected.size()) ==
           0;
}

[[nodiscard]] inline std::uintptr_t read_manager_slot(std::uintptr_t image) noexcept {
    std::uintptr_t pointer{};
    if (image == 0 || image > UINTPTR_MAX - native_dispatch_manager_slot_rva ||
        image + native_dispatch_manager_slot_rva > UINTPTR_MAX - sizeof(pointer)) {
        return UINTPTR_MAX;
    }
    std::memcpy(&pointer,
        reinterpret_cast<const void*>(image + native_dispatch_manager_slot_rva),
        sizeof(pointer));
    return pointer;
}

[[nodiscard]] inline bool site_guards_match(std::uintptr_t image) noexcept {
    if (image == 0 || image > UINTPTR_MAX - native_dispatch_main_message_loop_rva ||
        image > UINTPTR_MAX - native_dispatch_main_call_rva ||
        image > UINTPTR_MAX - native_dispatch_remap_button_rva ||
        image > UINTPTR_MAX - native_dispatch_helper_rva) {
        return false;
    }
    const auto main_call = image + native_dispatch_main_call_rva;
    if (main_call < native_dispatch_main_arguments.size()) {
        return false;
    }
    std::array<std::byte, native_dispatch_helper_guard.size()> helper{};
    std::array<std::byte, native_dispatch_remap_guard.size()> remap{};
    std::array<std::byte, native_dispatch_main_arguments.size()> arguments{};
    std::array<std::byte, native_dispatch_main_original_call.size()> call{};
    return read_bytes(image + native_dispatch_helper_rva, helper) &&
           read_bytes(image + native_dispatch_remap_button_rva, remap) &&
           read_bytes(main_call - arguments.size(), arguments) && read_bytes(main_call, call) &&
           native_dispatch_original_guards_match(helper, remap, arguments, call, main_call, image);
}

[[nodiscard]] inline bool main_arguments_match(std::uintptr_t site) noexcept {
    if (site < native_dispatch_main_arguments.size()) {
        return false;
    }
    std::array<std::byte, native_dispatch_main_arguments.size()> arguments{};
    return read_bytes(site - arguments.size(), arguments) &&
           std::memcmp(arguments.data(), native_dispatch_main_arguments.data(), arguments.size()) ==
               0;
}

[[nodiscard]] inline bool remap_entry_jump_matches(std::uintptr_t site,
    std::uintptr_t cell) noexcept {
    std::array<std::byte, 13> actual{};
    if (!read_bytes(site, actual)) {
        return false;
    }
    return decode_ui_capture_jump6_target(actual, site) == cell &&
           std::memcmp(actual.data() + sizeof(REL::ASM::JMP6),
               native_dispatch_remap_guard.data() + sizeof(REL::ASM::JMP6),
               native_dispatch_remap_guard.size() - sizeof(REL::ASM::JMP6)) == 0;
}

[[nodiscard]] inline bool helper_entry_jump_matches(std::uintptr_t site,
    std::uintptr_t cell) noexcept {
    std::array<std::byte, 8> actual{};
    if (!read_bytes(site, actual)) {
        return false;
    }
    return decode_ui_capture_jump6_target(actual, site) == cell &&
           std::memcmp(actual.data() + sizeof(REL::ASM::JMP6),
               native_dispatch_helper_guard.data() + sizeof(REL::ASM::JMP6),
               native_dispatch_helper_guard.size() - sizeof(REL::ASM::JMP6)) == 0;
}

[[nodiscard]] inline bool
main_call_jump_matches(std::uintptr_t site, std::uintptr_t relay, std::uintptr_t image) noexcept {
    std::array<std::byte, 5> call{};
    return main_arguments_match(site) && read_bytes(site, call) &&
           decode_native_dispatch_call5_target(call, site) == relay && image != 0;
}

[[nodiscard]] inline bool main_call_original_matches(std::uintptr_t site,
    std::uintptr_t image) noexcept {
    std::array<std::byte, 5> call{};
    return main_arguments_match(site) && read_bytes(site, call) &&
           std::memcmp(call.data(),
               native_dispatch_main_original_call.data(),
               native_dispatch_main_original_call.size()) == 0 &&
           image != 0 && image <= UINTPTR_MAX - native_dispatch_main_message_loop_rva &&
           decode_native_dispatch_call5_target(call, site) ==
               image + native_dispatch_main_message_loop_rva;
}

template <class Platform>
class BasicNativeDispatchOperations final {
public:
    explicit BasicNativeDispatchOperations(Platform& platform) noexcept : platform_(platform) {}

    [[nodiscard]] bool preflight() noexcept {
        image = platform_.image_base();
        trampoline = platform_.trampoline();
        return image != 0 && platform_.bindings_available() &&
               platform_.interfaces_supported(trampoline) && read_manager_slot(image) == 0 &&
               site_guards_match(image);
    }

    [[nodiscard]] bool prepare() noexcept {
        allocation = platform_.allocate_branch_pool(trampoline, native_dispatch_pool_size);
        if (!allocation) {
            return false;
        }
        const auto address = reinterpret_cast<std::uintptr_t>(allocation);
        if (!emit_native_dispatch_resources(std::span{allocation, native_dispatch_pool_size},
                address,
                NativeDispatchTargets{
                    .helper_interceptor = platform_.helper_interceptor(),
                    .helper_continuation =
                        image + native_dispatch_helper_rva + sizeof(REL::ASM::JMP6),
                    .main_wrapper = platform_.main_wrapper(),
                    .remap_interceptor = platform_.remap_interceptor(),
                    .remap_continuation =
                        image + native_dispatch_remap_button_rva + sizeof(REL::ASM::JMP6),
                },
                layout)) {
            return false;
        }

        if (!emit_ui_capture_entry_jump(helper_patch,
                image + native_dispatch_helper_rva,
                layout.helper.pointer_cell) ||
            !emit_ui_capture_entry_jump(remap_patch,
                image + native_dispatch_remap_button_rva,
                layout.remap_cell) ||
            !emit_native_dispatch_call5(main_patch,
                image + native_dispatch_main_call_rva,
                layout.main_relay) ||
            !platform_.flush_instruction_cache(address, native_dispatch_pool_size)) {
            return false;
        }
        std::memcpy(prepared_resources.data(), allocation, prepared_resources.size());
        return true;
    }

    [[nodiscard]] bool pin_and_publish() noexcept {
        if (!platform_.pin_module()) {
            return false;
        }
        pinned = true;
        const auto helper = reinterpret_cast<SeededOriginal>(layout.helper.original_gateway);
        const auto remap = reinterpret_cast<RemapOriginal>(layout.remap_gateway);
        const auto main_loop = reinterpret_cast<MainMessageLoopOriginal>(
            image + native_dispatch_main_message_loop_rva);
        return platform_.publish_originals(helper, remap, main_loop);
    }

    [[nodiscard]] bool precommit() const noexcept {
        if (!pinned || image != platform_.image_base() || !platform_.bindings_available() ||
            !platform_.interfaces_supported(trampoline) || read_manager_slot(image) != 0 ||
            !site_guards_match(image)) {
            return false;
        }
        return platform_.precommit();
    }

    [[nodiscard]] NativeDispatchAttemptInspection attempt_once(NativeDispatchSite site,
        std::uint8_t expected_owned_mask) noexcept {
        std::uintptr_t address{};
        const std::byte* patch{};
        std::size_t patch_size{};
        switch (site) {
        case NativeDispatchSite::helper:
            address = image + native_dispatch_helper_rva;
            patch = helper_patch.data();
            patch_size = helper_patch.size();
            break;
        case NativeDispatchSite::remap_button:
            address = image + native_dispatch_remap_button_rva;
            patch = remap_patch.data();
            patch_size = remap_patch.size();
            break;
        case NativeDispatchSite::main_message_loop:
            address = image + native_dispatch_main_call_rva;
            patch = main_patch.data();
            patch_size = main_patch.size();
            break;
        }
        const bool write_succeeded =
            platform_.write_safe(address, std::span<const std::byte>{patch, patch_size});
        const bool instruction_cache_flushed =
            platform_.flush_instruction_cache(address, patch_size);
        const bool all_sites_safe = resources_match() && sites_are_original_or_owned();
        const bool expected_prefix = all_sites_safe && sites_match_mask(expected_owned_mask);
        return NativeDispatchAttemptInspection{
            .all_sites_safe = all_sites_safe,
            .expected_prefix = expected_prefix,
            .write_succeeded = write_succeeded,
            .instruction_cache_flushed = instruction_cache_flushed,
        };
    }

    void retain_disabled() const noexcept { platform_.retain_disabled(); }

    void fatal_control_flow() const noexcept { platform_.fatal_control_flow(); }

private:
    std::uintptr_t image{};
    const F4SE::TrampolineInterface* trampoline{};
    std::byte* allocation{};
    NativeDispatchLayout layout{};
    std::array<std::byte, native_dispatch_pool_size> prepared_resources{};
    std::array<std::byte, sizeof(REL::ASM::JMP6)> helper_patch{};
    std::array<std::byte, sizeof(REL::ASM::JMP6)> remap_patch{};
    std::array<std::byte, 5> main_patch{};
    bool pinned{};

    Platform& platform_;

    [[nodiscard]] bool resources_match() const noexcept {
        return (allocation != nullptr) && layout.bytes_used <= prepared_resources.size() &&
               std::memcmp(allocation, prepared_resources.data(), prepared_resources.size()) == 0 &&
               reinterpret_cast<std::uintptr_t>(allocation) <=
                   UINTPTR_MAX - native_dispatch_pool_size &&
               layout.helper.pointer_cell ==
                   ((reinterpret_cast<std::uintptr_t>(allocation) + 7U) & ~std::uintptr_t{7U}) &&
               layout.main_relay == layout.helper.pointer_cell + 48U &&
               layout.remap_cell == layout.helper.pointer_cell + 64U &&
               layout.remap_gateway == layout.helper.pointer_cell + 72U;
    }

    [[nodiscard]] bool helper_is_original() const noexcept {
        return matches_bytes(image + native_dispatch_helper_rva, native_dispatch_helper_guard);
    }

    [[nodiscard]] bool remap_is_original() const noexcept {
        return matches_bytes(image + native_dispatch_remap_button_rva, native_dispatch_remap_guard);
    }

    [[nodiscard]] bool main_is_original() const noexcept {
        return main_call_original_matches(image + native_dispatch_main_call_rva, image);
    }

    [[nodiscard]] bool helper_is_owned() const noexcept {
        return helper_entry_jump_matches(image + native_dispatch_helper_rva,
            layout.helper.pointer_cell);
    }

    [[nodiscard]] bool remap_is_owned() const noexcept {
        return remap_entry_jump_matches(image + native_dispatch_remap_button_rva,
            layout.remap_cell);
    }

    [[nodiscard]] bool main_is_owned() const noexcept {
        return main_call_jump_matches(image + native_dispatch_main_call_rva,
            layout.main_relay,
            image);
    }

    [[nodiscard]] bool sites_are_original_or_owned() const noexcept {
        return (helper_is_original() || helper_is_owned()) &&
               (remap_is_original() || remap_is_owned()) && (main_is_original() || main_is_owned());
    }

    [[nodiscard]] bool sites_match_mask(std::uint8_t mask) const noexcept {
        const bool helper_expected =
            (mask & (1U << std::to_underlying(NativeDispatchSite::helper))) != 0;
        const bool remap_expected =
            (mask & (1U << std::to_underlying(NativeDispatchSite::remap_button))) != 0;
        const bool main_expected =
            (mask & (1U << std::to_underlying(NativeDispatchSite::main_message_loop))) != 0;
        return (helper_expected ? helper_is_owned() : helper_is_original()) &&
               (remap_expected ? remap_is_owned() : remap_is_original()) &&
               (main_expected ? main_is_owned() : main_is_original());
    }
};

} // namespace fallout4_adapter::native
