#include "bindings.hpp"

#include "observation.hpp"

#include <RE/B/BGSObjectInstance.h>
#include <RE/B/BSSpinLock.h>
#include <RE/B/BSTEvent.h>
#include <RE/T/TESForm.h> // IWYU pragma: keep

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>

namespace fallout4_adapter::native {
namespace {
constexpr std::uintptr_t register_rva = 0x476000;
constexpr std::uint32_t image_timestamp = 0x6A727FCC;
constexpr std::uint32_t image_size = 0x425A000;

bool matches(const std::byte* image,
    std::uintptr_t rva,
    std::span<const std::uint8_t> bytes) noexcept {
    return rva <= image_size && bytes.size() <= image_size - rva &&
           std::memcmp(image + rva, bytes.data(), bytes.size()) == 0;
}

std::uintptr_t checked_image() noexcept {
    const auto* image = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (!image) {
        return 0;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        static_cast<std::uint32_t>(dos->e_lfanew) > 0x1000U - sizeof(IMAGE_NT_HEADERS64)) {
        return 0;
    }
    const auto* nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE ||
        nt_headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt_headers->FileHeader.TimeDateStamp != image_timestamp ||
        nt_headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt_headers->OptionalHeader.SizeOfImage != image_size) {
        return 0;
    }

