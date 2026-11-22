#pragma once

#include <fallout_gamebryo/model.hpp>

#include <cstdint>
#include <optional>

namespace fallout_new_vegas {
enum class DetectionState : std::uint8_t { hidden, caution, detected, danger };

[[nodiscard]] constexpr DetectionState
classify_detection(bool unseen, bool in_combat, std::int32_t detection_value) noexcept {
    if (unseen) {
        return DetectionState::caution;
    }
    if (in_combat) {
        return DetectionState::danger;
    }
    return detection_value > 0 ? DetectionState::detected : DetectionState::hidden;
}

class StealthDetection final {
public:
    using Reader = std::optional<DetectionState> (*)() noexcept;

    [[nodiscard]] bool bind(Reader read) noexcept;
    [[nodiscard]] std::optional<fallout_gamebryo::LedStealthSample>
    sample(const fallout_gamebryo::Observation& observation, bool sneaking) noexcept;

private:
    struct DetectionSample {
        fallout_gamebryo::MonoTime at;
        std::uint64_t epoch{};
        std::uint32_t player{};
        std::optional<fallout_gamebryo::LedStealthSample> value;
    };

    Reader read_{};
    std::optional<DetectionSample> last_read_;
};

[[nodiscard]] StealthDetection::Reader native_stealth_reader() noexcept;
} // namespace fallout_new_vegas
