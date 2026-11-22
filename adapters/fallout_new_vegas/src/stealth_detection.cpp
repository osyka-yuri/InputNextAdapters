#include "stealth_detection.hpp"

#include <fallout_gamebryo/model.hpp>

#include <chrono>
#include <cstdint>
#include <optional>

namespace fallout_new_vegas {
bool StealthDetection::bind(Reader read) noexcept {
    read_ = read;
    last_read_.reset();
    return read_ != nullptr;
}

std::optional<fallout_gamebryo::LedStealthSample>
StealthDetection::sample(const fallout_gamebryo::Observation& observation, bool sneaking) noexcept {
    using fallout_gamebryo::Mode;
    if (!read_ || !sneaking || !observation.focused || !observation.alive ||
        observation.player == 0U || observation.mode != Mode::gameplay) {
        last_read_.reset();
        return std::nullopt;
    }

    if (last_read_ && last_read_->epoch == observation.epoch &&
        last_read_->player == observation.player && observation.at >= last_read_->at &&
        observation.at - last_read_->at < std::chrono::milliseconds{50}) {
        return last_read_->value;
    }

    const auto value = read_().transform([](DetectionState state) {
        return fallout_gamebryo::LedStealthSample{.sneaking = true,
            .detection_score = static_cast<std::int32_t>(state)};
    });
    last_read_ = DetectionSample{.at = observation.at,
        .epoch = observation.epoch,
        .player = observation.player,
        .value = value};
    return value;
}
} // namespace fallout_new_vegas
