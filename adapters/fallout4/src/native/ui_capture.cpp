#include "ui_capture.hpp"
#include "observation.hpp"
#include "ui_capture_pipeline.hpp"

#include "bindings.hpp"

#include <RE/B/BSFixedString.h>
#include <RE/B/BSSpinLock.h>
#include <RE/C/ControlMap.h>
#include <RE/I/IMenu.h>
#include <RE/I/INPUT_DEVICE.h>
#include <RE/U/UI.h>
#include <RE/V/VATS.h>

#include <Windows.h>
#ifdef ERROR
#    undef ERROR
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

namespace fallout4_adapter::native {
namespace {
NativeUiCapturePipeline g_ui_capture_pipeline{};

struct UiInterns {
    RE::BSFixedString vats_menu{"VATSMenu"};
    RE::BSFixedString attack{"PrimaryAttack"};
    RE::BSFixedString aim{"SecondaryAttack"};
};

[[nodiscard]] const UiInterns* prepare_ui_interns() noexcept {
    static const auto* interns = [] noexcept -> UiInterns* {
        try {
            return new UiInterns{};
        } catch (...) {
            return nullptr;
        }
    }();
    return interns;
}

[[nodiscard]] bool load_bytes(const void* address, void* destination, std::size_t size) noexcept {
    if (!address || !destination || size == 0) {
        return false;
    }
    std::memcpy(destination, address, size);
    return true;
}

} // namespace

[[noreturn]] void fatal_unexpected_entry() noexcept {
    OutputDebugStringA(
        "FATAL: Fallout 4 UI transition hook encountered unexpected control-flow state.\n");
    RaiseFailFastException(nullptr, nullptr, 0);
    static_cast<void>(TerminateProcess(GetCurrentProcess(), ERROR_UNHANDLED_EXCEPTION));
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

NativeUiCapturePipeline& ui_capture_pipeline() noexcept {
    return g_ui_capture_pipeline;
}

namespace {
// Fallout4.exe 1.11.240.0 (RVA 0x1659400..0x16596F9):
// BSReadWriteLock uses bit 31 for write ownership and bits 0-27
// for lock counts. Bits 28-30 are unused, not writer-wait flags.
constexpr std::uint32_t read_write_count_mask = 0x0FFFFFFFU;
constexpr std::size_t lock_word_offset = sizeof(std::uint32_t);

static_assert(sizeof(int) == sizeof(std::uint32_t));
static_assert(sizeof(long) == sizeof(std::uint32_t));
static_assert(sizeof(RE::BSSpinLock) == sizeof(std::uint32_t) * 2U);
static_assert(alignof(RE::BSSpinLock) == alignof(std::uint32_t));
static_assert(std::is_standard_layout_v<RE::BSSpinLock>);
static_assert(sizeof(RE::BSReadWriteLock) == sizeof(std::uint32_t) * 2U);
static_assert(alignof(RE::BSReadWriteLock) == alignof(std::uint32_t));
static_assert(std::is_standard_layout_v<RE::BSReadWriteLock>);

[[nodiscard]] bool is_u32_aligned(const volatile void* address) noexcept {
    return address != nullptr &&
           (reinterpret_cast<std::uintptr_t>(address) % alignof(std::uint32_t)) == 0U;
}

[[nodiscard]] volatile std::uint32_t* spin_owner_word(RE::BSSpinLock* lock) noexcept {
    if (!is_u32_aligned(lock)) {
        return nullptr;
    }
    return reinterpret_cast<volatile std::uint32_t*>(reinterpret_cast<std::byte*>(lock));
}

[[nodiscard]] volatile std::uint32_t* spin_count_word(RE::BSSpinLock* lock) noexcept {
    if (!is_u32_aligned(lock)) {
        return nullptr;
    }
    return reinterpret_cast<volatile std::uint32_t*>(
        reinterpret_cast<std::byte*>(lock) + lock_word_offset);
}

// MSVC x64: aligned 32-bit load with an acquire compiler barrier.
[[nodiscard]] std::uint32_t acquire_load_u32(const volatile std::uint32_t* word) noexcept {
    const auto value = *word;
    std::atomic_signal_fence(std::memory_order_acquire);
    return value;
}

void store_volatile_u32(volatile std::uint32_t* word, std::uint32_t value) noexcept {
    *word = value;
}

[[nodiscard]] std::uint32_t interlocked_cas_u32(volatile std::uint32_t* word,
    std::uint32_t desired,
    std::uint32_t expected) noexcept {
    return std::bit_cast<std::uint32_t>(
        InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(word),
            std::bit_cast<LONG>(desired),
            std::bit_cast<LONG>(expected)));
}
} // namespace

bool try_process_lock(RE::BSSpinLock* lock, std::uint32_t thread_id) noexcept {
    auto* const owner = spin_owner_word(lock);
    auto* const count = spin_count_word(lock);
    if (!owner || !count || thread_id == 0U) {
        return false;
    }
    if (interlocked_cas_u32(count, 1U, 0U) != 0U) {
        return false;
    }
    store_volatile_u32(owner, thread_id);
    MemoryBarrier();
    return true;
}

void release_process_lock(RE::BSSpinLock* lock,
    std::uint32_t expected_thread_id,
    void (*on_failure)() noexcept) noexcept {
    auto* const owner = spin_owner_word(lock);
    auto* const count = spin_count_word(lock);
    if (!owner || !count) {
        if (on_failure) {
            on_failure();
        }
        return;
    }
    // Preserve recursive or foreign locks on invalid release.
    const auto current_count = acquire_load_u32(count);
    const auto current_owner = acquire_load_u32(owner);
    if (current_count != 1U || current_owner == 0U || expected_thread_id == 0U ||
        current_owner != expected_thread_id) {
        if (on_failure) {
            on_failure();
        }
        return;
    }
    store_volatile_u32(owner, 0U);
    MemoryBarrier();
    if (interlocked_cas_u32(count, 0U, 1U) != 1U && on_failure) {
        on_failure();
    }
}

bool try_read_count(volatile std::uint32_t* count) noexcept {
    if (!is_u32_aligned(count)) {
        return false;
    }
    return interlocked_cas_u32(count, 1U, 0U) == 0U;
}

void release_read_count(volatile std::uint32_t* count, void (*on_failure)() noexcept) noexcept {
    if (!is_u32_aligned(count)) {
        if (on_failure) {
            on_failure();
        }
        return;
    }
    auto observed = acquire_load_u32(count);
    for (;;) {
        if (observed == 0U || (observed & ~read_write_count_mask) != 0U) {
            if (on_failure) {
                on_failure();
            }
            return;
        }
        const auto previous = interlocked_cas_u32(count, observed - 1U, observed);
        if (previous == observed) {
            return;
        }
        observed = previous;
    }
}

bool project_ui_static_fields(std::uintptr_t image,
    UiStaticOffsets offsets,
    UiStaticFields& fields) noexcept {
    fields = {};
    const auto max_offset = std::max({
        offsets.manager_slot + sizeof(fields.manager_slot),
        offsets.state + sizeof(fields.state),
        offsets.shutdown + sizeof(fields.shutdown),
        offsets.running + sizeof(fields.running),
    });
    if (image == 0 || max_offset < offsets.manager_slot || max_offset < offsets.state ||
        max_offset < offsets.shutdown || max_offset < offsets.running ||
        image > UINTPTR_MAX - max_offset) {
        return false;
    }
    return load_bytes(reinterpret_cast<const void*>(image + offsets.manager_slot),
               &fields.manager_slot,
               sizeof(fields.manager_slot)) &&
           load_bytes(reinterpret_cast<const void*>(image + offsets.state),
               &fields.state,
               sizeof(fields.state)) &&
           load_bytes(reinterpret_cast<const void*>(image + offsets.shutdown),
               &fields.shutdown,
               sizeof(fields.shutdown)) &&
           load_bytes(reinterpret_cast<const void*>(image + offsets.running),
               &fields.running,
               sizeof(fields.running));
}

bool ui_static_fields_eligible(std::uintptr_t image, const UiStaticFields& fields) noexcept {
    return image <= UINTPTR_MAX - ui_static_storage_rva &&
           fields.manager_slot == image + ui_static_storage_rva && fields.state == 0 &&
           fields.shutdown == 0 && fields.running == 1;
}

bool capture_eligible(std::uintptr_t image,
    const UiStaticFields& fields,
    std::uint64_t expected_generation,
    std::uint64_t current_generation,
    std::uint64_t in_flight,
    bool closed) noexcept {
    return !closed && expected_generation != 0 && current_generation == expected_generation &&
           in_flight == 0 && ui_static_fields_eligible(image, fields);
}

namespace {
struct CaptureOperations {
    std::uintptr_t image{};
    DWORD thread_id{};
    std::uint64_t expected_generation{};
    RE::UI* ui{};
    RE::ControlMap* control_map{};
    RE::VATS* vats{};
    volatile std::uint32_t* menu_stack_count{};
    volatile std::uint32_t* context_stack_count{};

