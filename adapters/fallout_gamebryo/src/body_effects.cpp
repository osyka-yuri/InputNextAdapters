#include <fallout_gamebryo/detail/body_effects.hpp>
#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/settings.hpp>

#include "body_recipes.hpp"

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace fallout_gamebryo::detail {
namespace {
using inputnext::adapters::BodyVoice;

bool active_at(MonoTime started_at, std::uint32_t duration_ms, MonoTime now) noexcept {
    return started_at <= now && now - started_at < std::chrono::milliseconds{duration_ms};
}
} // namespace

void BodyEffects::clear() noexcept {
    active_ = {};
}

void BodyEffects::reset_voice_ids() noexcept {
    next_voice_id_ = 1;
    next_sequence_ = 1;
}

void BodyEffects::add(const Settings& settings,
    BodyEffect effect,
    MonoTime started_at,
    MonoTime now,
    float strength,
    std::uint64_t action) noexcept {
    const auto profile_index = static_cast<std::size_t>(effect);
    if (!settings.enabled || !settings.body_enabled || !std::isfinite(strength) ||
        strength <= 0.0F || strength > 1.5F || profile_index >= settings.body.size()) {
        return;
    }
    const auto& adjustment = settings.body[profile_index];
    if (!adjustment.enabled) {
        return;
    }
    auto recipe = recipe_for(effect);
    const auto duration = static_cast<std::uint32_t>(
        std::clamp(static_cast<float>(recipe.duration_ms) * adjustment.duration_scale,
            1.0F,
            200.0F));
    if (!active_at(started_at, duration, now)) {
        return;
    }
    if (adjustment.audio_haptic) {
        static_cast<void>(
            inputnext::adapters::apply_audio_haptic_profile(recipe.wave, *adjustment.audio_haptic));
    }
    const BodyVoice voice{
        .wave = recipe.wave,
        .started_at = started_at,
        .duration_ms = duration,
        .stable_id = action != 0U ? action : next_voice_id_++,
        .sequence = next_sequence_++,
        .source_id = static_cast<std::uint32_t>(profile_index + 1U),
        .priority = recipe.priority,
        .scale = std::clamp(strength * settings.body_scale * adjustment.strength_scale, 0.0F, 1.5F),
        .hd_scale = 1.0F,
        .duration_scale = 1.0F,
        .position_gain = 0.65F,
        .channel_mask = 0x3U,
    };

    BodyVoice* weakest = nullptr;
    for (auto& slot : active_.voices) {
        if (!slot || !active_at(slot->started_at, slot->duration_ms, now)) {
            slot = voice;
            return;
        }
        if (!weakest || slot->priority < weakest->priority) {
            weakest = &*slot;
        }
    }
    if (weakest && weakest->priority <= voice.priority) {
        *weakest = voice;
    }
}

void BodyEffects::write(inputnext::adapters::OutputFrame& frame, MonoTime now) noexcept {
    const BodyVoice* strongest = nullptr;
    for (auto& slot : active_.voices) {
        if (!slot) {
            continue;
        }
        if (!active_at(slot->started_at, slot->duration_ms, now)) {
            slot.reset();
            continue;
        }
        if (!strongest || strongest->priority < slot->priority) {
            strongest = &*slot;
        }
    }
    frame.body = active_;
    if (!strongest || strongest->source_id == 0U || strongest->source_id > body_effect_count) {
        return;
    }
    const auto recipe = recipe_for(static_cast<BodyEffect>(strongest->source_id - 1U));
    const auto age =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - strongest->started_at);
    frame.rumble = inputnext::adapters::RumbleIntent{
        .payload = {.low_frequency = std::clamp(recipe.fallback_low * strongest->scale, 0.0F, 1.0F),
            .high_frequency = std::clamp(recipe.fallback_high * strongest->scale, 0.0F, 1.0F)},
        .ttl_ms = std::clamp(strongest->duration_ms - static_cast<std::uint32_t>(age.count()),
            1U,
            effect_ttl_ms)};
}
} // namespace fallout_gamebryo::detail