    // Exact-build wrapper and payload guards. See docs/bindings.md for the evidence.
    static constexpr std::array<std::uint8_t, 15> registration{
        0x48,
        0x8B,
        0xD1,
        0x48,
        0x8D,
        0x0D,
        0x56,
        0x4E,
        0xC8,
        0x02,
        0xE9,
        0x01,
        0xBB,
        0x00,
        0x00,
    };
    static constexpr std::array<std::uint8_t, 15> unregistration{
        0x48,
        0x8B,
        0xD1,
        0x48,
        0x8D,
        0x0D,
        0x36,
        0x4E,
        0xC8,
        0x02,
        0xE9,
        0x91,
        0xC1,
        0x00,
        0x00,
    };
    static constexpr std::array<std::uint8_t, 59> construction{
        0x48,
        0x8B,
        0x85,
        0x08,
        0x01,
        0x00,
        0x00,
        0xF0,
        0xFF,
        0x40,
        0x28,
        0x4C,
        0x8B,
        0xAD,
        0x00,
        0x01,
        0x00,
        0x00,
        0x4C,
        0x89,
        0x6D,
        0x60,
        0x48,
        0x89,
        0x45,
        0x68,
        0xF0,
        0xFF,
        0x40,
        0x28,
        0x48,
        0x89,
        0x45,
        0x68,
        0x48,
        0x8D,
        0x55,
        0x60,
        0x48,
        0x8D,
        0x44,
        0x24,
        0x58,
        0x48,
        0x8D,
        0x0D,
        0x37,
        0x0C,
        0xC8,
        0x02,
        0x48,
        0x89,
        0x45,
        0x70,
        0xE8,
        0x4E,
        0x76,
        0x00,
        0x00,
    };
    static constexpr std::array<std::uint8_t, 18> vats_payload{
        0x48,
        0x39,
        0x42,
        0x08,
        0x0F,
        0x85,
        0x3A,
        0x02,
        0x00,
        0x00,
        0x48,
        0x8B,
        0x42,
        0x10,
        0x83,
        0x78,
        0x18,
        0x00,
    };
    static constexpr std::array<std::uint8_t, 12> push_input_context_lock{
        0x48,
        0x8D,
        0x0D,
        0x2E,
        0x04,
        0xD3,
        0x01,
        0xE8,
        0x11,
        0x9D,
        0xFE,
        0xFF,
    };
    static constexpr std::array<std::uint8_t, 23> pop_input_context_lock{
        0x48,
        0x8D,
        0x0D,
        0x53,
        0x03,
        0xD3,
        0x01,
        0x44,
        0x89,
        0x7C,
        0x24,
        0x50,
        0x41,
        0x8B,
        0xFF,
        0x44,
        0x8B,
        0xF2,
        0xE8,
        0x2B,
        0x9C,
        0xFE,
        0xFF,
    };
    static constexpr std::array<std::uint8_t, 8>
        ui_helper_entry{0x48, 0x83, 0xEC, 0x28, 0x84, 0xC9, 0x74, 0x27};
    static constexpr std::array<std::uint8_t, 10> ui_helper_state_check{
        0x83,
        0x79,
        0x68,
        0x00,
        0x0F,
        0x85,
        0xAC,
        0x00,
        0x00,
        0x00,
    };
    static constexpr std::array<std::uint8_t, 7>
        ui_helper_start_state{0xC7, 0x41, 0x68, 0x02, 0x00, 0x00, 0x00};
    static constexpr std::array<std::uint8_t, 25> ui_helper_stop_wait{
        0x83,
        0x79,
        0x68,
        0x02,
        0x75,
        0x13,
        0x44,
        0x89,
        0x41,
        0x68,
        0xBA,
        0xFF,
        0xFF,
        0xFF,
        0xFF,
        0x48,
        0x8B,
        0x49,
        0x60,
        0xFF,
        0x15,
        0xB5,
        0xB1,
        0x86,
        0x01,
    };
    static constexpr std::array<std::uint8_t, 5> ui_helper_noop_return{0x48,
        0x83,
        0xC4,
        0x28,
        0xC3};
    static constexpr std::array<std::uint8_t, 8>
        manager_static_storage{0x48, 0x8D, 0x3D, 0xF3, 0xCD, 0x6C, 0x02, 0x48};
    static constexpr std::array<std::uint8_t, 7>
        manager_publish_slot{0x48, 0x89, 0x3D, 0xAD, 0xCD, 0x6C, 0x02};
    static constexpr std::array<std::uint8_t, 7>
        manager_init_state{0x44, 0x89, 0x2D, 0x0D, 0xCE, 0x6C, 0x02};
    static constexpr std::array<std::uint8_t, 7>
        manager_init_shutdown{0x40, 0x88, 0x35, 0xFB, 0xCD, 0x6C, 0x02};
    static constexpr std::array<std::uint8_t, 7>
        manager_init_running{0x40, 0x88, 0x35, 0xEC, 0xCD, 0x6C, 0x02};
    static constexpr std::array<std::uint8_t, 4> manager_kill_shutdown{0xC6, 0x43, 0x6C, 0x01};
    static constexpr std::array<std::uint8_t, 8> manager_kill_running_wait{
        0x0F,
        0xB6,
        0x43,
        0x6E,
        0x84,
        0xC0,
        0x74,
        0x14,
    };
    static constexpr std::array<std::uint8_t, 7>
        manager_kill_clear_slot{0x48, 0x89, 0x2D, 0x06, 0xC3, 0x6C, 0x02};
    static constexpr std::array<std::uint8_t, 6> worker_reset_completion{0xFF,
        0x15,
        0xD1,
        0x8E,
        0x86,
        0x01};
    static constexpr std::array<std::uint8_t, 4> worker_load_ui_state{0x41, 0x8B, 0x4E, 0x68};
    static constexpr std::array<std::uint8_t, 11> worker_ui_state2_loop{
        0x41,
        0x83,
        0x7E,
        0x68,
        0x02,
        0x0F,
        0x84,
        0xA7,
        0xFE,
        0xFF,
        0xFF,
    };
    static constexpr std::array<std::uint8_t, 6> worker_signal_completion{0xFF,
        0x15,
        0xA6,
        0x8B,
        0x86,
        0x01};
    static constexpr std::array<std::uint8_t, 5> worker_clear_running{0x41, 0xC6, 0x46, 0x6E, 0x00};
    static constexpr std::array<std::uint8_t, 19> native_process_lock_release{
        0x44,
        0x89,
        0x82,
        0xD8,
        0x01,
        0x00,
        0x00,
        0x0F,
        0xAE,
        0xF0,
        0xF0,
        0x44,
        0x0F,
        0xB1,
        0x82,
        0xDC,
        0x01,
        0x00,
        0x00,
    };
    static constexpr std::array<std::uint8_t, 29> process_spin_lock_acquire{
        0x48,
        0x89,
        0x74,
        0x24,
        0x18,
        0x57,
        0x48,
        0x83,
        0xEC,
        0x20,
        0x48,
        0x8B,
        0xF9,
        0xFF,
        0x15,
        0x95,
        0x2A,
        0x16,
        0x02,
        0x8B,
        0xF0,
        0x39,
        0x07,
        0x75,
        0x0F,
        0xF0,
        0xFF,
        0x47,
        0x04,
    };
    static constexpr std::array<std::uint8_t, 5> process_messages_lock_call{0xE8,
        0xF1,
        0xD0,
        0x85,
        0xFE};
    static constexpr std::array<std::uint8_t, 6> process_messages_unlock_cas{0xF0,
        0x41,
        0x0F,
        0xB1,
        0x5E,
        0x04};
    static constexpr std::array<std::uint8_t, 5> process_messages_unlock_decrement{0xF0,
        0x41,
        0xFF,
        0x4E,
        0x04};
    static constexpr std::array<std::uint8_t, 12> menu_stack_read_lock{
        0x48,
        0x8D,
        0x0D,
        0x75,
        0xA3,
        0x33,
        0x02,
        0xE8,
        0x88,
        0x64,
        0xBD,
        0xFF,
    };
    static constexpr std::array<std::uint8_t, 12> menu_stack_read_unlock{
        0x48,
        0x8D,
        0x0D,
        0x4A,
        0xA2,
        0x33,
        0x02,
        0xE8,
        0xBD,
        0x65,
        0xBD,
        0xFF,
    };
    static constexpr std::array<std::uint8_t, 12> menu_stack_write_lock{
        0x48,
        0x8D,
        0x0D,
        0xE8,
        0xA1,
        0x33,
        0x02,
        0xE8,
        0xFB,
        0x62,
        0xBD,
        0xFF,
    };
    static constexpr std::array<std::uint8_t, 12> menu_stack_write_unlock{
        0x48,
        0x8D,
        0x0D,
        0xBC,
        0xA1,
        0x33,
        0x02,
        0xE8,
        0x2F,
        0x65,
        0xBD,
        0xFF,
    };
    static constexpr std::array<std::uint8_t, 16> rw_try_read_subset{
        0x33,
        0xC0,
        0xBE,
        0x01,
        0x00,
        0x00,
        0x00,
        0xF0,
        0x0F,
        0xB1,
        0x77,
        0x04,
        0x74,
        0x25,
        0x66,
        0x0F,
    };
    static constexpr std::array<std::uint8_t, 5> rw_read_release{0xF0, 0xFF, 0x49, 0x04, 0xC3};

