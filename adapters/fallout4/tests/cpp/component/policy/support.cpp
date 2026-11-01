#include "support.hpp"

#include "effect_recipes.hpp"
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace fallout4_adapter::policy_tests {

MonoTime at(std::int64_t milliseconds) noexcept {
    return MonoTime{} + std::chrono::milliseconds{milliseconds};
}

Observation gameplay_at(std::int64_t milliseconds) noexcept {
    Observation observation{};
    observation.sampled_at = at(milliseconds);
    observation.mode = Mode::gameplay;
    observation.weapon = {
        .identity = 42,
        .family = WeaponFamily::rifle,
        .automatic = true,
        .drawn = true,
        .ready = true,
    };
    observation.attack_trigger = TriggerSide::right;
    observation.aim_trigger = TriggerSide::left;
    observation.health_ratio = 0.8F;
    observation.action_points_ratio = 0.8F;
    return observation;
}

PolicyEvent event(EventKind kind, MonoTime arrived_at, std::uint64_t sequence) noexcept {
    PolicyEvent result{};
    result.kind = kind;
    result.arrived_at = arrived_at;
    result.sequence = sequence;
    result.weapon_identity = 42;
    result.magnitude = 0.75F;
    return result;
}

std::unique_ptr<OutputFrame> update_on_heap(Policy& policy,
    const Observation& observation,
    std::span<const PolicyEvent> events,
    MonoTime now) {
    // Direct initialization avoids the stack temporary introduced by make_unique.
    // NOLINTNEXTLINE(modernize-make-unique)
    return std::unique_ptr<OutputFrame>{new OutputFrame(policy.update(observation, events, now))};
}

std::size_t voice_count(const OutputFrame& frame) noexcept {
    return static_cast<std::size_t>(std::count_if(frame.body.voices.begin(),
        frame.body.voices.end(),
        [](const auto& voice) { return voice.has_value(); }));
}

std::size_t cue_voice_count(const OutputFrame& frame, detail::Cue cue) noexcept {
    const auto source_id = static_cast<std::uint32_t>(cue) + 1U;
    return static_cast<std::size_t>(std::count_if(frame.body.voices.begin(),
        frame.body.voices.end(),
        [source_id](const auto& voice) { return voice && voice->source_id == source_id; }));
}

std::uint64_t cue_sequence(const OutputFrame& frame, detail::Cue cue) noexcept {
    const auto source_id = static_cast<std::uint32_t>(cue) + 1U;
    for (const auto& voice : frame.body.voices) {
        if (voice && voice->source_id == source_id) {
            return voice->sequence;
        }
    }
    return 0;
}

const inputnext::adapters::BodyVoice* find_voice(const OutputFrame& frame,
    detail::Cue cue) noexcept {
    const auto source_id = static_cast<std::uint32_t>(cue) + 1U;
    for (const auto& voice : frame.body.voices) {
        if (voice && voice->source_id == source_id) {
            return &*voice;
        }
    }
    return nullptr;
}

int quantized_field(float value, int maximum) noexcept {
    const auto rounded = std::lround(static_cast<double>(value) * static_cast<double>(maximum));
    return std::clamp(static_cast<int>(rounded), 0, maximum);
}

} // namespace fallout4_adapter::policy_tests
