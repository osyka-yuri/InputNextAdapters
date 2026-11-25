#include "bindings.hpp"

#include "animation_subscription.hpp"
#include "diagnostic_logging.hpp"
#include "dispatch.hpp"
#include "observation.hpp"
#include "optional_id_map.hpp"
#include "ui_capture.hpp"

#include <RE/B/BGSObjectInstance.h>
#include <RE/B/BSSpinLock.h>
#include <RE/B/BSTEvent.h>
#include <RE/T/TESForm.h> // IWYU pragma: keep

#include <Windows.h>
#ifdef ERROR
#    undef ERROR
#endif

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
struct CheckedModule {
    std::uintptr_t base{};
    std::size_t size{};
    std::array<ImageSectionRange, pe_max_sections> sections{};
    std::size_t section_count{};
};

struct AddressRange {
    std::uintptr_t rva{};
    std::size_t size{};
    OptionalAddressKind kind{OptionalAddressKind::code};
};

static_assert(pe_detail::dos_signature == IMAGE_DOS_SIGNATURE);
static_assert(pe_detail::dos_header_size == sizeof(IMAGE_DOS_HEADER));
static_assert(pe_detail::dos_lfanew_offset == offsetof(IMAGE_DOS_HEADER, e_lfanew));
static_assert(pe_detail::nt_signature == IMAGE_NT_SIGNATURE);
static_assert(pe_detail::nt_file_header_offset == offsetof(IMAGE_NT_HEADERS64, FileHeader));
static_assert(pe_detail::nt_file_header_size == sizeof(IMAGE_FILE_HEADER));
static_assert(pe_detail::file_machine_amd64 == IMAGE_FILE_MACHINE_AMD64);
static_assert(pe_detail::file_machine_offset == offsetof(IMAGE_FILE_HEADER, Machine));
static_assert(pe_detail::file_sections_offset == offsetof(IMAGE_FILE_HEADER, NumberOfSections));
static_assert(
    pe_detail::file_optional_size_offset == offsetof(IMAGE_FILE_HEADER, SizeOfOptionalHeader));
