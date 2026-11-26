#include "led_bindings.hpp"

#include "bindings.hpp"
#include "optional_id_map.hpp"

#include <RE/A/Actor.h>
#include <RE/B/BSTEvent.h>
#include <RE/C/CurrentRadiationSourceCount.h>
#include <RE/IDs.h>
#include <RE/P/ProcessLists.h>
#include <REL/Offset2ID.h>

// Include after CommonLibF4 headers because diagnostic_logging.hpp pulls <Windows.h>.
#include "diagnostic_logging.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace fallout4_adapter::native {
namespace {
// Fallout4.exe 1.11.240.0, RVA 0x1055230.
// Initializes the engine-owned radiation singleton without synchronization.
// Main-thread only.
inline constexpr std::uint64_t radiation_event_source_factory_id = 2248914U;

static_assert(offsetof(RE::BSTGlobalEvent, eventSourceSDMKiller) == 0x10U);

enum class LedBinding : std::uint8_t {
    actor_is_sneaking,
    process_lists_singleton,
    highest_detection_level,
    radiation_source_slot,
    global_event_singleton,
    radiation_source_factory,
    count,
};

constexpr std::size_t binding_index(LedBinding binding) noexcept {
    return static_cast<std::size_t>(binding);
}

inline constexpr std::size_t led_binding_count = binding_index(LedBinding::count);

struct LedBindings {
    std::array<std::uintptr_t, led_binding_count> addresses{};
    std::array<bool, led_binding_count> available{};

    [[nodiscard]] bool has(LedBinding binding) const noexcept {
        return available[binding_index(binding)];
    }

    [[nodiscard]] std::uintptr_t address(LedBinding binding) const noexcept {
        return addresses[binding_index(binding)];
    }
};

const LedBindings& led_bindings() noexcept {
    static const LedBindings result = [] noexcept {
        LedBindings output{};
        const auto base = checked_image_base();
        const auto image_size = checked_image_size();
        const auto sections = checked_image_sections();
        if (base == 0 || image_size == 0 || image_size > UINT32_MAX || sections.empty()) {
            return output;
        }

        static constexpr std::array<OptionalAddressRequest, led_binding_count> requests{
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
            OptionalAddressRequest{.id = radiation_event_source_factory_id,
                .width = radiation_factory_expected_size,
                .kind = OptionalAddressKind::code},
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
                sections,
                static_cast<std::uint32_t>(image_size),
                requests);
            for (std::size_t index = 0; index < led_binding_count; ++index) {
                output.available[index] = resolved.available[index];
                if (resolved.available[index]) {
                    output.addresses[index] = base + resolved.rvas[index];
                }
            }

            constexpr auto factory_index = binding_index(LedBinding::radiation_source_factory);
            constexpr auto slot_index = binding_index(LedBinding::radiation_source_slot);
            if (output.available[factory_index]) {
                const auto* factory_bytes =
                    reinterpret_cast<const std::byte*>(output.addresses[factory_index]);
                const bool validated =
                    output.available[slot_index] &&
                    valid_radiation_source_factory(
                        std::span<const std::byte>{factory_bytes, radiation_factory_expected_size},
                        {.factory_rva = resolved.rvas[factory_index],
                            .source_slot_rva = resolved.rvas[slot_index]});
                if (!validated) {
                    output.available[factory_index] = false;
                    output.addresses[factory_index] = 0;
                    log_warn("Fallout 4 radiation source factory (ID {}) failed signature "
                             "validation at RVA 0x{:X}; lazy creation disabled.",
                        radiation_event_source_factory_id,
                        resolved.rvas[factory_index]);
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
    if (!resolved.has(LedBinding::actor_is_sneaking) ||
        !resolved.has(LedBinding::process_lists_singleton) ||
        !resolved.has(LedBinding::highest_detection_level)) {
        return std::nullopt;
    }

    auto* process_lists = *reinterpret_cast<RE::ProcessLists**>(
        resolved.address(LedBinding::process_lists_singleton));
    if (!process_lists) {
        return std::nullopt;
    }
    using IsSneaking = bool (*)(RE::Actor*);
    using HighestDetection = std::int16_t (*)(RE::ProcessLists*, RE::Actor*, std::uint32_t&);
    const auto is_sneaking =
        reinterpret_cast<IsSneaking>(resolved.address(LedBinding::actor_is_sneaking));
    const auto highest_detection =
        reinterpret_cast<HighestDetection>(resolved.address(LedBinding::highest_detection_level));
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
    if (!resolved.has(LedBinding::radiation_source_slot)) {
        return false;
    }

    using RadiationEventSource = RE::BSTGlobalEvent::EventSource<RE::CurrentRadiationSourceCount>;
    using RadiationSourceFactory = void (*)(RE::BSTGlobalEvent::KillSDMEventSource* const*);
    try {
        auto* const* source_slot = reinterpret_cast<RadiationEventSource* const*>(
            resolved.address(LedBinding::radiation_source_slot));
        auto* source = *source_slot;
        if (!source) {
            if (!resolved.has(LedBinding::global_event_singleton) ||
                !resolved.has(LedBinding::radiation_source_factory)) {
                return false;
            }
            auto* global = *reinterpret_cast<RE::BSTGlobalEvent* const*>(
                resolved.address(LedBinding::global_event_singleton));
            if (!global) {
                return false;
            }
            // NOLINTNEXTLINE(misc-const-correctness): Native factory mutates the pointee event source.
            auto* const kill_source = std::addressof(global->eventSourceSDMKiller);
            const auto ensure_source = reinterpret_cast<RadiationSourceFactory>(
                resolved.address(LedBinding::radiation_source_factory));
            ensure_source(&kill_source);
            source = *source_slot;
            if (!source) {
                return false;
            }
        }
        source->RegisterSink(sink);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace fallout4_adapter::native
