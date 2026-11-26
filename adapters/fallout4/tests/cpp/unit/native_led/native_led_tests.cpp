#include "native/optional_id_map.hpp"
#include "native/radiation_cache.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {
void check(bool condition, const char* message) {
    INFO(message);
    CHECK(condition);
}

template <class T>
void write_le(std::span<std::byte> buffer, std::size_t offset, T value) {
    REQUIRE(offset + sizeof(T) <= buffer.size());
    std::memcpy(buffer.data() + offset, &value, sizeof(T));
}

struct SyntheticSectionSpec {
    std::uint32_t virtual_size{};
    std::uint32_t virtual_address{};
    std::uint32_t raw_size{};
    std::uint32_t characteristics{};
};

struct SyntheticPeOptions {
    std::uint32_t size_of_image{0x5000U};
    std::uint32_t size_of_headers{0x400U};
    std::uint32_t section_alignment{0x1000U};
    std::uint32_t lfanew{0x80U};
    std::uint16_t optional_header_size{0xF0U};
    std::size_t buffer_size{0x1000U};
};

std::vector<std::byte> build_synthetic_pe64(std::span<const SyntheticSectionSpec> sections,
    const SyntheticPeOptions& options = {}) {
    using namespace fallout4_adapter::native::pe_detail;
    std::vector<std::byte> image(options.buffer_size, std::byte{0});
    write_le<std::uint16_t>(image, 0U, dos_signature);
    write_le<std::int32_t>(image, dos_lfanew_offset, static_cast<std::int32_t>(options.lfanew));

    write_le<std::uint32_t>(image, options.lfanew, nt_signature);
    const std::size_t file_offset = options.lfanew + nt_file_header_offset;
    write_le<std::uint16_t>(image, file_offset + file_machine_offset, file_machine_amd64);
    write_le<std::uint16_t>(image,
        file_offset + file_sections_offset,
        static_cast<std::uint16_t>(sections.size()));
    write_le<std::uint16_t>(image,
        file_offset + file_optional_size_offset,
        options.optional_header_size);

    const std::size_t optional_offset = options.lfanew + nt_optional_header_offset;
    write_le<std::uint16_t>(image, optional_offset, optional_hdr64_magic);
    write_le<std::uint32_t>(image,
        optional_offset + optional_section_alignment_offset,
        options.section_alignment);
    write_le<std::uint32_t>(image,
        optional_offset + optional_size_of_image_offset,
        options.size_of_image);
    write_le<std::uint32_t>(image,
        optional_offset + optional_size_of_headers_offset,
        options.size_of_headers);

    const std::size_t first_section = optional_offset + options.optional_header_size;
    for (std::size_t index = 0; index < sections.size(); ++index) {
        const std::size_t entry = first_section + (index * section_header_size);
        write_le<std::uint32_t>(image,
            entry + section_virtual_size_offset,
            sections[index].virtual_size);
        write_le<std::uint32_t>(image,
            entry + section_virtual_address_offset,
            sections[index].virtual_address);
        write_le<std::uint32_t>(image, entry + section_raw_size_offset, sections[index].raw_size);
        write_le<std::uint32_t>(image,
            entry + section_characteristics_offset,
            sections[index].characteristics);
    }
    return image;
}

struct RipSlotFixup {
    std::size_t disp_offset{};
    std::uint32_t factory_rva{};
    std::uint32_t next_ip_offset{};
    std::uint32_t target_rva{};
};

void write_disp32(std::span<std::byte> code, const RipSlotFixup& fixup) {
    const auto disp = static_cast<std::int32_t>(
        static_cast<std::int64_t>(fixup.target_rva) -
        (static_cast<std::int64_t>(fixup.factory_rva) + fixup.next_ip_offset));
    write_le<std::int32_t>(code, fixup.disp_offset, disp);
}