static_assert(pe_detail::nt_optional_header_offset == offsetof(IMAGE_NT_HEADERS64, OptionalHeader));
static_assert(pe_detail::optional_hdr64_magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
static_assert(pe_detail::optional_header64_min_size == sizeof(IMAGE_OPTIONAL_HEADER64));
static_assert(pe_detail::optional_section_alignment_offset ==
              offsetof(IMAGE_OPTIONAL_HEADER64, SectionAlignment));
static_assert(
    pe_detail::optional_size_of_image_offset == offsetof(IMAGE_OPTIONAL_HEADER64, SizeOfImage));
static_assert(
    pe_detail::optional_size_of_headers_offset == offsetof(IMAGE_OPTIONAL_HEADER64, SizeOfHeaders));
static_assert(pe_detail::section_header_size == sizeof(IMAGE_SECTION_HEADER));
static_assert(pe_detail::section_virtual_size_offset == offsetof(IMAGE_SECTION_HEADER, Misc));
static_assert(
    pe_detail::section_virtual_address_offset == offsetof(IMAGE_SECTION_HEADER, VirtualAddress));
static_assert(pe_detail::section_raw_size_offset == offsetof(IMAGE_SECTION_HEADER, SizeOfRawData));
static_assert(
    pe_detail::section_characteristics_offset == offsetof(IMAGE_SECTION_HEADER, Characteristics));
static_assert(image_section_discardable == IMAGE_SCN_MEM_DISCARDABLE);
static_assert(image_section_execute == IMAGE_SCN_MEM_EXECUTE);
static_assert(image_section_read == IMAGE_SCN_MEM_READ);
static_assert(image_section_write == IMAGE_SCN_MEM_WRITE);

[[nodiscard]] constexpr bool
range_within_image(std::uintptr_t rva, std::size_t access_size, std::size_t image_size) noexcept {
    return rva <= image_size && access_size <= image_size - rva;
}

[[nodiscard]] constexpr const char* address_kind_label(OptionalAddressKind kind) noexcept {
    switch (kind) {
    case OptionalAddressKind::code:
        return "executable code";
    case OptionalAddressKind::data:
        return "readable data";
    case OptionalAddressKind::writable_data:
        return "writable data";
    }
    return "unknown";
}

bool matches(const std::byte* image,
    std::uintptr_t rva,
    std::span<const std::uint8_t> bytes,
    std::span<const ImageSectionRange> sections,
    std::size_t image_size) noexcept {
    return image_size <= UINT32_MAX && bytes.size() <= UINT32_MAX &&
           section_contains_rva(sections,
               static_cast<std::uint32_t>(image_size),
               rva,
               static_cast<std::uint32_t>(bytes.size()),
               OptionalAddressKind::code) &&
           range_within_image(rva, bytes.size(), image_size) &&
           std::memcmp(image + rva, bytes.data(), bytes.size()) == 0;
}

CheckedModule find_checked_module() noexcept {
    const auto* image = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
    if (!image) {
        return {};
    }

    CheckedModule module{};
    PeImageSummary summary{};
    const auto parse_status =
        parse_pe64_sections(std::span<const std::byte>{image, pe_header_probe_size},
            module.sections,
            summary);
    if (parse_status != PeParseStatus::ok) {
        log_warn("Fallout 4 executable PE validation failed ({}); compatibility not confirmed.",
            pe_parse_status_name(parse_status));
        return {};
    }
    module.section_count = summary.section_count;
    const auto image_size = static_cast<std::size_t>(summary.image_size);
    const std::span<const ImageSectionRange> sections{module.sections.data(), module.section_count};

    // Verified instruction signatures and payload guards. See docs/bindings.md for details.
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

    static constexpr std::array<SignatureCheck, 30> signature_checks{
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
        if (!matches(image, signature.rva, signature.bytes, sections, image_size)) {
            log_warn("Fallout 4 verified instruction signature mismatch at RVA 0x{:X}; "
                     "compatibility not confirmed.",
                signature.rva);
            return {};
        }
    }

    // Static address ranges accessed by the native adapter layer (locks, manager instance, hook sites).
    constexpr std::array required_static_ranges{
        AddressRange{.rva = register_rva,
            .size = registration.size(),
            .kind = OptionalAddressKind::code},
        AddressRange{.rva = input_context_stack_lock_rva,
            .size = sizeof(RE::BSReadWriteLock),
            .kind = OptionalAddressKind::writable_data},
        AddressRange{.rva = ui_static_slot_rva,
            .size = sizeof(std::uintptr_t),
            .kind = OptionalAddressKind::writable_data},
        AddressRange{.rva = ui_static_storage_rva,
            .size = ui_static_storage_size,
            .kind = OptionalAddressKind::writable_data},
        AddressRange{.rva = menu_stack_lock_rva,
            .size = sizeof(RE::BSReadWriteLock),
            .kind = OptionalAddressKind::writable_data},
        AddressRange{.rva = native_dispatch_helper_rva,
            .size = native_dispatch_helper_guard.size(),
            .kind = OptionalAddressKind::code},
        AddressRange{.rva = native_dispatch_main_call_rva - native_dispatch_main_arguments.size(),
            .size =
                native_dispatch_main_arguments.size() + native_dispatch_main_original_call.size(),
            .kind = OptionalAddressKind::code},
        AddressRange{.rva = native_dispatch_main_message_loop_rva,
            .size = 5,
            .kind = OptionalAddressKind::code},
        AddressRange{.rva = native_dispatch_remap_button_rva,
            .size = native_dispatch_remap_guard.size(),
            .kind = OptionalAddressKind::code},
        AddressRange{.rva = graph_reference_acquire_rva,
            .size = 4,
            .kind = OptionalAddressKind::code},
        AddressRange{.rva = graph_reference_release_rva,
            .size = 25,
            .kind = OptionalAddressKind::code},
        AddressRange{.rva = animation_source_rva, .size = 9, .kind = OptionalAddressKind::code},
    };
    for (const auto& range : required_static_ranges) {
        if (range.size > UINT32_MAX || !range_within_image(range.rva, range.size, image_size)) {
            log_warn("Fallout 4 executable image size (0x{:X}) is insufficient for required "
                     "static range at RVA 0x{:X} (size 0x{:X}); compatibility not confirmed.",
                image_size,
                range.rva,
                range.size);
            return {};
        }
        if (!section_contains_rva(sections,
                static_cast<std::uint32_t>(image_size),
                range.rva,
                static_cast<std::uint32_t>(range.size),
                range.kind)) {
            log_warn("Fallout 4 required static range at RVA 0x{:X} (size 0x{:X}) does not "
                     "reside in a compatible {} section; compatibility not confirmed.",
                range.rva,
                range.size,
                address_kind_label(range.kind));
            return {};
        }
    }
    module.base = reinterpret_cast<std::uintptr_t>(image);
    module.size = image_size;
    return module;
}

const CheckedModule& checked_image() noexcept {
    static const CheckedModule module = find_checked_module();
    return module;
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
    return checked_image().base != 0;
}

std::uintptr_t checked_image_base() noexcept {
    return checked_image().base;
}

std::size_t checked_image_size() noexcept {
    return checked_image().size;
}

bool is_equipped_weapon_data(const RE::EquippedItemData& data) noexcept {
    static const auto expected_vtable = [] noexcept -> std::uintptr_t {
        // RE::VTABLE::EquippedWeaponData in the supported 1.11.240 executable.
        constexpr std::uintptr_t rva = 0x256aee0;
        constexpr std::array<std::uintptr_t, 5> method_rvas{0xd4a460,
            0x16babf0,
            0xd30b90,
            0xd30c20,
            0xd30d60};
        const auto& image = checked_image();
        if (image.base == 0 || !range_within_image(rva, sizeof(method_rvas), image.size)) {
            return 0;
        }
        std::array<std::uintptr_t, method_rvas.size()> methods{};
        std::memcpy(methods.data(),
            reinterpret_cast<const void*>(image.base + rva),
            sizeof(methods));
        for (std::size_t index = 0; index < methods.size(); ++index) {
            if (methods[index] != image.base + method_rvas[index]) {
                return 0;
            }
        }
        return image.base + rva;
    }();
    if (expected_vtable == 0) {
        return false;
    }
    std::uintptr_t vtable{};
    std::memcpy(&vtable, std::addressof(data), sizeof(vtable));
    return vtable == expected_vtable;
}

std::span<const ImageSectionRange> checked_image_sections() noexcept {
    const auto& module = checked_image();
    return {module.sections.data(), module.section_count};
}

RE::BSReadWriteLock* input_context_stack_lock() noexcept {
    const auto base = checked_image_base();
    return base ? reinterpret_cast<RE::BSReadWriteLock*>(base + input_context_stack_lock_rva)
                : nullptr;
}

bool register_weapon_fired(const RE::BSTEventSink<WeaponFiredEvent>* sink) noexcept {
    const auto base = checked_image_base();
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