    [[nodiscard]] bool try_acquire(UiCaptureFence fence) const noexcept {
        switch (fence) {
        case UiCaptureFence::process_messages:
            return try_process_lock(std::addressof(ui->processMessagesLock), thread_id);
        case UiCaptureFence::transition_gate:
            return g_ui_capture_pipeline.try_gate();
        case UiCaptureFence::menu_stack:
            return try_read_count(menu_stack_count);
        case UiCaptureFence::input_context:
            return try_read_count(context_stack_count);
        }
        return false;
    }

    [[nodiscard]] bool eligible() const noexcept {
        const UiStaticOffsets offsets{
            .manager_slot = ui_static_slot_rva,
            .state = ui_state_rva,
            .shutdown = ui_shutdown_rva,
            .running = ui_running_rva,
        };
        UiStaticFields fields{};
        if (!project_ui_static_fields(image, offsets, fields)) {
            return false;
        }
        const auto generation = g_ui_capture_pipeline.generation();
        const auto in_flight = g_ui_capture_pipeline.in_flight();
        const bool closed = g_ui_capture_pipeline.closed();
        return capture_eligible(image, fields, expected_generation, generation, in_flight, closed);
    }

    [[nodiscard]] bool copy_scalars(const UiInterns& interns,
        UiScalarSnapshot& snapshot) const noexcept {
        const auto context_size = control_map->contextPriorityStack.size();
        const auto menu_size = ui->menuStack.size();
        if (!ui_scalar_counts_valid(context_size, menu_size, 0)) {
            return false;
        }

        snapshot.context_count = static_cast<std::uint8_t>(context_size);
        snapshot.menu_count = static_cast<std::uint8_t>(menu_size);
        std::int32_t top_context{-1};
        for (std::uint32_t index = 0; index < context_size; ++index) {
            const auto context = control_map->contextPriorityStack[index].get();
            const auto value = static_cast<std::int32_t>(context);
            if (value < 0 || value >= 0x21) {
                return false;
            }
            snapshot.contexts[index] = value;
            top_context = value;
        }

        for (std::uint32_t index = 0; index < menu_size; ++index) {
            const auto* menu = ui->menuStack[index].get();
            if (!menu) {
                return false;
            }
            snapshot.menus[index] = UiMenuScalar{
                .flags = menu->menuFlags.underlying(),
                .input_context = static_cast<std::int32_t>(menu->inputContext.get()),
                .vats_menu_name = menu->menuName.c_str() == interns.vats_menu.c_str(),
            };
        }

        const auto native_mode = vats->mode.get();
        snapshot.native_vats_mode = static_cast<std::uint32_t>(native_mode);

        if (top_context < 0 || top_context >= 0x21) {
            return false;
        }
        const auto context_index = static_cast<std::uint32_t>(top_context);
        const auto device_index =
            static_cast<std::size_t>(std::to_underlying(RE::INPUT_DEVICE::kGamepad));
        const auto* input_context = control_map->controlMaps[context_index];
        if (!input_context) {
            snapshot.attack_key = invalid_mapped_key;
            snapshot.aim_key = invalid_mapped_key;
            return true;
        }

        const auto& mappings = input_context->deviceMappings[device_index];
        if (!ui_scalar_counts_valid(context_size, menu_size, mappings.size())) {
            return false;
        }
        const auto keys = select_ui_mapped_keys(mappings.size(),
            interns.attack.c_str(),
            interns.aim.c_str(),
            [&mappings](std::size_t index) noexcept {
                const auto& mapping = mappings[static_cast<std::uint32_t>(index)];
                return UiMappingValue{.event_name = mapping.eventID.c_str(),
                    .input_key = mapping.inputKey};
            });
        if (!keys.valid) {
            return false;
        }
        snapshot.attack_key = keys.attack;
        snapshot.aim_key = keys.aim;
        return true;
    }