std::vector<std::byte> build_radiation_factory_bytes(
    fallout4_adapter::native::RadiationFactoryBinding binding) {
    using fallout4_adapter::native::radiation_factory_expected_size;
    std::vector<std::byte> code(radiation_factory_expected_size, std::byte{0x90});
    constexpr std::array<std::uint8_t, 7> prologue{0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0x3D};
    constexpr std::array<std::uint8_t, 7> jne_op{0x00, 0x0F, 0x85, 0xB0, 0x00, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 3> mov_kill{0x48, 0x8B, 0x19};
    constexpr std::array<std::uint8_t, 3> mov_slot{0x48, 0x89, 0x3D};
    constexpr std::array<std::uint8_t, 5> epilogue{0x48, 0x83, 0xC4, 0x28, 0xC3};
    std::memcpy(code.data(), prologue.data(), prologue.size());
    std::memcpy(code.data() + 0x0BU, jne_op.data(), jne_op.size());
    std::memcpy(code.data() + 0x1EU, mov_kill.data(), mov_kill.size());
    std::memcpy(code.data() + 0x43U, mov_slot.data(), mov_slot.size());
    std::memcpy(code.data() + 0xB6U, mov_slot.data(), mov_slot.size());
    std::memcpy(code.data() + 0xC2U, epilogue.data(), epilogue.size());
    write_disp32(code,
        {.disp_offset = 0x07U,
            .factory_rva = binding.factory_rva,
            .next_ip_offset = 0x0CU,
            .target_rva = binding.source_slot_rva});
    write_disp32(code,
        {.disp_offset = 0x46U,
            .factory_rva = binding.factory_rva,
            .next_ip_offset = 0x4AU,
            .target_rva = binding.source_slot_rva});
    write_disp32(code,
        {.disp_offset = 0xB9U,
            .factory_rva = binding.factory_rva,
            .next_ip_offset = 0xBDU,
            .target_rva = binding.source_slot_rva});
    return code;
}
} // namespace

TEST_CASE("fallout4.native_led.radiation_unknown_zero_positive_and_lifecycle_race",
    "[unit][fallout4][native_led]") {
    using fallout4_adapter::native::RadiationCountCache;
    RadiationCountCache cache;
    check(!cache.current_count(), "startup count remains unknown until a real event");
    check(cache.enable(), "admitted gameplay enables event publication");
    check(!cache.current_count(), "enabling does not fabricate zero exposure");

    const auto zero_token = cache.capture_event_token();
    const auto zero_token_value = zero_token.value_or(RadiationCountCache::EventToken{});
    check(zero_token.has_value() && cache.publish(zero_token_value, 0U),
        "a real zero-count event establishes known zero");
    check(cache.current_count() == 0U, "known zero remains distinct from unknown");

    const auto positive_token = cache.capture_event_token();
    const auto positive_token_value = positive_token.value_or(RadiationCountCache::EventToken{});
    check(positive_token.has_value() && cache.publish(positive_token_value, 3U),
        "a real positive source count is published");
    check(cache.current_count() == 3U, "positive source count is copied without scaling");

    const auto unavailable_token = cache.capture_event_token();
    const auto unavailable_token_value =
        unavailable_token.value_or(RadiationCountCache::EventToken{});
    check(unavailable_token.has_value() && cache.publish(unavailable_token_value, std::nullopt),
        "a source event without a value explicitly withdraws cached knowledge");
    check(!cache.current_count(), "unavailable source data must not retain positive exposure");

    const auto stale_token = cache.capture_event_token();
    const auto stale_token_value = stale_token.value_or(RadiationCountCache::EventToken{});
    check(stale_token.has_value(), "an admitted cache exposes an event token");
    check(cache.invalidate(), "load lifecycle advances generation");
    check(!cache.current_count(), "lifecycle invalidation clears knowledge immediately");
    check(cache.enable(), "fresh admitted gameplay opens the new generation");
    check(!cache.publish(stale_token_value, 8U),
        "a pre-reset event cannot publish across invalidation and re-enable");
    check(!cache.current_count(), "the stale publication cannot restore old radiation");
}

