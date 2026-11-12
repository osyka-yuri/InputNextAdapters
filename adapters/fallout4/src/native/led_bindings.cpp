#include "led_bindings.hpp"

#include "bindings.hpp"
#include "optional_id_map.hpp"

#include <RE/A/Actor.h>
#include <RE/B/BSTEvent.h>
#include <RE/C/CurrentRadiationSourceCount.h>
#include <RE/IDs.h>
#include <RE/P/ProcessLists.h>
#include <REL/Offset2ID.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <span>

namespace fallout4_adapter::native {
namespace {
struct ImageSections {
    std::array<ImageSectionRange, 96> values{};
    std::size_t size{};
    std::uint32_t image_size{};
};

std::optional<ImageSections> image_sections() noexcept {
    const auto base = checked_image_base();
    if (base == 0) {
        return std::nullopt;
    }
    // The checked host gate validates DOS/NT signatures, e_lfanew within the first page,
    // architecture, non-zero SizeOfImage, required address bounds, and instruction signatures before these header reads.
    const auto* image = reinterpret_cast<const std::byte*>(base);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    const auto image_size = headers->OptionalHeader.SizeOfImage;
    const auto section_count = static_cast<std::size_t>(headers->FileHeader.NumberOfSections);
    const auto first_section_rva = static_cast<std::size_t>(dos->e_lfanew) + sizeof(DWORD) +
                                   sizeof(IMAGE_FILE_HEADER) +
                                   headers->FileHeader.SizeOfOptionalHeader;
    if (section_count == 0 || section_count > ImageSections{}.values.size() ||
        first_section_rva > image_size ||
        section_count > (image_size - first_section_rva) / sizeof(IMAGE_SECTION_HEADER)) {
        return std::nullopt;
    }

    ImageSections sections{.image_size = image_size};
    const auto* first = IMAGE_FIRST_SECTION(headers);
    for (std::size_t index = 0; index < section_count; ++index) {
        const auto& section = first[index];
        DWORD virtual_size{};
        const auto* section_bytes = reinterpret_cast<const std::byte*>(&section);
        std::memcpy(&virtual_size,
            section_bytes + offsetof(IMAGE_SECTION_HEADER, Misc),
            sizeof(virtual_size));
        const auto range_size = std::max(virtual_size, section.SizeOfRawData);
        if (range_size == 0 || section.VirtualAddress >= image_size ||
            range_size > image_size - section.VirtualAddress) {
            continue;
        }
        sections.values[sections.size++] = ImageSectionRange{
            .rva = section.VirtualAddress,
            .size = range_size,
            .characteristics = section.Characteristics,
        };
    }
    return sections;
}

struct LedBindings {
    std::array<std::uintptr_t, 5> addresses{};
    std::array<bool, 5> available{};
};

const LedBindings& led_bindings() noexcept {
    static const LedBindings result = [] noexcept {
        LedBindings output{};
        const auto base = checked_image_base();
        const auto sections = image_sections();
        if (base == 0 || !sections) {
            return output;
        }

        static constexpr std::array requests{
            OptionalAddressRequest{.id = RE::ID::Actor::IsSneaking.id(),
                .width = 1U,
                .kind = OptionalAddressKind::code},
            OptionalAddressRequest{.id = RE::ID::ProcessLists::Singleton.id(),
                .width = sizeof(void*),
                .kind = OptionalAddressKind::data},
            OptionalAddressRequest{
                .id = RE::ID::ProcessLists::RequestHighestDetectionLevelAgainstActor.id(),
                .width = 1U,
                .kind = OptionalAddressKind::code},
            OptionalAddressRequest{.id = RE::ID::CurrentRadiationSourceCount::GetEventSource.id(),
                .width = sizeof(void*),
                .kind = OptionalAddressKind::writable_data},
            OptionalAddressRequest{.id = RE::ID::BSTEvent::BSTGlobalEvent::Singleton.id(),
                .width = sizeof(void*),
                .kind = OptionalAddressKind::data},
        };

        try {
            // These are public inverse-table APIs. The local table is freed on return.
            REL::Offset2ID inverse;
            inverse.load_v5();
            if (inverse.size() == 0) {
                inverse.load_v2();
            }
            if (inverse.size() == 0) {
                return output;
            }
            const auto resolved = resolve_optional_addresses(inverse,
                std::span{sections->values.data(), sections->size},
                sections->image_size,
                requests);
            for (std::size_t index = 0; index < output.addresses.size(); ++index) {
                output.available[index] = resolved.available[index];
                if (resolved.available[index]) {
                    output.addresses[index] = base + resolved.rvas[index];
                }
            }
        } catch (...) {
            return LedBindings{};
        }
        return output;
    }();
    return result;
}

} // namespace

std::optional<StealthFacts> read_stealth_facts(RE::Actor* actor) noexcept {
    if (!actor || !bindings_available()) {
        return std::nullopt;
    }
    const auto& resolved = led_bindings();
    if (!resolved.available[0] || !resolved.available[1] || !resolved.available[2]) {
        return std::nullopt;
    }

    auto* process_lists = *reinterpret_cast<RE::ProcessLists**>(resolved.addresses[1]);
    if (!process_lists) {
        return std::nullopt;
    }
    using IsSneaking = bool (*)(RE::Actor*);
    using HighestDetection = std::int16_t (*)(RE::ProcessLists*, RE::Actor*, std::uint32_t&);
    const auto is_sneaking = reinterpret_cast<IsSneaking>(resolved.addresses[0]);
    const auto highest_detection = reinterpret_cast<HighestDetection>(resolved.addresses[2]);
    if (!is_sneaking(actor)) {
        return std::nullopt;
    }
    std::uint32_t los_count{};
    const auto score = highest_detection(process_lists, actor, los_count);
    return StealthFacts{.sneaking = true, .detection_score = score};
}

bool register_radiation_count_sink(
    RE::BSTEventSink<RE::CurrentRadiationSourceCount>* sink) noexcept {
    if (!sink || !bindings_available()) {
        return false;
    }
    const auto& resolved = led_bindings();
    if (!resolved.available[3] || !resolved.available[4]) {
        return false;
    }

    using RadiationEventSource = RE::BSTGlobalEvent::EventSource<RE::CurrentRadiationSourceCount>;
    try {
        auto** source_slot = reinterpret_cast<RadiationEventSource**>(resolved.addresses[3]);
        auto* source = *source_slot;
        if (!source) {
            auto* global = *reinterpret_cast<RE::BSTGlobalEvent**>(resolved.addresses[4]);
            if (!global) {
                return false;
            }
            source = new (std::nothrow) RadiationEventSource{&global->eventSourceSDMKiller};
            if (!source) {
                return false;
            }
            *source_slot = source;
        }
        source->RegisterSink(sink);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace fallout4_adapter::native
