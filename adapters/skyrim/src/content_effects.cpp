#include "skyrim_adapter/content_effects.hpp"

#include "skyrim_adapter/content_observation.hpp"
#include "skyrim_adapter/model.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace skyrim_adapter {
namespace {
constexpr auto maximum_sample_age = std::chrono::milliseconds{150};
constexpr unsigned warning_stage = 3;

constexpr std::array<std::uint32_t, 3> need_durations_ms{260, 320, 380};
constexpr std::array<int, 3> need_priorities{7, 8, 9};
constexpr std::array<float, 3> need_low_amplitudes{0.15F, 0.18F, 0.22F};
constexpr std::array<float, 3> need_high_amplitudes{0.20F, 0.26F, 0.31F};

ContentPulse need_pulse(ContentCueKind kind, unsigned stage, MonoTime sampled_at) noexcept {
    const auto severity = std::min(stage, 5U) - warning_stage;
    ContentPulse pulse{};
    pulse.kind = kind;
    pulse.stage = stage;
    pulse.started_at = sampled_at;
    pulse.duration_ms = need_durations_ms[severity];
    pulse.priority = need_priorities[severity];
    pulse.low = need_low_amplitudes[severity];
    pulse.high = need_high_amplitudes[severity];
    return pulse;
}

ContentPulse transformation_pulse(BeastForm form, MonoTime sampled_at) noexcept {
    ContentPulse pulse{};
    pulse.kind = ContentCueKind::transformation;
    pulse.beast = form;
    pulse.started_at = sampled_at;
    pulse.duration_ms = 300;
    pulse.priority = 9;
    pulse.low = 0.16F;
    pulse.high = 0.24F;
    return pulse;
}
} // namespace

void ContentEffects::reset() noexcept {
    needs_ = {};
    previous_beast_.reset();
    previous_sampled_at_ = {};
    player_ = 0;
    has_sample_ = false;
}

std::array<std::optional<ContentPulse>, 4> ContentEffects::step(
    const ContentObservation& observation,
    FormId player,
    MonoTime sampled_at,
    MonoTime now) noexcept {
    std::array<std::optional<ContentPulse>, 4> pulses{};
    if (player == 0 || sampled_at > now || now - sampled_at > maximum_sample_age) {
        reset();
        return pulses;
    }

    if (has_sample_ && sampled_at == previous_sampled_at_) {
        return pulses;
    }
    if (has_sample_ && sampled_at < previous_sampled_at_) {
        reset();
        return pulses;
    }

    if (has_sample_ &&
        (player != player_ || sampled_at - previous_sampled_at_ > maximum_sample_age)) {
        reset();
    }

    player_ = player;
    previous_sampled_at_ = sampled_at;
    has_sample_ = true;

    const std::array<std::optional<unsigned>, 3> stages{
        observation.cold_stage,
        observation.hunger_stage,
        observation.exhaustion_stage,
    };
    constexpr std::array<ContentCueKind, 3> kinds{
        ContentCueKind::cold,
        ContentCueKind::hunger,
        ContentCueKind::exhaustion,
    };

    for (std::size_t index = 0; index < needs_.size(); ++index) {
        const auto stage = stages[index];
        if (!observation.survival_enabled || !stage || *stage > 5U) {
            needs_[index].stage.reset();
            continue;
        }

        auto& previous = needs_[index].stage;
        if (previous && *stage > *previous && *stage >= warning_stage) {
            pulses[index] = need_pulse(kinds[index], *stage, sampled_at);
        }
        previous = *stage;
    }

    if (!observation.beast) {
        previous_beast_.reset();
    } else {
        const BeastForm current = *observation.beast;
        if (previous_beast_ && *previous_beast_ != current &&
            (*previous_beast_ != BeastForm::none || current != BeastForm::none)) {
            const BeastForm cue_form = current == BeastForm::none ? *previous_beast_ : current;
            pulses[3] = transformation_pulse(cue_form, sampled_at);
        }
        previous_beast_ = current;
    }

    return pulses;
}
} // namespace skyrim_adapter