TEST_CASE("fallout4.native_led.radiation_generation_exhaustion_disables_without_wrap",
    "[unit][fallout4][native_led]") {
    using fallout4_adapter::native::RadiationCountCache;
    RadiationCountCache cache{RadiationCountCache::maximum_generation};
    check(cache.enable(), "the final valid lifecycle generation can be admitted");
    const auto token = cache.capture_event_token();
    const auto token_value = token.value_or(RadiationCountCache::EventToken{});
    check(token.has_value() && cache.publish(token_value, 2U),
        "final generation accepts its current event");
    check(!cache.invalidate(), "exhaustion disables publication instead of wrapping");
    const auto snapshot = cache.snapshot();
    check(snapshot.exhausted && !snapshot.accepting && !snapshot.known,
        "exhausted generation is disabled and unknown");
    check(!cache.enable() && !cache.capture_event_token(),
        "an exhausted cache cannot be re-enabled");
    check(!cache.publish(token_value, 7U), "the final old event cannot cross exhaustion");
}

TEST_CASE("fallout4.native_led.optional_ids_require_presence_and_matching_image_sections",
    "[unit][fallout4][native_led]") {
    using namespace fallout4_adapter::native;
    constexpr std::array mappings{
        OptionalIdMapping{.id = 101U, .offset = 0x110U},
        OptionalIdMapping{.id = 102U, .offset = 0x220U},
        OptionalIdMapping{.id = 103U, .offset = 0x230U},
    };
    constexpr std::array sections{
        ImageSectionRange{.rva = 0x100U,
            .size = 0x80U,
            .characteristics = image_section_read | image_section_execute},
        ImageSectionRange{.rva = 0x200U,
            .size = 0x80U,
            .characteristics = image_section_read | image_section_write},
    };
    constexpr std::array requests{
        OptionalAddressRequest{.id = 101U, .width = 1U, .kind = OptionalAddressKind::code},
        OptionalAddressRequest{.id = 102U, .width = 8U, .kind = OptionalAddressKind::data},
        OptionalAddressRequest{.id = 103U, .width = 8U, .kind = OptionalAddressKind::code},
        OptionalAddressRequest{.id = 104U, .width = 8U, .kind = OptionalAddressKind::data},
    };
    const auto result =
        resolve_optional_addresses(mappings, std::span{sections}, 0x1000U, requests);
    check(result.available[0] && result.rvas[0] == 0x110U,
        "code IDs resolve only into a readable executable section");
    check(result.available[1] && result.rvas[1] == 0x220U,
        "data IDs resolve only into readable non-executable data");
    check(!result.available[2], "a code ID mapped into a data section is rejected");
    check(!result.available[3] && result.rvas[3] == 0U,
        "missing IDs remain unavailable without a fatal lookup");
    check(!section_contains_rva(std::span{sections},
              0x1000U,
              0x27CU,
              8U,
              OptionalAddressKind::writable_data),
        "data pointers that cross a section boundary are rejected");
}

