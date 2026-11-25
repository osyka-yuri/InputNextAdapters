#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace fallout4_adapter::native {
struct OptionalIdMapping {
    std::uint64_t id{};
    std::uint64_t offset{};
};

struct ImageSectionRange {
    std::uint32_t rva{};
    std::uint32_t size{};
    std::uint32_t characteristics{};
};

inline constexpr std::uint32_t image_section_discardable = 0x02000000U;
inline constexpr std::uint32_t image_section_execute = 0x20000000U;
inline constexpr std::uint32_t image_section_read = 0x40000000U;
inline constexpr std::uint32_t image_section_write = 0x80000000U;

inline constexpr std::uint32_t pe_header_probe_size = 0x1000U;
inline constexpr std::size_t pe_max_sections = 96U;

namespace pe_detail {
inline constexpr std::uint16_t dos_signature = 0x5A4DU;
inline constexpr std::size_t dos_header_size = 0x40U;
inline constexpr std::size_t dos_lfanew_offset = 0x3CU;

inline constexpr std::uint32_t nt_signature = 0x00004550U;
inline constexpr std::size_t nt_file_header_offset = 0x04U;
inline constexpr std::size_t nt_file_header_size = 0x14U;
inline constexpr std::uint16_t file_machine_amd64 = 0x8664U;
inline constexpr std::size_t file_machine_offset = 0x00U;
inline constexpr std::size_t file_sections_offset = 0x02U;
inline constexpr std::size_t file_optional_size_offset = 0x10U;

inline constexpr std::size_t nt_optional_header_offset = 0x18U;
inline constexpr std::uint16_t optional_hdr64_magic = 0x020BU;
inline constexpr std::size_t optional_header64_min_size = 0xF0U;
inline constexpr std::size_t optional_section_alignment_offset = 0x20U;
inline constexpr std::size_t optional_size_of_image_offset = 0x38U;
inline constexpr std::size_t optional_size_of_headers_offset = 0x3CU;

inline constexpr std::size_t section_header_size = 0x28U;
inline constexpr std::size_t section_virtual_size_offset = 0x08U;
inline constexpr std::size_t section_virtual_address_offset = 0x0CU;
inline constexpr std::size_t section_raw_size_offset = 0x10U;
inline constexpr std::size_t section_characteristics_offset = 0x24U;

template <class T>
[[nodiscard]] inline T read_unaligned(const std::byte* base, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, base + offset, sizeof(T));
    return value;
}
} // namespace pe_detail

enum class PeParseStatus : std::uint8_t {
    ok,
    header_too_small,
    invalid_dos_signature,
    invalid_e_lfanew,
    invalid_nt_signature,
    invalid_machine,
    invalid_optional_header,
    invalid_image_geometry,
    invalid_section_count,
    invalid_section_entry,
};

[[nodiscard]] constexpr std::string_view pe_parse_status_name(PeParseStatus status) noexcept {
    switch (status) {
    case PeParseStatus::ok:
        return "ok";
    case PeParseStatus::header_too_small:
        return "header span smaller than PE headers";
    case PeParseStatus::invalid_dos_signature:
        return "invalid DOS signature";
    case PeParseStatus::invalid_e_lfanew:
        return "invalid e_lfanew offset";
    case PeParseStatus::invalid_nt_signature:
        return "invalid NT signature";
    case PeParseStatus::invalid_machine:
        return "unexpected non-x64 machine type";
    case PeParseStatus::invalid_optional_header:
        return "invalid or truncated PE32+ optional header";
    case PeParseStatus::invalid_image_geometry:
        return "invalid PE image size, alignment, or SizeOfHeaders";
    case PeParseStatus::invalid_section_count:
        return "invalid PE section count or overflowing section table";
    case PeParseStatus::invalid_section_entry:
        return "invalid, misaligned, overlapping, or out-of-bounds PE section";
    }
    return "unknown PE parse failure";
}

struct PeImageSummary {
    std::uint32_t image_size{};
    std::uint32_t size_of_headers{};
    std::uint32_t section_alignment{};
    std::size_t section_count{};
};

[[nodiscard]] inline bool pe_section_overlaps(std::span<const ImageSectionRange> existing_sections,
    const ImageSectionRange& candidate) noexcept {
    const auto section_start = static_cast<std::uint64_t>(candidate.rva);
    const auto section_end = section_start + candidate.size;
    return std::ranges::any_of(existing_sections, [&](const ImageSectionRange& existing) noexcept {
        const auto existing_end = static_cast<std::uint64_t>(existing.rva) + existing.size;
        return section_start < existing_end && existing.rva < section_end;
    });
}