    struct SignatureCheck {
        std::uintptr_t rva{};
        std::span<const std::uint8_t> bytes;
    };

    static constexpr std::array<SignatureCheck, 35> signature_checks{
        {
            {.rva = register_rva, .bytes = registration},
            {.rva = 0x476020, .bytes = unregistration},
            {.rva = 0x47A1F7, .bytes = construction},
            {.rva = 0xE5D36E, .bytes = vats_payload},
            {.rva = 0x166F763, .bytes = push_input_context_lock},
            {.rva = 0x166F83E, .bytes = pop_input_context_lock},
            {.rva = 0xBD50C0, .bytes = ui_helper_entry},
            {.rva = 0xBD50CF, .bytes = ui_helper_state_check},
            {.rva = 0xBD50D9, .bytes = ui_helper_start_state},
            {.rva = 0xBD514A, .bytes = ui_helper_stop_wait},
            {.rva = 0xBD5185, .bytes = ui_helper_noop_return},
            {.rva = 0xBD4526, .bytes = manager_static_storage},
            {.rva = 0xBD453C, .bytes = manager_publish_slot},
            {.rva = 0xBD4574, .bytes = manager_init_state},
            {.rva = 0xBD458A, .bytes = manager_init_shutdown},
            {.rva = 0xBD459B, .bytes = manager_init_running},
            {.rva = 0xBD4FAA, .bytes = manager_kill_shutdown},
            {.rva = 0xBD4FB4, .bytes = manager_kill_running_wait},
            {.rva = 0xBD4FE3, .bytes = manager_kill_clear_slot},
            {.rva = 0xBD74C9, .bytes = worker_reset_completion},
            {.rva = 0xBD74CF, .bytes = worker_load_ui_state},
            {.rva = 0xBD7653, .bytes = worker_ui_state2_loop},
            {.rva = 0xBD775C, .bytes = worker_signal_completion},
            {.rva = 0xBD77A1, .bytes = worker_clear_running},
            {.rva = 0xBD5127, .bytes = native_process_lock_release},
            {.rva = 0x2DD800, .bytes = process_spin_lock_acquire},
            {.rva = 0x1A8070A, .bytes = process_messages_lock_call},
            {.rva = 0x1A81790, .bytes = process_messages_unlock_cas},
            {.rva = 0x1A817E1, .bytes = process_messages_unlock_decrement},
            {.rva = 0x1A82FEC, .bytes = menu_stack_read_lock},
            {.rva = 0x1A83117, .bytes = menu_stack_read_unlock},
            {.rva = 0x1A83179, .bytes = menu_stack_write_lock},
            {.rva = 0x1A831A5, .bytes = menu_stack_write_unlock},
            {.rva = 0x165953C, .bytes = rw_try_read_subset},
            {.rva = 0x16596D0, .bytes = rw_read_release},
        },
    };
    for (const auto& signature : signature_checks) {
        if (!matches(image, signature.rva, signature.bytes)) {
            return 0;
        }
    }
    return reinterpret_cast<std::uintptr_t>(image);
}

std::uintptr_t image_base() noexcept {
    static const auto base = checked_image();
    return base;
}

// Only the observed prefix is decoded; no undocumented collection template is declared.
struct FiredPrefix {
    const RE::BGSObjectInstance* weapon{};
    std::uintptr_t shooter{};
    const std::byte* projectiles{};
};

static_assert(sizeof(FiredPrefix) == 0x18);
static_assert(offsetof(FiredPrefix, shooter) == 8 && offsetof(FiredPrefix, projectiles) == 0x10);
} // namespace