TEST_CASE("fallout4.native_led.radiation_factory_and_slot_section_constraints",
    "[unit][fallout4][native_led]") {
    using namespace fallout4_adapter::native;
    constexpr std::array mappings{
        OptionalIdMapping{.id = 1001U, .offset = 0x110U},    // IsSneaking
        OptionalIdMapping{.id = 1002U, .offset = 0x310U},    // ProcessLists::Singleton
        OptionalIdMapping{.id = 1003U, .offset = 0x120U},    // RequestHighestDetectionLevel
        OptionalIdMapping{.id = 4803487U, .offset = 0x410U}, // CurrentRadiationSourceCount slot
        OptionalIdMapping{.id = 4796078U, .offset = 0x320U}, // BSTGlobalEvent::Singleton
        OptionalIdMapping{.id = 2248914U, .offset = 0x140U}, // Radiation factory helper
    };
    constexpr std::array sections{
        ImageSectionRange{.rva = 0x100U,
            .size = 0x180U,
            .characteristics = image_section_read | image_section_execute},
        ImageSectionRange{.rva = 0x300U, .size = 0x80U, .characteristics = image_section_read},
        ImageSectionRange{.rva = 0x400U,
            .size = 0x80U,
            .characteristics = image_section_read | image_section_write},
    };
    constexpr std::array led_requests{
        OptionalAddressRequest{.id = 1001U, .width = 1U, .kind = OptionalAddressKind::code},
        OptionalAddressRequest{.id = 1002U, .width = 8U, .kind = OptionalAddressKind::data},
        OptionalAddressRequest{.id = 1003U, .width = 1U, .kind = OptionalAddressKind::code},
        OptionalAddressRequest{.id = 4803487U,
            .width = 8U,
            .kind = OptionalAddressKind::writable_data},
        OptionalAddressRequest{.id = 4796078U, .width = 8U, .kind = OptionalAddressKind::data},
        OptionalAddressRequest{.id = 2248914U,
            .width = radiation_factory_expected_size,
            .kind = OptionalAddressKind::code},
    };

    const auto full =
        resolve_optional_addresses(mappings, std::span{sections}, 0x1000U, led_requests);
    check(full.available[3] && full.rvas[3] == 0x410U && full.available[4] &&
              full.rvas[4] == 0x320U && full.available[5] && full.rvas[5] == 0x140U,
        "radiation source slot, global singleton, and full-width engine factory helper resolve "
        "only "
        "in their respective writable data, data, and executable code sections");

    constexpr std::uint32_t exact_fit_rva = 0x280U - radiation_factory_expected_size;
    constexpr std::array exact_fit_mappings{
        OptionalIdMapping{.id = 4803487U, .offset = 0x410U},
        OptionalIdMapping{.id = 4796078U, .offset = 0x320U},
        OptionalIdMapping{.id = 2248914U, .offset = exact_fit_rva},
    };
    const auto exact_fit =
        resolve_optional_addresses(exact_fit_mappings, std::span{sections}, 0x1000U, led_requests);
    check(exact_fit.available[5] && exact_fit.rvas[5] == exact_fit_rva,
        "factory helper ending exactly at the executable section boundary is accepted");

    constexpr std::array overflowing_mappings{
        OptionalIdMapping{.id = 4803487U, .offset = 0x410U},
        OptionalIdMapping{.id = 4796078U, .offset = 0x320U},
        OptionalIdMapping{.id = 2248914U,
            .offset = exact_fit_rva + 1U}, // starts in .text, overflows
    };
    const auto overflowed = resolve_optional_addresses(overflowing_mappings,
        std::span{sections},
        0x1000U,
        led_requests);
    check(overflowed.rvas[5] == exact_fit_rva + 1U && !overflowed.available[5],
        "factory helper starting inside .text whose 0xC7 span overflows section bounds is "
        "rejected");

    constexpr std::array misclassified_mappings{
        OptionalIdMapping{.id = 4803487U, .offset = 0x310U}, // read-only section
        OptionalIdMapping{.id = 4796078U, .offset = 0x320U},
        OptionalIdMapping{.id = 2248914U, .offset = 0x420U}, // writable data instead of code
    };
    const auto rejected = resolve_optional_addresses(misclassified_mappings,
        std::span{sections},
        0x1000U,
        led_requests);
    check(!rejected.available[3] && !rejected.available[5] && rejected.available[4],
        "radiation source slot in read-only memory and factory helper outside .text are rejected");
}