[[nodiscard]] inline bool validate_and_append_pe_section(const std::byte* section_entry,
    const PeImageSummary& geometry,
    std::span<ImageSectionRange> output_sections,
    std::size_t& recorded) noexcept {
    const auto virtual_size = pe_detail::read_unaligned<std::uint32_t>(section_entry,
        pe_detail::section_virtual_size_offset);
    const auto virtual_address = pe_detail::read_unaligned<std::uint32_t>(section_entry,
        pe_detail::section_virtual_address_offset);
    const auto raw_size =
        pe_detail::read_unaligned<std::uint32_t>(section_entry, pe_detail::section_raw_size_offset);
    const auto characteristics = pe_detail::read_unaligned<std::uint32_t>(section_entry,
        pe_detail::section_characteristics_offset);

    const auto range_size = virtual_size != 0U ? virtual_size : raw_size;
    if (range_size == 0U) {
        return true;
    }
    if (virtual_address < geometry.size_of_headers ||
        (virtual_address & (geometry.section_alignment - 1U)) != 0U ||
        virtual_address >= geometry.image_size ||
        range_size > geometry.image_size - virtual_address) {
        return false;
    }
    const ImageSectionRange candidate{
        .rva = virtual_address,
        .size = range_size,
        .characteristics = characteristics,
    };
    if (pe_section_overlaps(output_sections.first(recorded), candidate)) {
        return false;
    }

    output_sections[recorded++] = candidate;
    return true;
}

[[nodiscard]] inline PeParseStatus parse_pe64_sections(std::span<const std::byte> mapped_headers,
    std::span<ImageSectionRange> output_sections,
    PeImageSummary& summary) noexcept {
    summary = {};
    const auto* const data = mapped_headers.data();
    const auto mapped_size = mapped_headers.size();
    if (!data || mapped_size < pe_detail::dos_header_size) {
        return PeParseStatus::header_too_small;
    }
    if (pe_detail::read_unaligned<std::uint16_t>(data, 0U) != pe_detail::dos_signature) {
        return PeParseStatus::invalid_dos_signature;
    }

    const auto lfanew = pe_detail::read_unaligned<std::int32_t>(data, pe_detail::dos_lfanew_offset);
    constexpr auto min_nt_size =
        pe_detail::nt_optional_header_offset + pe_detail::optional_header64_min_size;
    const auto lfanew_u32 = static_cast<std::uint32_t>(lfanew);
    if (lfanew < 0 || lfanew_u32 < pe_detail::dos_header_size) {
        return PeParseStatus::invalid_e_lfanew;
    }
    const auto nt_offset = static_cast<std::size_t>(lfanew_u32);
    if (nt_offset > mapped_size || min_nt_size > mapped_size - nt_offset) {
        return PeParseStatus::invalid_e_lfanew;
    }
    if (pe_detail::read_unaligned<std::uint32_t>(data, nt_offset) != pe_detail::nt_signature) {
        return PeParseStatus::invalid_nt_signature;
    }

    const auto file_offset = nt_offset + pe_detail::nt_file_header_offset;
    if (pe_detail::read_unaligned<std::uint16_t>(data,
            file_offset + pe_detail::file_machine_offset) != pe_detail::file_machine_amd64) {
        return PeParseStatus::invalid_machine;
    }

    const auto section_count = pe_detail::read_unaligned<std::uint16_t>(data,
        file_offset + pe_detail::file_sections_offset);
    const auto optional_size = pe_detail::read_unaligned<std::uint16_t>(data,
        file_offset + pe_detail::file_optional_size_offset);
    const auto optional_offset = nt_offset + pe_detail::nt_optional_header_offset;
    if (optional_size < pe_detail::optional_header64_min_size ||
        optional_size > mapped_size - optional_offset ||
        pe_detail::read_unaligned<std::uint16_t>(data, optional_offset) !=
            pe_detail::optional_hdr64_magic) {
        return PeParseStatus::invalid_optional_header;
    }

    const PeImageSummary geometry{
        .image_size = pe_detail::read_unaligned<std::uint32_t>(data,
            optional_offset + pe_detail::optional_size_of_image_offset),
        .size_of_headers = pe_detail::read_unaligned<std::uint32_t>(data,
            optional_offset + pe_detail::optional_size_of_headers_offset),
        .section_alignment = pe_detail::read_unaligned<std::uint32_t>(data,
            optional_offset + pe_detail::optional_section_alignment_offset),
        .section_count = section_count,
    };
    const auto first_section_offset = optional_offset + optional_size;
    if (geometry.image_size == 0U || geometry.size_of_headers == 0U ||
        geometry.size_of_headers > geometry.image_size ||
        geometry.size_of_headers < first_section_offset || geometry.section_alignment == 0U ||
        !std::has_single_bit(geometry.section_alignment)) {
        return PeParseStatus::invalid_image_geometry;
    }
    if (section_count == 0U || section_count > output_sections.size() ||
        section_count >
            (geometry.size_of_headers - first_section_offset) / pe_detail::section_header_size) {
        return PeParseStatus::invalid_section_count;
    }
    if (section_count > (mapped_size - first_section_offset) / pe_detail::section_header_size) {
        return PeParseStatus::header_too_small;
    }

    std::size_t recorded = 0U;
    for (std::size_t index = 0; index < section_count; ++index) {
        const auto entry_offset = first_section_offset + (index * pe_detail::section_header_size);
        if (!validate_and_append_pe_section(data + entry_offset,
                geometry,
                output_sections,
                recorded)) {
            return PeParseStatus::invalid_section_entry;
        }
    }

    summary = PeImageSummary{
        .image_size = geometry.image_size,
        .size_of_headers = geometry.size_of_headers,
        .section_alignment = geometry.section_alignment,
        .section_count = recorded,
    };
    return PeParseStatus::ok;
}