    void release(UiCaptureFence fence) const noexcept {
        switch (fence) {
        case UiCaptureFence::process_messages:
            release_process_lock(std::addressof(ui->processMessagesLock), thread_id, [] noexcept {
                g_ui_capture_pipeline.close_capture_without_gate();
            });
            break;
        case UiCaptureFence::transition_gate:
            g_ui_capture_pipeline.release_gate();
            break;
        case UiCaptureFence::menu_stack:
            release_read_count(menu_stack_count,
                [] noexcept { g_ui_capture_pipeline.close_capture_without_gate(); });
            break;
        case UiCaptureFence::input_context:
            release_read_count(context_stack_count,
                [] noexcept { g_ui_capture_pipeline.close_capture_without_gate(); });
            break;
        }
    }
};

struct OwnerPhaseOperations {
    [[nodiscard]] static bool try_gate() noexcept { return ui_capture_pipeline().try_gate(); }

    [[nodiscard]] static OwnerPhaseFacts read_facts() noexcept {
        const auto image = checked_image_base();
        const UiStaticOffsets offsets{
            .manager_slot = ui_static_slot_rva,
            .state = ui_state_rva,
            .shutdown = ui_shutdown_rva,
            .running = ui_running_rva,
        };
        UiStaticFields fields{};
        const bool available = project_ui_static_fields(image, offsets, fields);
        return OwnerPhaseFacts{
            .generation = ui_capture_pipeline().generation(),
            .in_flight = ui_capture_pipeline().in_flight(),
            .closed = ui_capture_pipeline().closed(),
            .static_fields_eligible = available && ui_static_fields_eligible(image, fields),
        };
    }

