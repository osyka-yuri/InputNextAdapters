#pragma once

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace fallout_gamebryo::tests {
inline constexpr std::uint32_t player_id = 0x14U;

inline MonoTime at(std::int64_t milliseconds) noexcept {
    return MonoTime{} + std::chrono::milliseconds{milliseconds};
}

inline Observation gameplay(std::int64_t time = 1000) noexcept {
    return Observation{
        .at = at(time),
        .epoch = 1U,
        .frame = 1U,
        .player = player_id,
        .mode = Mode::gameplay,
        .weapon = {.id = 42U,
            .kind = WeaponClass::pistol,
            .loaded_ammo = 5U,
            .drawn = true,
            .ready = true},
        .attack_trigger = TriggerSide::right,
        .aim_trigger = TriggerSide::left,
        .alive = true,
        .focused = true,
        .in_combat = true,
        .health = inputnext::adapters::LedHealthSample{.current = 80.0F, .maximum = 100.0F},
        .stealth = std::nullopt,
        .action_points_ratio = 1.0F,
        .radiation = 0.0F,
        .radiation_sources = 0U,
    };
}

inline Event
shot(const Observation& observation, std::uint64_t action, std::int64_t time) noexcept {
    return Event{
        .at = at(time),
        .epoch = observation.epoch,
        .frame = observation.frame,
        .action = action,
        .mode = Mode::gameplay,
        .kind = EventKind::shot,
        .source = observation.player,
        .weapon = observation.weapon.id,
        .weapon_class = observation.weapon.kind,
    };
}

inline std::size_t voice_count(const inputnext::adapters::OutputFrame& frame) noexcept {
    std::size_t count{};
    for (const auto& voice : frame.body.voices) {
        count += voice.has_value() ? 1U : 0U;
    }
    return count;
}

inline inputnext::adapters::BodyVoice voice_for(const inputnext::adapters::OutputFrame& frame,
    BodyEffect effect) {
    const auto source = static_cast<std::uint32_t>(effect) + 1U;
    for (const auto& voice : frame.body.voices) {
        if (voice && voice->source_id == source) {
            return *voice;
        }
    }
    throw std::logic_error("expected effect voice was not present");
}

inline bool has_voice(const inputnext::adapters::OutputFrame& frame, BodyEffect effect) noexcept {
    const auto source = static_cast<std::uint32_t>(effect) + 1U;
    return std::ranges::any_of(frame.body.voices,
        [source](const auto& voice) { return voice && voice->source_id == source; });
}

inline std::size_t effect_voice_count(const inputnext::adapters::OutputFrame& frame,
    BodyEffect effect) noexcept {
    const auto source = static_cast<std::uint32_t>(effect) + 1U;
    return static_cast<std::size_t>(std::ranges::count_if(frame.body.voices,
        [source](const auto& voice) { return voice && voice->source_id == source; }));
}

inline std::array<float, 2> pcm_peaks(const inputnext::adapters::BodyVoice& voice,
    std::chrono::milliseconds age = {}) {
    const inputnext::adapters::BodyProgram program{.voices = {voice}};
    std::vector<float> samples(inputnext::adapters::body_pcm_max_frames * 2U);
    const auto window =
        inputnext::adapters::build_body_pcm(program, voice.started_at + age, samples);
    std::array<float, 2> peaks{};
    for (std::size_t frame{}; frame < window.frame_count; ++frame) {
        peaks[0] = std::max(peaks[0], std::abs(samples[frame * 2U]));
        peaks[1] = std::max(peaks[1], std::abs(samples[(frame * 2U) + 1U]));
    }
    return peaks;
}
} // namespace fallout_gamebryo::tests
