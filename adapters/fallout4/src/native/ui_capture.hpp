#pragma once

#include "native/observation.hpp"

#include <RE/C/ControlMap.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace fallout4_adapter::native {
inline constexpr std::size_t ui_capture_max_menu_entries = max_menu_entries;
inline constexpr std::size_t ui_capture_max_context_entries = max_context_entries;
inline constexpr std::size_t ui_capture_max_mapping_entries = max_mapped_entries;

struct UiMappedKeys {
    std::uint32_t attack{invalid_mapped_key};
    std::uint32_t aim{invalid_mapped_key};
    bool valid{};
};

struct UiMappingValue {
    const char* event_name{};
    std::int32_t input_key{};
};

[[nodiscard]] constexpr bool ui_scalar_counts_valid(std::size_t context_count,
    std::size_t menu_count,
    std::size_t mapping_count) noexcept {
    return context_count != 0 && context_count <= ui_capture_max_context_entries &&
           menu_count <= ui_capture_max_menu_entries &&
           mapping_count <= ui_capture_max_mapping_entries;
}

template <class GetMapping>
[[nodiscard]] UiMappedKeys select_ui_mapped_keys(std::size_t mapping_count,
    const char* attack_name,
    const char* aim_name,
    GetMapping get_mapping) noexcept {
    UiMappedKeys result{};
    if (mapping_count > ui_capture_max_mapping_entries || !attack_name || !aim_name) {
        return result;
    }

    result.valid = true;
    bool found_attack{};
    bool found_aim{};
    for (std::size_t index = 0; index < mapping_count; ++index) {
        const auto mapping = get_mapping(index);
        if (!found_attack && mapping.event_name == attack_name) {
            result.attack = mapping.input_key < 0 ? invalid_mapped_key
                                                  : static_cast<std::uint32_t>(mapping.input_key);
            found_attack = true;
        } else if (!found_aim && mapping.event_name == aim_name) {
            result.aim = mapping.input_key < 0 ? invalid_mapped_key
                                               : static_cast<std::uint32_t>(mapping.input_key);
            found_aim = true;
        }
        if (found_attack && found_aim) {
            break;
        }
    }
    return result;
}

struct UiStaticOffsets {
    std::uintptr_t manager_slot{};
    std::uintptr_t state{};
    std::uintptr_t shutdown{};
    std::uintptr_t running{};
};

struct UiStaticFields {
    std::uintptr_t manager_slot{};
    std::uint32_t state{};
    std::uint8_t shutdown{};
    std::uint8_t running{};
};

using SeededOriginal = std::uint64_t (*)(std::uint8_t original_cl, std::uint64_t entry_rax);

enum class OwnerPhaseAdmission : std::uint8_t { ready, deferred, retired };

struct OwnerPhaseFacts {
    std::uint64_t generation{};
    std::uint64_t in_flight{};
    bool closed{};
    bool static_fields_eligible{};
};

template <class Operations>
[[nodiscard]] OwnerPhaseAdmission admit_owner_phase(Operations& operations,
    std::uint64_t expected_generation) noexcept {
    if (expected_generation == 0) {
        return OwnerPhaseAdmission::retired;
    }
    if (!operations.try_gate()) {
        return OwnerPhaseAdmission::deferred;
    }

    const auto facts = operations.read_facts();
    operations.release_gate();
    if (facts.closed || facts.generation == 0 || facts.generation != expected_generation) {
        return OwnerPhaseAdmission::retired;
    }
    if (facts.in_flight != 0 || !facts.static_fields_eligible) {
        return OwnerPhaseAdmission::deferred;
    }
    return OwnerPhaseAdmission::ready;
}

enum class UiCaptureFence : std::uint8_t {
    process_messages,
    transition_gate,
    menu_stack,
    input_context
};

template <class Operations, class Copy>
[[nodiscard]] bool with_ui_capture_fences(Operations& operations, Copy&& copy) noexcept {
    constexpr std::array fences{
        UiCaptureFence::process_messages,
        UiCaptureFence::transition_gate,
        UiCaptureFence::menu_stack,
        UiCaptureFence::input_context,
    };
    std::size_t acquired{};
    bool copied{};
    for (const auto fence : fences) {
        if (!operations.try_acquire(fence)) {
            break;
        }
        ++acquired;
    }
    if (acquired == fences.size()) {
        if (operations.eligible()) {
            copied = std::forward<Copy>(copy)();
        }
    }
    while (acquired != 0) {
        --acquired;
        operations.release(fences[acquired]);
    }
    return copied;
}

[[nodiscard]] std::uint64_t ui_transition_generation() noexcept;
void disable_ui_capture() noexcept;
[[nodiscard]] OwnerPhaseAdmission owner_phase_admission(std::uint64_t expected_generation) noexcept;
[[nodiscard]] UiScalarSnapshot capture_ui_scalars(std::uint64_t expected_generation) noexcept;

using RemapOriginal = bool (*)(RE::ControlMap*,
    const RE::BSFixedString&,
    RE::INPUT_DEVICE,
    std::int32_t);

[[nodiscard]] bool publish_ui_capture_originals(SeededOriginal helper,
    RemapOriginal remap) noexcept;
[[nodiscard]] std::uint64_t ui_transition_interceptor(std::uint8_t original_cl,
    std::uint64_t entry_rax);
[[nodiscard]] bool remap_button_interceptor(RE::ControlMap* control_map,
    const RE::BSFixedString& event_name,
    RE::INPUT_DEVICE device,
    std::int32_t button);

[[nodiscard]] bool try_process_lock_words(volatile long* words, std::uint32_t thread_id) noexcept;
void release_process_lock_words(volatile long* words,
    void (*on_failure)() noexcept = nullptr) noexcept;
[[nodiscard]] bool try_read_count(volatile long* count) noexcept;
void release_read_count(volatile long* count, void (*on_failure)() noexcept = nullptr) noexcept;
[[nodiscard]] bool project_ui_static_fields(std::uintptr_t image,
    UiStaticOffsets offsets,
    UiStaticFields& fields) noexcept;
[[nodiscard]] bool ui_static_fields_eligible(std::uintptr_t image,
    const UiStaticFields& fields) noexcept;
[[nodiscard]] bool capture_eligible(std::uintptr_t image,
    const UiStaticFields& fields,
    std::uint64_t expected_generation,
    std::uint64_t current_generation,
    std::uint64_t in_flight,
    bool closed) noexcept;
} // namespace fallout4_adapter::native