    static void release_gate() noexcept { ui_capture_pipeline().release_gate(); }
};
} // namespace

std::uint64_t ui_transition_generation() noexcept {
    return ui_capture_pipeline().generation();
}

void disable_ui_capture() noexcept {
    ui_capture_pipeline().disable_ui_capture();
}

bool publish_ui_capture_originals(SeededOriginal helper, RemapOriginal remap) noexcept {
    return ui_capture_pipeline().publish_originals(helper, remap);
}

OwnerPhaseAdmission owner_phase_admission(std::uint64_t expected_generation) noexcept {
    OwnerPhaseOperations operations{};
    return admit_owner_phase(operations, expected_generation);
}

std::uint64_t ui_transition_interceptor(std::uint8_t original_cl, std::uint64_t entry_rax) {
    return invoke_ui_transition(ui_capture_pipeline(), original_cl, entry_rax);
}

bool remap_button_interceptor(RE::ControlMap* control_map,
    const RE::BSFixedString& event_name,
    RE::INPUT_DEVICE device,
    std::int32_t button) {
    return invoke_remap_button(ui_capture_pipeline(), control_map, event_name, device, button);
}

UiScalarSnapshot capture_ui_scalars(std::uint64_t expected_generation) noexcept {
    UiScalarSnapshot snapshot{};
    snapshot.generation = expected_generation;
    const auto initial_generation = ui_transition_generation();
    if (expected_generation == 0 || initial_generation != expected_generation) {
        return snapshot;
    }

    const auto image = checked_image_base();
    const auto* interns = prepare_ui_interns();
    auto* user_interface = RE::UI::GetSingleton();
    auto* control_map = RE::ControlMap::GetSingleton();
    auto* vats = RE::VATS::GetSingleton();
    const auto thread_id = GetCurrentThreadId();
    const auto before_fences_generation = ui_transition_generation();
    if (!image || !interns || !user_interface || !control_map || !vats || thread_id == 0) {
        return snapshot;
    }
    if (before_fences_generation != expected_generation) {
        return snapshot;
    }

    auto* menu_stack_count =
        reinterpret_cast<volatile std::uint32_t*>(image + menu_stack_lock_rva + lock_word_offset);
    auto* context_stack_count = reinterpret_cast<volatile std::uint32_t*>(
        image + context_stack_lock_rva + lock_word_offset);

    CaptureOperations operations{
        .image = image,
        .thread_id = thread_id,
        .expected_generation = expected_generation,
        .ui = user_interface,
        .control_map = control_map,
        .vats = vats,
        .menu_stack_count = menu_stack_count,
        .context_stack_count = context_stack_count,
    };
    const bool copied = with_ui_capture_fences(operations, [&] noexcept {
        if (!operations.copy_scalars(*interns, snapshot)) {
            return false;
        }
        snapshot.generation = expected_generation;
        snapshot.valid = true;
        return true;
    });
    const auto final_generation = ui_transition_generation();
    if (!copied || final_generation != expected_generation) {
        snapshot.valid = false;
        return snapshot;
    }
    return snapshot;
}

} // namespace fallout4_adapter::native