bool bindings_available() noexcept {
    return image_base() != 0;
}

std::uintptr_t checked_image_base() noexcept {
    return image_base();
}

RE::BSReadWriteLock* input_context_stack_lock() noexcept {
    const auto base = image_base();
    return base ? reinterpret_cast<RE::BSReadWriteLock*>(base + 0x339FB98) : nullptr;
}

bool register_weapon_fired(const RE::BSTEventSink<WeaponFiredEvent>* sink) noexcept {
    const auto base = image_base();
    if (!base || !sink) {
        return false;
    }
    using Register = void (*)(const RE::BSTEventSink<WeaponFiredEvent>*);
    reinterpret_cast<Register>(base + register_rva)(sink);
    return true;
}

std::optional<ShotFacts> decode_weapon_fired(const WeaponFiredEvent& event) noexcept {
    if (!bindings_available()) {
        return std::nullopt;
    }
    FiredPrefix prefix{};
    std::memcpy(&prefix, std::addressof(event), sizeof(prefix));
    if (!prefix.weapon || !prefix.weapon->object || !prefix.shooter || !prefix.projectiles) {
        return std::nullopt;
    }
    const std::byte* data{};
    std::uint32_t count{};
    std::memcpy(static_cast<void*>(&data), prefix.projectiles + 8, sizeof(data));
    std::memcpy(&count, prefix.projectiles + 0x18, sizeof(count));
    if (!data || count == 0) {
        return std::nullopt;
    }

    // Failed launches can append null entries. Inspect a bounded prefix of borrowed pointers.
    constexpr std::uint32_t maximum_inspected_projectiles = 64;
    for (std::uint32_t index = 0; index < std::min(count, maximum_inspected_projectiles); ++index) {
        std::uintptr_t projectile{};
        std::memcpy(&projectile, data + (sizeof(projectile) * index), sizeof(projectile));
        if (projectile != 0) {
            const auto identity = weapon_identity(prefix.weapon->object->GetFormID(),
                reinterpret_cast<std::uintptr_t>(prefix.weapon->instanceData.get()));
            if (identity != 0) {
                return ShotFacts{.shooter_address = prefix.shooter, .weapon_identity = identity};
            }
            break;
        }
    }
    return std::nullopt;
}
} // namespace fallout4_adapter::native
