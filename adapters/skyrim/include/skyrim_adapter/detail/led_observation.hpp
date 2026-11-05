#pragma once

#include <inputnext_adapters/led_policy.hpp>

#include <cmath>
#include <cstdint>
#include <optional>

namespace skyrim_adapter::detail {
[[nodiscard]] inline std::optional<inputnext::adapters::LedHealthSample>
normalized_led_health(float current, float maximum) noexcept {
    if (!std::isfinite(current) || current < 0.0F || !std::isfinite(maximum) || maximum <= 0.0F) {
        return std::nullopt;
    }
    return inputnext::adapters::LedHealthSample{.current = current, .maximum = maximum};
}

[[nodiscard]] inline std::optional<inputnext::adapters::LedStealthSample>
normalized_led_stealth(bool sneaking, std::optional<std::int32_t> raw_detection_score) noexcept {
    if (!raw_detection_score) {
        return std::nullopt;
    }
    return inputnext::adapters::LedStealthSample{
        .sneaking = sneaking,
        .detection_score = *raw_detection_score,
    };
}
} // namespace skyrim_adapter::detail