enum class OptionalAddressKind : std::uint8_t { code, data, writable_data };

struct OptionalAddressRequest {
    std::uint64_t id{};
    std::uint32_t width{};
    OptionalAddressKind kind{OptionalAddressKind::code};
};

template <std::size_t Count>
struct OptionalAddressResult {
    std::array<std::uint32_t, Count> rvas{};
    std::array<bool, Count> available{};
};

[[nodiscard]] inline bool section_contains_rva(std::span<const ImageSectionRange> sections,
    std::uint32_t image_size,
    std::uint64_t rva,
    std::uint32_t width,
    OptionalAddressKind kind) noexcept {
    if (width == 0U || rva >= image_size || width > image_size - rva) {
        return false;
    }
    for (const auto& section : sections) {
        if (section.rva >= image_size || section.size == 0U ||
            section.size > image_size - section.rva) {
            continue;
        }
        const auto end = static_cast<std::uint64_t>(section.rva) + section.size;
        if (rva < section.rva || rva + width > end) {
            continue;
        }
        if ((section.characteristics & image_section_discardable) != 0U) {
            return false;
        }
        const bool readable = (section.characteristics & image_section_read) != 0U;
        const bool executable = (section.characteristics & image_section_execute) != 0U;
        const bool writable = (section.characteristics & image_section_write) != 0U;
        if (kind == OptionalAddressKind::code) {
            return readable && executable && !writable;
        }
        return readable && !executable && (kind != OptionalAddressKind::writable_data || writable);
    }
    return false;
}

template <std::size_t Count, class MappingRange>
[[nodiscard]] OptionalAddressResult<Count> resolve_optional_addresses(const MappingRange& mappings,
    std::span<const ImageSectionRange> sections,
    std::uint32_t image_size,
    const std::array<OptionalAddressRequest, Count>& requests) noexcept {
    OptionalAddressResult<Count> result{};
    std::array<bool, Count> found{};
    // Iterate the public inverse-table view exactly once. Missing IDs stay unavailable.
    for (const auto& mapping : mappings) {
        for (std::size_t index = 0; index < Count; ++index) {
            if (!found[index] && mapping.id == requests[index].id && mapping.offset <= UINT32_MAX) {
                result.rvas[index] = static_cast<std::uint32_t>(mapping.offset);
                found[index] = true;
            }
        }
    }
    for (std::size_t index = 0; index < Count; ++index) {
        result.available[index] = found[index] && section_contains_rva(sections,
                                                      image_size,
                                                      result.rvas[index],
                                                      requests[index].width,
                                                      requests[index].kind);
    }
    return result;
}
} // namespace fallout4_adapter::native