TEST_CASE("fallout4.native_led.pe64_parser_and_section_security_checks",
    "[unit][fallout4][native_led]") {
    using namespace fallout4_adapter::native;

    std::array<ImageSectionRange, pe_max_sections> parsed_sections{};
    PeImageSummary summary{};

    // Valid PE64 image with a discardable section.
    constexpr std::array valid_specs{
        SyntheticSectionSpec{.virtual_size = 0x800U,
            .virtual_address = 0x1000U,
            .raw_size = 0x800U,
            .characteristics = image_section_read | image_section_execute},
        SyntheticSectionSpec{.virtual_size = 0x1000U,
            .virtual_address = 0x2000U,
            .raw_size = 0x1000U,
            .characteristics = image_section_read},
        SyntheticSectionSpec{.virtual_size = 0x400U,
            .virtual_address = 0x3000U,
            .raw_size = 0x200U,
            .characteristics = image_section_read | image_section_write},
        SyntheticSectionSpec{.virtual_size = 0x200U,
            .virtual_address = 0x4000U,
            .raw_size = 0x200U,
            .characteristics = image_section_read | image_section_discardable},
    };
    const auto valid_pe = build_synthetic_pe64(valid_specs);
    check(parse_pe64_sections(valid_pe, parsed_sections, summary) == PeParseStatus::ok &&
              summary.section_count == 4U && summary.image_size == 0x5000U &&
              summary.size_of_headers == 0x400U && summary.section_alignment == 0x1000U,
        "valid PE64 image with contiguous sections parses completely");

    const std::span<const ImageSectionRange> valid_span{parsed_sections.data(),
        summary.section_count};
    check(!section_contains_rva(valid_span,
              summary.image_size,
              0x4010U,
              4U,
              OptionalAddressKind::data),
        "discardable sections (.reloc) are excluded from data lookups");

    constexpr std::array rwx_section{
        ImageSectionRange{.rva = 0x1000U,
            .size = 0x100U,
            .characteristics = image_section_read | image_section_write | image_section_execute},
    };
    check(!section_contains_rva(rwx_section, 0x2000U, 0x1010U, 1U, OptionalAddressKind::code),
        "writable executable sections are rejected for code bindings");

    // Malformed PE headers.
    const std::array<std::byte, 32> tiny_buffer{};
    check(parse_pe64_sections(tiny_buffer, parsed_sections, summary) ==
              PeParseStatus::header_too_small,
        "truncated buffer smaller than DOS header is rejected");

    auto bad_dos = valid_pe;
    write_le<std::uint16_t>(bad_dos, 0U, 0x1234U);
    check(parse_pe64_sections(bad_dos, parsed_sections, summary) ==
              PeParseStatus::invalid_dos_signature,
        "invalid DOS signature is rejected");

    auto overlap_lfanew = valid_pe;
    write_le<std::int32_t>(overlap_lfanew, pe_detail::dos_lfanew_offset, 0x20);
    check(parse_pe64_sections(overlap_lfanew, parsed_sections, summary) ==
              PeParseStatus::invalid_e_lfanew,
        "e_lfanew overlapping DOS header (< 0x40) is rejected");

    const auto unaligned_lfanew = build_synthetic_pe64(valid_specs, {.lfanew = 0x82U});
    check(parse_pe64_sections(unaligned_lfanew, parsed_sections, summary) == PeParseStatus::ok &&
              summary.section_count == 4U,
        "valid unaligned e_lfanew is accepted because header reads are unaligned");

    auto oob_lfanew = valid_pe;
    write_le<std::int32_t>(oob_lfanew, pe_detail::dos_lfanew_offset, 0xF80);
    check(parse_pe64_sections(oob_lfanew, parsed_sections, summary) ==
              PeParseStatus::invalid_e_lfanew,
        "e_lfanew leaving insufficient space for NT64 header is rejected");

    auto bad_nt = valid_pe;
    write_le<std::uint32_t>(bad_nt, 0x80U, 0x0000454EU);
    check(parse_pe64_sections(bad_nt, parsed_sections, summary) ==
              PeParseStatus::invalid_nt_signature,
        "invalid NT signature is rejected");

    auto x86_machine = valid_pe;
    write_le<std::uint16_t>(x86_machine, 0x80U + pe_detail::nt_file_header_offset, 0x014CU);
    check(parse_pe64_sections(x86_machine, parsed_sections, summary) ==
              PeParseStatus::invalid_machine,
        "non-x64 COFF machine type is rejected");

    const auto small_optional = build_synthetic_pe64(valid_specs, {.optional_header_size = 0xE0U});
    check(parse_pe64_sections(small_optional, parsed_sections, summary) ==
              PeParseStatus::invalid_optional_header,
        "undersized SizeOfOptionalHeader (< 0xF0) is rejected");

    auto pe32_magic = valid_pe;
    write_le<std::uint16_t>(pe32_magic, 0x80U + pe_detail::nt_optional_header_offset, 0x010BU);
    check(parse_pe64_sections(pe32_magic, parsed_sections, summary) ==
              PeParseStatus::invalid_optional_header,
        "PE32 optional magic is rejected");

    const auto bad_alignment = build_synthetic_pe64(valid_specs, {.section_alignment = 0x3000U});
    check(parse_pe64_sections(bad_alignment, parsed_sections, summary) ==
              PeParseStatus::invalid_image_geometry,
        "non-power-of-two SectionAlignment is rejected");

    const auto oversized_headers = build_synthetic_pe64(valid_specs, {.size_of_headers = 0x6000U});
    check(parse_pe64_sections(oversized_headers, parsed_sections, summary) ==
              PeParseStatus::invalid_image_geometry,
        "SizeOfHeaders exceeding SizeOfImage is rejected");

    const auto large_declared_headers =
        build_synthetic_pe64(std::span{valid_specs}.subspan(1), {.size_of_headers = 0x1200U});
    check(parse_pe64_sections(large_declared_headers, parsed_sections, summary) ==
                  PeParseStatus::ok &&
              summary.section_count == 3U,
        "SizeOfHeaders exceeding 4 KiB probe succeeds when section table fits in probe buffer");

    check(parse_pe64_sections(std::span{valid_pe}.first(0x1B0U), parsed_sections, summary) ==
              PeParseStatus::header_too_small,
        "mapped header buffer shorter than section table is rejected before reading entries");

    const auto zero_sections = build_synthetic_pe64({});
    check(parse_pe64_sections(zero_sections, parsed_sections, summary) ==
              PeParseStatus::invalid_section_count,
        "zero NumberOfSections is rejected");

    const auto cramped_table = build_synthetic_pe64(valid_specs, {.size_of_headers = 0x180U});
    check(parse_pe64_sections(cramped_table, parsed_sections, summary) ==
              PeParseStatus::invalid_image_geometry,
        "SizeOfHeaders smaller than section headers start is rejected");

    const auto partial_table = build_synthetic_pe64(valid_specs, {.size_of_headers = 0x1B0U});
    check(parse_pe64_sections(partial_table, parsed_sections, summary) ==
              PeParseStatus::invalid_section_count,
        "section table overflowing SizeOfHeaders is rejected");

    // Zero-sized placeholder sections are skipped; VirtualSize governs live range when nonzero.
    constexpr std::array empty_and_padded_specs{
        SyntheticSectionSpec{.virtual_size = 0U,
            .virtual_address = 0U,
            .raw_size = 0U,
            .characteristics = image_section_read},
        SyntheticSectionSpec{.virtual_size = 0x800U,
            .virtual_address = 0x1000U,
            .raw_size = 0xC00U,
            .characteristics = image_section_read | image_section_execute},
        SyntheticSectionSpec{.virtual_size = 0U,
            .virtual_address = 0x2000U,
            .raw_size = 0x400U,
            .characteristics = image_section_read},
    };
    check(parse_pe64_sections(build_synthetic_pe64(empty_and_padded_specs),
              parsed_sections,
              summary) == PeParseStatus::ok &&
              summary.section_count == 2U,
        "zero-sized PE sections are skipped without discarding valid sections");
    const std::span<const ImageSectionRange> padded_span{parsed_sections.data(),
        summary.section_count};
    check(section_contains_rva(padded_span, 0x5000U, 0x17FFU, 1U, OptionalAddressKind::code) &&
              !section_contains_rva(padded_span, 0x5000U, 0x1800U, 1U, OptionalAddressKind::code) &&
              section_contains_rva(padded_span, 0x5000U, 0x2200U, 4U, OptionalAddressKind::data),
        "raw-file alignment padding past VirtualSize is excluded while zero VirtualSize falls "
        "back to SizeOfRawData");

    constexpr std::array header_overlap_spec{
        SyntheticSectionSpec{.virtual_size = 0x200U,
            .virtual_address = 0x200U,
            .raw_size = 0x200U,
            .characteristics = image_section_read},
    };
    check(parse_pe64_sections(
              build_synthetic_pe64(header_overlap_spec, {.section_alignment = 0x200U}),
              parsed_sections,
              summary) == PeParseStatus::invalid_section_entry,
        "section overlapping PE headers is rejected");

    constexpr std::array misaligned_spec{
        SyntheticSectionSpec{.virtual_size = 0x200U,
            .virtual_address = 0x1200U,
            .raw_size = 0x200U,
            .characteristics = image_section_read | image_section_execute},
    };
    check(parse_pe64_sections(build_synthetic_pe64(misaligned_spec), parsed_sections, summary) ==
              PeParseStatus::invalid_section_entry,
        "section misaligned to SectionAlignment is rejected");

    constexpr std::array oob_section_spec{
        SyntheticSectionSpec{.virtual_size = 0x2000U,
            .virtual_address = 0x4000U,
            .raw_size = 0x2000U,
            .characteristics = image_section_read},
    };
    check(parse_pe64_sections(build_synthetic_pe64(oob_section_spec, {.size_of_image = 0x5000U}),
              parsed_sections,
              summary) == PeParseStatus::invalid_section_entry,
        "section extending beyond SizeOfImage is rejected");

    constexpr std::array overlapping_specs{
        SyntheticSectionSpec{.virtual_size = 0x1800U,
            .virtual_address = 0x1000U,
            .raw_size = 0x1800U,
            .characteristics = image_section_read | image_section_execute},
        SyntheticSectionSpec{.virtual_size = 0x800U,
            .virtual_address = 0x2000U,
            .raw_size = 0x800U,
            .characteristics = image_section_read},
    };
    check(parse_pe64_sections(build_synthetic_pe64(overlapping_specs), parsed_sections, summary) ==
              PeParseStatus::invalid_section_entry,
        "overlapping virtual section ranges are rejected");
}

