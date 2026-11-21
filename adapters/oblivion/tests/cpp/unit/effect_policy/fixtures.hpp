#pragma once

#include "effect_model.hpp"
#include "effect_policy.hpp"
#include "effect_settings.hpp"
#include "inputnext_adapters/body_program.hpp"
#include "inputnext_adapters/output_frame.hpp"
#include "model.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace oblivion_adapter::test::effect_policy {

using inputnext::adapters::BodyVoice;
using inputnext::adapters::MonoTime;
using inputnext::adapters::OutputFrame;

inline constexpr std::uint64_t fixture_epoch = 9;

inline std::unique_ptr<EffectPolicy> make_policy(const EffectSettings& settings = {}) {
    return std::make_unique<EffectPolicy>(settings);
}

inline MonoTime at(int milliseconds) noexcept {
    return MonoTime{} + std::chrono::milliseconds{milliseconds};
}

inline EffectObservation observation(int milliseconds, std::uint64_t frame = 1) noexcept {
    return {
        .sampled_at = at(milliseconds),
        .epoch = fixture_epoch,
        .frame = frame,
        .mode = EffectMode::gameplay,
        .alive = true,
        .in_combat = true,
        .health = calculate_health(100.0F, 100.0F),
        .player_id = 0x14,
        .cell_id = 0x1234,
        .mounted = false,
        .ground = Ground::grounded,
    };
}

inline EffectEvent event(EffectEventKind kind,
    int milliseconds,
    std::uint64_t frame,
    std::uint64_t sequence = 1) noexcept {
    return {
        .kind = kind,
        .at = at(milliseconds),
        .epoch = fixture_epoch,
        .frame = frame,
        .sequence = sequence,
        .mode = EffectMode::gameplay,
    };
}

inline std::size_t voice_count(const OutputFrame& frame) noexcept {
    return static_cast<std::size_t>(std::count_if(frame.body.voices.begin(),
        frame.body.voices.end(),
        [](const auto& voice) { return voice.has_value(); }));
}

template <typename T>
const T& require_value(const std::optional<T>& value, const char* message) {
    if (!value.has_value()) {
        throw std::runtime_error{message};
    }
    return *value;
}

inline const BodyVoice& first_voice(const OutputFrame& frame) {
    const auto found = std::ranges::find_if(frame.body.voices,
        [](const auto& voice) { return voice.has_value(); });
    if (found == frame.body.voices.end()) {
        throw std::runtime_error{"expected an output body voice"};
    }
    return require_value(*found, "selected output body voice is present");
}

constexpr std::string_view profile_name(EffectId effect) noexcept {
    return effect_sections[profile_index(effect)];
}

// Distinct frequencies identify the selected profile even when several are configured.
constexpr float profile_frequency(EffectId effect) noexcept {
    return 400.0F + static_cast<float>(profile_index(effect));
}

inline ProfileAdjustment& mark_profile(EffectSettings& settings, EffectId effect) noexcept {
    auto& profile = settings.profiles[profile_index(effect)];
    profile.frequency_hz = profile_frequency(effect);
    return profile;
}

inline void check_profile(const OutputFrame& frame, EffectId effect) {
    REQUIRE(voice_count(frame) == 1);
    CHECK(first_voice(frame).wave.frequency == profile_frequency(effect));
}

} // namespace oblivion_adapter::test::effect_policy
