#include "native_observation/native_observation_test_cases.hpp"
#include "native_observation/native_observation_test_support.hpp"

#include "native/observation.hpp"
#include <skyrim_adapter/detail/led_observation.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace skyrim_adapter::native_observation_tests {

void led_sources_preserve_unknown_health_and_optional_detection() {
    const auto health = skyrim_adapter::detail::normalized_led_health(5.0F, 100.0F);
    require(health && health->current == 5.0F && health->maximum == 100.0F,
        "absolute health/max values must be copied into the LED source sample");
    require(!skyrim_adapter::detail::normalized_led_health(5.0F, 0.0F),
        "an unavailable or invalid gameplay maximum must remain unknown");

    require(!skyrim_adapter::detail::normalized_led_stealth(true, std::nullopt),
        "missing raw detection must not be replaced with a fabricated hidden score");
    const auto detected =
        skyrim_adapter::detail::normalized_led_stealth(true, std::optional<std::int32_t>{-37});
    require(detected && detected->sneaking && detected->detection_score == -37,
        "verified signed detection scores must remain raw values");
}

void optional_detection_relocations_are_tested_without_loading_address_library() {
    struct Mapping {
        std::uint64_t id{};
        std::uint64_t offset{};
    };

    const std::array mappings{
        Mapping{.id = 514167, .offset = 0x1000},
        Mapping{.id = 40394, .offset = 0x2000},
        Mapping{.id = 41408, .offset = 0},
    };
    using skyrim_adapter::native::detail::relocation_id_present;
    require(relocation_id_present(mappings, 514167),
        "an optional singleton mapping with a real offset must be available");
    require(relocation_id_present(mappings, 40394),
        "an optional detection function mapping with a real offset must be available");
    require(!relocation_id_present(mappings, 41408),
        "a zero optional offset must remain unavailable");
    require(!relocation_id_present(mappings, 999999),
        "a missing optional relocation must not be reported as available");
    require(!relocation_id_present(mappings, 0), "a runtime with no selected ID must fail closed");
}
} // namespace skyrim_adapter::native_observation_tests