TEST_CASE("fallout4.native_led.radiation_factory_signature_validation",
    "[unit][fallout4][native_led]") {
    using namespace fallout4_adapter::native;
    constexpr RadiationFactoryBinding binding{.factory_rva = 0x1055230U,
        .source_slot_rva = 0x3279570U};

    // Fallout4.exe 1.11.240.0 ID 2248914 machine code at RVA [0x1055230, 0x10552F7).
    constexpr std::array<std::uint8_t, radiation_factory_expected_size> raw_1_11_240_0_factory{
        0x48,
        0x83,
        0xEC,
        0x28,
        0x48,
        0x83,
        0x3D,
        0x34,
        0x43,
        0x22,
        0x02,
        0x00,
        0x0F,
        0x85,
        0xB0,
        0x00,
        0x00,
        0x00,
        0x48,
        0x8D,
        0x05,
        0x67,
        0x38,
        0x41,
        0x01,
        0x48,
        0x89,
        0x5C,
        0x24,
        0x30,
        0x48,
        0x8B,
        0x19,
        0x48,
        0x8D,
        0x0D,
        0x40,
        0x43,
        0x22,
        0x02,
        0x48,
        0x89,
        0x05,
        0x21,
        0x43,
        0x22,
        0x02,
        0x33,
        0xC0,
        0x89,
        0x05,
        0x29,
        0x43,
        0x22,
        0x02,
        0x48,
        0x89,
        0x7C,
        0x24,
        0x20,
        0x48,
        0x8D,
        0x3D,
        0x0D,
        0x43,
        0x22,
        0x02,
        0x48,
        0x89,
        0x3D,
        0xF6,
        0x42,
        0x22,
        0x02,
        0x89,
        0x05,
        0x14,
        0x43,
        0x22,
        0x02,
        0xE8,
        0x4B,
        0x49,
        0x60,
        0x00,
        0x48,
        0x8D,
        0x0D,
        0x1C,
        0x43,
        0x22,
        0x02,
        0xE8,
        0xFF,
        0x45,
        0x60,
        0x00,
        0x48,
        0x8D,
        0x0D,
        0x18,
        0x43,
        0x22,
        0x02,
        0xE8,
        0x33,
        0x49,
        0x60,
        0x00,
        0x48,
        0x8D,
        0x0D,
        0x1C,
        0x43,
        0x22,
        0x02,
        0xE8,
        0xE7,
        0x45,
        0x60,
        0x00,
        0x48,
        0x8D,
        0x0D,
        0x18,
        0x43,
        0x22,
        0x02,
        0xE8,
        0x1B,
        0x49,
        0x60,
        0x00,
        0x48,
        0x8D,
        0x0D,
        0x1C,
        0x43,
        0x22,
        0x02,
        0xE8,
        0xCF,
        0x45,
        0x60,
        0x00,
        0x48,
        0x8D,
        0x05,
        0xB8,
        0xDE,
        0x4D,
        0x01,
        0xC6,
        0x05,
        0x11,
        0x43,
        0x22,
        0x02,
        0x00,
        0x48,
        0x8B,
        0xD7,
        0x48,
        0x89,
        0x05,
        0xA7,
        0x42,
        0x22,
        0x02,
        0x48,
        0x8B,
        0xCB,
        0xE8,
        0x5F,
        0xE6,
        0x1D,
        0xFF,
        0x48,
        0x8B,
        0x5C,
        0x24,
        0x30,
        0x48,
        0x89,
        0x3D,
        0x83,
        0x42,
        0x22,
        0x02,
        0x48,
        0x8B,
        0x7C,
        0x24,
        0x20,
        0x48,
        0x83,
        0xC4,
        0x28,
        0xC3,
    };
    const auto real_bytes = std::as_bytes(std::span{raw_1_11_240_0_factory});
    check(valid_radiation_source_factory(real_bytes, binding),
        "verbatim Fallout4.exe 1.11.240.0 radiation factory machine code passes validation");
    check(!valid_radiation_source_factory(real_bytes,
              RadiationFactoryBinding{.factory_rva = binding.factory_rva,
                  .source_slot_rva = binding.source_slot_rva + 8U}),
        "verbatim Fallout4.exe 1.11.240.0 radiation factory rejects a mismatched slot binding");

    const auto valid_code = build_radiation_factory_bytes(binding);
    check(valid_radiation_source_factory(valid_code, binding),
        "verified 1.11.240.0 radiation factory prologue, slot displacements, and epilogue pass");

    check(!valid_radiation_source_factory(
              std::span<const std::byte>{valid_code.data(), radiation_factory_expected_size - 1U},
              binding),
        "truncated factory code span is rejected");

    auto bad_prologue = valid_code;
    bad_prologue[0] = std::byte{0xCC};
    check(!valid_radiation_source_factory(bad_prologue, binding),
        "corrupted factory prologue fails validation");

    auto wrong_cmp_slot = valid_code;
    write_disp32(wrong_cmp_slot,
        {.disp_offset = 0x07U,
            .factory_rva = binding.factory_rva,
            .next_ip_offset = 0x0CU,
            .target_rva = binding.source_slot_rva + 8U});
    check(!valid_radiation_source_factory(wrong_cmp_slot, binding),
        "factory comparing a different slot global fails validation");

    auto wrong_publish_slot = valid_code;
    write_disp32(wrong_publish_slot,
        {.disp_offset = 0x46U,
            .factory_rva = binding.factory_rva,
            .next_ip_offset = 0x4AU,
            .target_rva = binding.source_slot_rva + 8U});
    check(!valid_radiation_source_factory(wrong_publish_slot, binding),
        "factory publishing to a different slot global fails validation");

    auto wrong_final_store = valid_code;
    write_disp32(wrong_final_store,
        {.disp_offset = 0xB9U,
            .factory_rva = binding.factory_rva,
            .next_ip_offset = 0xBDU,
            .target_rva = binding.source_slot_rva + 8U});
    check(!valid_radiation_source_factory(wrong_final_store, binding),
        "factory trailing store mismatch fails validation");

    auto bad_epilogue = valid_code;
    bad_epilogue.back() = std::byte{0xCC};
    check(!valid_radiation_source_factory(bad_epilogue, binding),
        "altered factory return instruction fails validation");
}
