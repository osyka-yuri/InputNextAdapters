#include "skyrim_adapter/body_waveform.hpp"

#include "skyrim_adapter/content_observation.hpp"
#include "skyrim_adapter/model.hpp"

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/body_program.hpp>

#include "effect_recipes.hpp"
#include "voice_profile.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace skyrim_adapter {
namespace {
constexpr float held_position_gain = 0.50F;
constexpr float hand_transient_position_gain = 0.50F;
constexpr float body_transient_position_gain = 0.75F;

using inputnext::adapters::WaveRecipe;

float hand_transient_gain(const std::optional<BodyCue>& cue) noexcept {
    return cue && cue->kind == BodyCueKind::melee_swing ? body_transient_position_gain
                                                        : hand_transient_position_gain;
}

void keep_body_component(WaveRecipe& recipe) noexcept {
    if (recipe.second_frequency > 0.0F) {
        recipe.frequency = std::min(recipe.frequency, recipe.second_frequency);
    }
    recipe.second_frequency = 0.0F;
    recipe.second_mix = 0.0F;
}

float melee_frequency_scale(const BodyCue& cue) noexcept {
    float scale = 1.0F;
    if ((cue.flags & static_cast<std::uint32_t>(HitFlag::power_attack)) != 0) {
        scale *= 0.92F;
    }
    if ((cue.flags & static_cast<std::uint32_t>(HitFlag::blocked)) != 0) {
        scale *= cue.shield_block ? 0.78F : 0.90F;
    }
    if ((cue.flags & static_cast<std::uint32_t>(HitFlag::bash_attack)) != 0) {
        scale *= 1.20F;
    }
    return scale;
}

WaveRecipe melee_recipe(const BodyCue& cue) noexcept {
    WaveRecipe recipe{};
    recipe.attack_ms = 1.0F;
    recipe.noise = cue.kind == BodyCueKind::melee_hit ? 0.20F : 0.14F;

    // Timbre and weight are independent: blunt impacts are damped bass pulses; blades combine
    // a higher resonant component with a lower body component. These are authored projections,
    // not measured weapon sounds; see docs/haptics-rationale.md.
    switch (cue.weapon) {
    case WeaponClass::unarmed:
        recipe.frequency = 78.0F;
        recipe.level = cue.kind == BodyCueKind::melee_hit ? 0.58F : 0.44F;
        recipe.noise = 0.04F;
        recipe.attack_ms = 2.5F;
        recipe.release_ms = 14.0F;
        recipe.decay_per_second = 18.0F;
        break;
    case WeaponClass::dagger:
        recipe.frequency = 260.0F;
        recipe.second_frequency = 104.0F;
        recipe.second_mix = 0.18F;
        recipe.level = cue.kind == BodyCueKind::melee_hit ? 0.62F : 0.50F;
        recipe.noise = 0.08F;
        recipe.attack_ms = 0.8F;
        recipe.release_ms = 10.0F;
        recipe.decay_per_second = 8.0F;
        break;
    case WeaponClass::sword:
        recipe.frequency = 216.0F;
        recipe.second_frequency = 82.0F;
        recipe.second_mix = 0.30F;
        recipe.level = cue.kind == BodyCueKind::melee_hit ? 0.70F : 0.56F;
        recipe.noise = 0.10F;
        recipe.release_ms = 16.0F;
        recipe.decay_per_second = 6.0F;
        break;
    case WeaponClass::axe:
        recipe.frequency = 176.0F;
        recipe.second_frequency = 58.0F;
        recipe.second_mix = 0.44F;
        recipe.level = cue.kind == BodyCueKind::melee_hit ? 0.76F : 0.60F;
        recipe.noise = cue.kind == BodyCueKind::melee_hit ? 0.12F : 0.08F;
        recipe.attack_ms = 1.2F;
        recipe.release_ms = 22.0F;
        recipe.decay_per_second = 4.5F;
        break;
    case WeaponClass::mace:
        recipe.frequency = 68.0F;
        recipe.level = cue.kind == BodyCueKind::melee_hit ? 0.82F : 0.68F;
        recipe.noise = 0.06F;
        recipe.attack_ms = 1.5F;
        recipe.release_ms = 26.0F;
        recipe.decay_per_second = 10.0F;
        break;
    case WeaponClass::two_handed:
        recipe.frequency = 54.0F;
        recipe.second_frequency = 144.0F;
        recipe.second_mix = 0.18F;
        recipe.level = cue.kind == BodyCueKind::melee_hit ? 0.88F : 0.74F;
        recipe.noise = 0.10F;
        recipe.attack_ms = 1.8F;
        recipe.release_ms = 30.0F;
        recipe.decay_per_second = 4.0F;
        break;
    case WeaponClass::none:
    case WeaponClass::bow:
    case WeaponClass::crossbow:
    case WeaponClass::unknown:
        return {};
    }

    if (cue.kind == BodyCueKind::melee_swing) {
        // A preparatory swing has no impact-excited ringing mode. Keep only its body cue.
        keep_body_component(recipe);
    }

    if ((cue.flags & static_cast<std::uint32_t>(HitFlag::power_attack)) != 0) {
        recipe.level = std::min(0.96F, recipe.level * 1.12F);
    }
    if ((cue.flags & static_cast<std::uint32_t>(HitFlag::blocked)) != 0) {
        recipe.level *= cue.shield_block ? 0.72F : 0.82F;
    }
    if ((cue.flags & static_cast<std::uint32_t>(HitFlag::bash_attack)) != 0) {
        recipe.release_ms = std::min(recipe.release_ms, 10.0F);
    }
    const float frequency_scale = melee_frequency_scale(cue);
    recipe.frequency *= frequency_scale;
    recipe.second_frequency *= frequency_scale;
    return recipe;
}

WaveRecipe spell_recipe(SpellKind spell, bool sustained) noexcept {
    WaveRecipe recipe{};
    switch (spell) {
    case SpellKind::fire:
        recipe = {
            .frequency = 116.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = sustained ? 0.36F : 0.64F,
            .noise = 0.24F,
            .attack_ms = 1.5F,
            .release_ms = 12.0F,
            .decay_per_second = sustained ? 0.0F : 11.0F,
        };
        break;
    case SpellKind::frost:
        recipe = {
            .frequency = 76.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = sustained ? 0.28F : 0.48F,
            .noise = 0.02F,
            .attack_ms = 4.0F,
            .release_ms = 20.0F,
            .decay_per_second = sustained ? 0.0F : 5.0F,
        };
        break;
    case SpellKind::shock:
        recipe = {
            .frequency = 290.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = sustained ? 0.38F : 0.60F,
            .noise = 0.10F,
            .attack_ms = 0.7F,
            .release_ms = 9.0F,
            .decay_per_second = sustained ? 0.0F : 14.0F,
            .modulation_hz = 12.0F,
        };
        break;
    case SpellKind::healing:
        recipe = {
            .frequency = 188.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = sustained ? 0.24F : 0.38F,
            .noise = 0.01F,
            .attack_ms = 3.0F,
            .release_ms = 14.0F,
            .decay_per_second = sustained ? 0.0F : 6.0F,
        };
        break;
    case SpellKind::ward:
        recipe = {
            .frequency = 128.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = sustained ? 0.19F : 0.30F,
            .noise = 0.02F,
            .attack_ms = 5.0F,
            .release_ms = 17.0F,
            .decay_per_second = sustained ? 0.0F : 5.0F,
        };
        break;
    case SpellKind::generic:
        recipe = {
            .frequency = 174.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = sustained ? 0.15F : 0.24F,
            .noise = 0.02F,
            .attack_ms = 3.0F,
            .release_ms = 14.0F,
            .decay_per_second = sustained ? 0.0F : 6.0F,
        };
        break;
    case SpellKind::unknown:
        return {};
    }
    return recipe;
}

WaveRecipe world_cue_recipe(WorldCueKind kind) noexcept {
    switch (kind) {
    case WorldCueKind::stamina_depleted:
        return {
            .frequency = 64.0F,
            .second_frequency = 100.0F,
            .second_mix = 0.22F,
            .level = 0.56F,
            .noise = 0.03F,
            .attack_ms = 1.0F,
            .release_ms = 3.0F,
            .decay_per_second = 0.0F,
            .modulation_hz = 0.0F,
            .first_pulse_ms = 45,
            .pulse_gap_ms = 45,
            .second_pulse_ms = 45,
        };
    case WorldCueKind::stagger_entry:
        return {
            .frequency = 112.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = 0.52F,
            .noise = 0.11F,
            .attack_ms = 0.8F,
            .release_ms = 12.0F,
            .decay_per_second = 12.0F,
        };
    case WorldCueKind::knockdown_entry:
        return {
            .frequency = 59.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = 0.78F,
            .noise = 0.15F,
            .attack_ms = 0.8F,
            .release_ms = 25.0F,
            .decay_per_second = 3.0F,
        };
    case WorldCueKind::killmove_entry:
        return {
            .frequency = 78.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = 0.34F,
            .noise = 0.08F,
            .attack_ms = 3.0F,
            .release_ms = 18.0F,
            .decay_per_second = 4.0F,
        };
    case WorldCueKind::landing:
        return {
            .frequency = 68.0F,
            .second_frequency = 112.0F,
            .second_mix = 0.18F,
            .level = 0.50F,
            .noise = 0.16F,
            .attack_ms = 0.6F,
            .release_ms = 18.0F,
            .decay_per_second = 5.0F,
        };
    case WorldCueKind::none:
        return {};
    }
    return {};
}

WaveRecipe footstep_recipe(const BodyCue& cue) noexcept {
    const detail::FootstepTuning tuning = detail::footstep_tuning(cue.gait, cue.mounted);
    if (tuning.duration_ms == 0 || cue.duration_ms == 0) {
        return {};
    }

    float frequency{};
    if (cue.mounted) {
        switch (cue.gait) {
        case MovementGait::walking:
            frequency = 62.0F;
            break;
        case MovementGait::running:
            frequency = 73.0F;
            break;
        case MovementGait::sprinting:
            frequency = 84.0F;
            break;
        case MovementGait::unknown:
        case MovementGait::sneaking:
            return {};
        }
    } else {
        switch (cue.gait) {
        case MovementGait::walking:
            frequency = 98.0F;
            break;
        case MovementGait::running:
            frequency = 110.0F;
            break;
        case MovementGait::sprinting:
            frequency = 122.0F;
            break;
        case MovementGait::sneaking:
            frequency = 96.0F;
            break;
        case MovementGait::unknown:
            return {};
        }
    }

    return {
        .frequency = frequency,
        .second_frequency = 0.0F,
        .second_mix = 0.0F,
        .level = tuning.body_level,
        .noise = 0.02F,
        .attack_ms = 1.0F,
        .release_ms = 5.0F,
        .decay_per_second = 0.0F,
    };
}

float survival_level(unsigned stage) noexcept {
    switch (std::clamp(stage, 3U, 5U)) {
    case 3:
        return 0.35F;
    case 4:
        return 0.45F;
    case 5:
        return 0.55F;
    default:
        return 0.35F;
    }
}

WaveRecipe content_recipe(const BodyCue& cue) noexcept {
    switch (cue.kind) {
    case BodyCueKind::survival_cold: {
        WaveRecipe recipe{
            .frequency = 244.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = survival_level(cue.content_stage),
            .noise = 0.02F,
            .attack_ms = 5.0F,
            .release_ms = 24.0F,
            .decay_per_second = 0.0F,
        };
        recipe.modulation_hz = 8.0F;
        return recipe;
    }
    case BodyCueKind::survival_hunger: {
        WaveRecipe recipe{
            .frequency = 58.0F,
            .second_frequency = 72.0F,
            .second_mix = 0.12F,
            .level = survival_level(cue.content_stage),
            .noise = 0.01F,
            .attack_ms = 1.0F,
            .release_ms = 20.0F,
            .decay_per_second = 0.0F,
        };
        recipe.first_pulse_ms = 72;
        recipe.pulse_gap_ms = 50;
        recipe.second_pulse_ms = 72;
        return recipe;
    }
    case BodyCueKind::survival_exhaustion: {
        WaveRecipe recipe{
            .frequency = 46.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = survival_level(cue.content_stage),
            .noise = 0.01F,
            .attack_ms = 14.0F,
            .release_ms = 34.0F,
            .decay_per_second = 0.0F,
        };
        recipe.modulation_hz = 1.2F;
        return recipe;
    }
    case BodyCueKind::transformation:
        switch (cue.beast) {
        case BeastForm::werewolf:
            return {
                .frequency = 48.0F,
                .second_frequency = 0.0F,
                .second_mix = 0.0F,
                .level = 0.56F,
                .noise = 0.02F,
                .attack_ms = 22.0F,
                .release_ms = 28.0F,
                .decay_per_second = 0.0F,
                .modulation_hz = 1.1F,
            };
        case BeastForm::vampire_lord:
            return {
                .frequency = 62.0F,
                .second_frequency = 0.0F,
                .second_mix = 0.0F,
                .level = 0.60F,
                .noise = 0.01F,
                .attack_ms = 20.0F,
                .release_ms = 26.0F,
                .decay_per_second = 0.0F,
                .modulation_hz = 1.3F,
            };
        case BeastForm::none:
            return {};
        }
        return {};
    case BodyCueKind::footstep:
    case BodyCueKind::none:
    case BodyCueKind::melee_swing:
    case BodyCueKind::melee_hit:
    case BodyCueKind::incoming_hit:
    case BodyCueKind::bow_draw:
    case BodyCueKind::bow_full_draw:
    case BodyCueKind::bow_release:
    case BodyCueKind::bow_tension:
    case BodyCueKind::crossbow_shot:
    case BodyCueKind::spell_release:
    case BodyCueKind::concentration:
    case BodyCueKind::voice_fire:
    case BodyCueKind::critical_health:
        return {};
    }
    return {};
}

WaveRecipe cue_recipe(const BodyCue& cue) noexcept {
    switch (cue.kind) {
    case BodyCueKind::melee_swing:
    case BodyCueKind::melee_hit:
        return melee_recipe(cue);
    case BodyCueKind::incoming_hit:
        if (cue.shield_block) {
            return {
                .frequency = 78.0F,
                .second_frequency = 0.0F,
                .second_mix = 0.0F,
                .level = 0.64F,
                .noise = 0.03F,
                .attack_ms = 0.8F,
                .release_ms = 15.0F,
                .decay_per_second = 8.0F,
            };
        }
        return {
            .frequency = 104.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = 0.82F,
            .noise = 0.18F,
            .attack_ms = 0.8F,
            .release_ms = 18.0F,
            .decay_per_second = 7.0F,
        };
    case BodyCueKind::bow_draw:
        return {
            .frequency = 118.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = 0.22F,
            .noise = 0.24F,
            .attack_ms = 3.0F,
            .release_ms = 12.0F,
            .decay_per_second = 8.0F,
        };
    case BodyCueKind::bow_full_draw:
        return {
            .frequency = 265.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = 0.42F,
            .noise = 0.05F,
            .attack_ms = 2.0F,
            .release_ms = 10.0F,
            .decay_per_second = 12.0F,
        };
    case BodyCueKind::bow_release:
        return {
            .frequency = 340.0F,
            .second_frequency = 170.0F,
            .second_mix = 0.12F,
            .level = 0.72F,
            .noise = 0.16F,
            .attack_ms = 0.4F,
            .release_ms = 11.0F,
            .decay_per_second = 20.0F,
        };
    case BodyCueKind::bow_tension:
        switch (cue.bow_phase) {
        case BowPhase::draw:
            return {
                .frequency = 118.0F,
                .second_frequency = 0.0F,
                .second_mix = 0.0F,
                .level = 0.20F,
                .noise = 0.04F,
                .attack_ms = 18.0F,
                .release_ms = 18.0F,
                .decay_per_second = 0.0F,
            };
        case BowPhase::attached:
            return {
                .frequency = 150.0F,
                .second_frequency = 0.0F,
                .second_mix = 0.0F,
                .level = 0.24F,
                .noise = 0.02F,
                .attack_ms = 18.0F,
                .release_ms = 18.0F,
                .decay_per_second = 0.0F,
            };
        case BowPhase::drawn:
            return {
                .frequency = 160.0F,
                .second_frequency = 0.0F,
                .second_mix = 0.0F,
                .level = 0.10F,
                .noise = 0.01F,
                .attack_ms = 24.0F,
                .release_ms = 24.0F,
                .decay_per_second = 0.0F,
            };
        case BowPhase::none:
        case BowPhase::releasing:
        case BowPhase::released:
        case BowPhase::next_attack:
        case BowPhase::follow_through:
        case BowPhase::unknown:
            return {};
        }
        return {};
    case BodyCueKind::crossbow_shot:
        return {
            .frequency = 72.0F,
            .second_frequency = 0.0F,
            .second_mix = 0.0F,
            .level = 0.90F,
            .noise = 0.12F,
            .attack_ms = 0.8F,
            .release_ms = 18.0F,
            .decay_per_second = 8.0F,
        };
    case BodyCueKind::spell_release:
    case BodyCueKind::concentration: {
        WaveRecipe recipe = spell_recipe(cue.spell, cue.kind == BodyCueKind::concentration);
        if (cue.dual_cast && recipe.frequency > 0.0F) {
            recipe.second_frequency = recipe.frequency * 0.5F;
            recipe.second_mix = 0.12F;
        }
        return recipe;
    }
    case BodyCueKind::voice_fire: {
        const detail::VoiceProfile profile = detail::voice_profile(cue.shout_level);
        return {
            .frequency = profile.pcm_frequency_hz,
            .second_frequency = profile.pcm_second_frequency_hz,
            .second_mix = profile.pcm_second_mix,
            .level = profile.pcm_level,
            .noise = profile.pcm_noise,
            .attack_ms = profile.pcm_attack_ms,
            .release_ms = profile.pcm_release_ms,
            .decay_per_second = profile.pcm_decay_per_second,
        };
    }
    case BodyCueKind::critical_health:
        return {
            .frequency = 66.0F,
            .second_frequency = 94.0F,
            .second_mix = 0.18F,
            .level = 0.30F,
            .noise = 0.02F,
            .attack_ms = 1.0F,
            .release_ms = 3.0F,
            .decay_per_second = 5.0F,
            .modulation_hz = 0.0F,
            .first_pulse_ms = 55,
            .pulse_gap_ms = 100,
            .second_pulse_ms = 45,
        };
    case BodyCueKind::survival_cold:
    case BodyCueKind::survival_hunger:
    case BodyCueKind::survival_exhaustion:
    case BodyCueKind::transformation:
        return content_recipe(cue);
    case BodyCueKind::footstep:
        return footstep_recipe(cue);
    case BodyCueKind::none:
        return {};
    }
    return {};
}

void apply_held_cue_audio(WaveRecipe& recipe, const BodyCue& cue) noexcept {
    if (!cue.audio_haptic || !inputnext::adapters::valid_audio_haptic_profile(*cue.audio_haptic)) {
        return;
    }

    const auto& profile = *cue.audio_haptic;
    double primary_gain = 0.0;
    double secondary_gain = 0.0;
    for (std::size_t index = 0; index < inputnext::adapters::audio_haptic_knot_count; ++index) {
        primary_gain += static_cast<double>(profile.envelope.primary[index]);
        secondary_gain += static_cast<double>(profile.envelope.secondary[index]);
    }
    const auto knot_count = static_cast<double>(inputnext::adapters::audio_haptic_knot_count);
    primary_gain /= knot_count;
    secondary_gain /= knot_count;
    const double total_gain = primary_gain + secondary_gain;
    if (!std::isfinite(primary_gain) || !std::isfinite(secondary_gain) ||
        !std::isfinite(total_gain) || total_gain <= 0.0) {
        return;
    }
    const double second_mix = secondary_gain / total_gain;
    const double adjusted_level = static_cast<double>(recipe.level) * total_gain;
    if (!std::isfinite(second_mix) || !std::isfinite(adjusted_level) || adjusted_level <= 0.0 ||
        adjusted_level > static_cast<double>(std::numeric_limits<float>::max())) {
        return;
    }
    const auto projected_mix = static_cast<float>(second_mix);
    const auto projected_level = static_cast<float>(adjusted_level);
    if (!std::isfinite(projected_mix) || !std::isfinite(projected_level) ||
        projected_level <= 0.0F) {
        return;
    }

    recipe.frequency = profile.frequency;
    recipe.second_frequency = profile.second_frequency;
    recipe.second_mix = projected_mix;
    recipe.level = projected_level;
    recipe.audio_envelope = {};
}

void apply_cue_audio(WaveRecipe& recipe, const BodyCue& cue, bool held) noexcept {
    if (held) {
        apply_held_cue_audio(recipe, cue);
        return;
    }
    if (!cue.audio_haptic) {
        return;
    }
    if (!inputnext::adapters::apply_audio_haptic_profile(recipe, *cue.audio_haptic)) {
        return;
    }
    if (cue.kind == BodyCueKind::melee_swing || cue.kind == BodyCueKind::melee_hit) {
        const float scale = melee_frequency_scale(cue);
        recipe.frequency *= scale;
        recipe.second_frequency *= scale;
    }
}

WaveRecipe tuned_cue_recipe(const BodyCue& cue, bool held) noexcept {
    WaveRecipe recipe =
        cue.world_kind == WorldCueKind::none ? cue_recipe(cue) : world_cue_recipe(cue.world_kind);
    apply_cue_audio(recipe, cue, held);
    if (std::isfinite(cue.frequency_hz) && cue.frequency_hz >= 20.0F &&
        cue.frequency_hz <= 1000.0F && recipe.frequency > 0.0F) {
        const float secondary_ratio =
            recipe.second_frequency > 0.0F ? recipe.second_frequency / recipe.frequency : 0.0F;
        recipe.frequency = cue.frequency_hz;
        if (secondary_ratio > 0.0F) {
            recipe.second_frequency = cue.frequency_hz * secondary_ratio;
        }
    }
    // Apply carrier bounds after hit modifiers and the user override, preserving disabled voices.
    if (recipe.frequency > 0.0F) {
        recipe.frequency = std::clamp(recipe.frequency, 20.0F, 1000.0F);
    }
    if (recipe.second_frequency > 0.0F) {
        recipe.second_frequency = std::clamp(recipe.second_frequency, 20.0F, 1000.0F);
    }
    return recipe;
}

} // namespace

