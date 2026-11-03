#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

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

inline constexpr std::uint32_t image_section_execute = 0x20000000U;
inline constexpr std::uint32_t image_section_read = 0x40000000U;
inline constexpr std::uint32_t image_section_write = 0x80000000U;

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
    if (width == 0 || rva >= image_size || width > image_size - rva) {
        return false;
    }
    for (const auto& section : sections) {
        const auto end = static_cast<std::uint64_t>(section.rva) + section.size;
        if (rva < section.rva || rva + width > end) {
            continue;
        }
        const bool readable = (section.characteristics & image_section_read) != 0;
        const bool executable = (section.characteristics & image_section_execute) != 0;
        const bool writable = (section.characteristics & image_section_write) != 0;
        if (kind == OptionalAddressKind::code) {
            return readable && executable;
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
