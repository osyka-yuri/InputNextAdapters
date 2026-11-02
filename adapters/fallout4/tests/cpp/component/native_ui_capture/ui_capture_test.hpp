#pragma once

#include "native/ui_capture.hpp"

#include <cstddef>
#include <cstdint>

namespace fallout4_adapter::native::ui_capture_test {
using ExitClobber = void (*)();

void reset(std::uint64_t generation = 1) noexcept;
void clear_originals() noexcept;
void set_original(SeededOriginal original) noexcept;
void set_remap_original(RemapOriginal original) noexcept;
void set_generation(std::uint64_t generation) noexcept;
void set_in_flight(std::uint64_t count) noexcept;
void set_exit_clobber(ExitClobber clobber) noexcept;
[[nodiscard]] std::uint64_t generation() noexcept;
[[nodiscard]] std::uint64_t in_flight() noexcept;
[[nodiscard]] bool closed() noexcept;
[[nodiscard]] bool try_gate() noexcept;
void release_gate() noexcept;
[[nodiscard]] bool try_process_lock(volatile long* words, std::uint32_t thread_id) noexcept;
void release_process_lock(volatile long* words) noexcept;
[[nodiscard]] bool try_read_count(volatile long* count) noexcept;
void release_read_count(volatile long* count) noexcept;
[[nodiscard]] bool project_static_fields(std::uintptr_t image,
    UiStaticOffsets offsets,
    UiStaticFields& fields) noexcept;
[[nodiscard]] bool static_fields_eligible(std::uintptr_t image,
    const UiStaticFields& fields) noexcept;
[[nodiscard]] bool
eligible(std::uintptr_t image, UiStaticOffsets offsets, std::uint64_t expected_generation) noexcept;
[[nodiscard]] std::uintptr_t interceptor_address() noexcept;
[[nodiscard]] std::uint64_t invoke(std::uint8_t original_cl, std::uint64_t entry_rax);
[[nodiscard]] bool invoke_remap(RE::ControlMap* control_map,
    const RE::BSFixedString& event_name,
    RE::INPUT_DEVICE device,
    std::int32_t button);
} // namespace fallout4_adapter::native::ui_capture_test