inputnext::adapters::BodyProgram make_body_program(const BodyPlan& plan) noexcept {
    using inputnext::adapters::BodyVoice;
    inputnext::adapters::BodyProgram program{};
    enum class ChannelRouting : std::uint8_t { left = 0x1U, right = 0x2U, both = 0x3U };

    const auto add_voice = [&program](std::size_t slot,
                               const std::optional<BodyCue>& cue,
                               float position_gain,
                               ChannelRouting routing,
                               bool held) noexcept {
        if (!cue) {
            return;
        }
        BodyVoice voice{};
        voice.wave = tuned_cue_recipe(*cue, held);
        voice.started_at = cue->started_at;
        voice.duration_ms = cue->duration_ms;
        voice.stable_id = cue->stable_id;
        voice.sequence = cue->sequence;
        voice.source_id = cue->source_id;
        voice.priority = cue->priority;
        voice.scale = cue->scale;
        voice.hd_scale = cue->hd_scale;
        voice.duration_scale = cue->duration_scale;
        voice.position_gain = position_gain;
        voice.channel_mask = static_cast<std::uint8_t>(routing);
        voice.held = held;
        program.voices[slot] = voice;
    };

    add_voice(0, plan.held[0], held_position_gain, ChannelRouting::left, true);
    add_voice(1, plan.held[1], held_position_gain, ChannelRouting::right, true);
    add_voice(2,
        plan.hand_transient[0],
        hand_transient_gain(plan.hand_transient[0]),
        ChannelRouting::left,
        false);
    add_voice(3,
        plan.hand_transient[1],
        hand_transient_gain(plan.hand_transient[1]),
        ChannelRouting::right,
        false);
    add_voice(4, plan.body_transient, body_transient_position_gain, ChannelRouting::both, false);
    return program;
}

BodyPcmWindow build_body_pcm(const BodyPlan& plan,
    MonoTime begins_at,
    std::span<float> stereo_destination) noexcept {
    return inputnext::adapters::build_body_pcm(make_body_program(plan),
        begins_at,
        stereo_destination);
}
} // namespace skyrim_adapter
