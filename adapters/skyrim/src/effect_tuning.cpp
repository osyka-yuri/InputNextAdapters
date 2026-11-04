#include "effect_tuning.hpp"

#include "skyrim_adapter/content_observation.hpp"
#include "skyrim_adapter/effect_settings.hpp"
#include "skyrim_adapter/model.hpp"

#include <input_next_effects.h>
#include <inputnext_adapters/audio_haptic_profile.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace skyrim_adapter::detail {
namespace {
constexpr EffectId invalid_effect = EffectId::count;
constexpr TriggerId invalid_trigger = TriggerId::count;

bool valid_effect_id(EffectId id) noexcept {
    return static_cast<std::size_t>(id) < effect_descriptors.size();
}

bool valid_trigger_id(TriggerId id) noexcept {
    return static_cast<std::size_t>(id) < trigger_descriptors.size();
}

bool supports_held_audio_profile(EffectId id) noexcept {
    switch (id) {
    case EffectId::bow_tension_draw:
    case EffectId::bow_tension_attached:
    case EffectId::bow_tension_drawn:
    case EffectId::fire_held:
    case EffectId::frost_held:
    case EffectId::shock_held:
    case EffectId::healing_held:
    case EffectId::ward_held:
    case EffectId::generic_held:
        return true;
    default:
        return false;
    }
}

float bounded(float value, float minimum, float maximum, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

std::uint32_t scaled_milliseconds(std::uint32_t value, float scale) noexcept {
    if (value == 0 || scale == 1.0F) {
        return value;
    }
    const double scaled = std::round(static_cast<double>(value) * static_cast<double>(scale));
    if (!std::isfinite(scaled) ||
        scaled >= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(std::max(1.0, scaled));
}

EffectId melee_effect(WeaponClass weapon, bool hit) noexcept {
    if (hit) {
        switch (weapon) {
        case WeaponClass::unarmed:
            return EffectId::unarmed_hit;
        case WeaponClass::dagger:
            return EffectId::dagger_hit;
        case WeaponClass::sword:
            return EffectId::sword_hit;
        case WeaponClass::axe:
            return EffectId::axe_hit;
        case WeaponClass::mace:
            return EffectId::mace_hit;
        case WeaponClass::two_handed:
            return EffectId::two_handed_hit;
        case WeaponClass::none:
        case WeaponClass::bow:
        case WeaponClass::crossbow:
        case WeaponClass::unknown:
            return invalid_effect;
        }
    } else {
        switch (weapon) {
        case WeaponClass::unarmed:
            return EffectId::unarmed_swing;
        case WeaponClass::dagger:
            return EffectId::dagger_swing;
        case WeaponClass::sword:
            return EffectId::sword_swing;
        case WeaponClass::axe:
            return EffectId::axe_swing;
        case WeaponClass::mace:
            return EffectId::mace_swing;
        case WeaponClass::two_handed:
            return EffectId::two_handed_swing;
        case WeaponClass::none:
        case WeaponClass::bow:
        case WeaponClass::crossbow:
        case WeaponClass::unknown:
            return invalid_effect;
        }
    }
    return invalid_effect;
}

EffectId magic_effect(SpellKind spell, bool held) noexcept {
    switch (spell) {
    case SpellKind::fire:
        return held ? EffectId::fire_held : EffectId::fire_release;
    case SpellKind::frost:
        return held ? EffectId::frost_held : EffectId::frost_release;
    case SpellKind::shock:
        return held ? EffectId::shock_held : EffectId::shock_release;
    case SpellKind::healing:
        return held ? EffectId::healing_held : EffectId::healing_release;
    case SpellKind::ward:
        return held ? EffectId::ward_held : EffectId::ward_release;
    case SpellKind::generic:
        return held ? EffectId::generic_held : EffectId::generic_release;
    case SpellKind::unknown:
        return invalid_effect;
    }
    return invalid_effect;
}

EffectId footstep_effect(const BodyCue& cue) noexcept {
    if (cue.mounted) {
        switch (cue.gait) {
        case MovementGait::walking:
            return EffectId::mounted_walk;
        case MovementGait::running:
            return EffectId::mounted_run;
        case MovementGait::sprinting:
            return EffectId::mounted_sprint;
        case MovementGait::unknown:
        case MovementGait::sneaking:
            return invalid_effect;
        }
    } else {
        switch (cue.gait) {
        case MovementGait::walking:
            return EffectId::footstep_walk;
        case MovementGait::running:
            return EffectId::footstep_run;
        case MovementGait::sprinting:
            return EffectId::footstep_sprint;
        case MovementGait::sneaking:
            return EffectId::footstep_sneak;
        case MovementGait::unknown:
            return invalid_effect;
        }
    }
    return invalid_effect;
}

} // namespace

EffectId effect_id_for_body_cue(const BodyCue& cue) noexcept {
    switch (cue.world_kind) {
    case WorldCueKind::stamina_depleted:
        return EffectId::stamina_depleted;
    case WorldCueKind::stagger_entry:
        return EffectId::stagger;
    case WorldCueKind::knockdown_entry:
        return EffectId::knockdown;
    case WorldCueKind::killmove_entry:
        return EffectId::killmove;
    case WorldCueKind::landing:
        return EffectId::landing;
    case WorldCueKind::none:
        break;
    }

    switch (cue.kind) {
    case BodyCueKind::melee_swing:
        return melee_effect(cue.weapon, false);
    case BodyCueKind::melee_hit:
        return melee_effect(cue.weapon, true);
    case BodyCueKind::incoming_hit:
        return cue.shield_block ? EffectId::shield_hit : EffectId::incoming_hit;
    case BodyCueKind::bow_draw:
        return EffectId::bow_draw;
    case BodyCueKind::bow_full_draw:
        return EffectId::bow_full_draw;
    case BodyCueKind::bow_release:
        return EffectId::bow_release;
    case BodyCueKind::bow_tension:
        switch (cue.bow_phase) {
        case BowPhase::draw:
            return EffectId::bow_tension_draw;
        case BowPhase::attached:
            return EffectId::bow_tension_attached;
        case BowPhase::drawn:
            return EffectId::bow_tension_drawn;
        case BowPhase::none:
        case BowPhase::releasing:
        case BowPhase::released:
        case BowPhase::next_attack:
        case BowPhase::follow_through:
        case BowPhase::unknown:
            return invalid_effect;
        }
        return invalid_effect;
    case BodyCueKind::crossbow_shot:
        return EffectId::crossbow_shot;
    case BodyCueKind::spell_release:
        return magic_effect(cue.spell, false);
    case BodyCueKind::concentration:
        return magic_effect(cue.spell, true);
    case BodyCueKind::voice_fire:
        return EffectId::voice;
    case BodyCueKind::critical_health:
        return EffectId::critical_health;
    case BodyCueKind::survival_cold:
        return EffectId::survival_cold;
    case BodyCueKind::survival_hunger:
        return EffectId::survival_hunger;
    case BodyCueKind::survival_exhaustion:
        return EffectId::survival_exhaustion;
    case BodyCueKind::transformation:
        switch (cue.beast) {
        case BeastForm::werewolf:
            return EffectId::werewolf;
        case BeastForm::vampire_lord:
            return EffectId::vampire_lord;
        case BeastForm::none:
            return invalid_effect;
        }
        return invalid_effect;
    case BodyCueKind::footstep:
        return footstep_effect(cue);
    case BodyCueKind::none:
        return invalid_effect;
    }
    return invalid_effect;
}

bool body_profile_admitted(const EffectSettings& settings, EffectId id) noexcept {
    if (!valid_effect_id(id)) {
        return false;
    }
    const EffectAdjustment& adjustment = settings.body[static_cast<std::size_t>(id)];
    const EffectDescriptor& descriptor = effect_descriptors[static_cast<std::size_t>(id)];
    const bool has_hd_gain = bounded(adjustment.hd_scale, 0.0F, 2.0F, 1.0F) > 0.0F;
    const bool has_classic_gain =
        descriptor.classic && bounded(adjustment.classic_scale, 0.0F, 2.0F, 1.0F) > 0.0F;
    return adjustment.enabled && (has_hd_gain || has_classic_gain);
}

TriggerId trigger_id_for_spell(SpellKind spell) noexcept {
    switch (spell) {
    case SpellKind::fire:
        return TriggerId::fire;
    case SpellKind::frost:
        return TriggerId::frost;
    case SpellKind::shock:
        return TriggerId::shock;
    case SpellKind::healing:
        return TriggerId::healing;
    case SpellKind::ward:
        return TriggerId::ward;
    case SpellKind::generic:
        return TriggerId::generic;
    case SpellKind::unknown:
        return invalid_trigger;
    }
    return invalid_trigger;
}

TriggerId trigger_id_for_action(const Action& action, const Observation& observation) noexcept {
    switch (action.event.kind) {
    case EventKind::spell_cast:
    case EventKind::voice_cast:
        return invalid_trigger;
    case EventKind::spell_fire:
        return trigger_id_for_spell(action.spell);
    case EventKind::hit:
        if (action.incoming &&
            (action.event.flags & static_cast<std::uint32_t>(HitFlag::blocked)) != 0 &&
            observation.shield_blocking &&
            observation.hands[0].equipment == EquipmentKind::shield &&
            observation.hands[0].equipped_id != 0) {
            return TriggerId::shield_hit;
        }
        return invalid_trigger;
    case EventKind::weapon_swing:
    case EventKind::voice_fire:
    case EventKind::bow_draw:
    case EventKind::bow_release:
    case EventKind::begin_draw:
    case EventKind::end_draw:
    case EventKind::begin_sheathe:
    case EventKind::end_sheathe:
    case EventKind::equip:
    case EventKind::footstep:
    case EventKind::player_bow_shot:
        return invalid_trigger;
    }
    return invalid_trigger;
}

TriggerId trigger_id_for_blocking(const Observation& observation) noexcept {
    if (!observation.weapon_drawn || !observation.blocking) {
        return invalid_trigger;
    }
    const bool has_shield = std::ranges::any_of(observation.hands, [](const HandObservation& hand) {
        return hand.equipped_id != 0 && hand.equipment == EquipmentKind::shield;
    });
    if (observation.shield_blocking && has_shield) {
        return TriggerId::shield_block;
    }
    const bool has_weapon = std::ranges::any_of(observation.hands, [](const HandObservation& hand) {
        return hand.equipped_id != 0 && hand.equipment == EquipmentKind::weapon;
    });
    return has_weapon ? TriggerId::weapon_block : invalid_trigger;
}

TriggerId trigger_id_for_held_magic(const HandObservation& hand) noexcept {
    if (hand.caster_source_id == 0 || hand.spell == SpellKind::unknown) {
        return invalid_trigger;
    }
    if (hand.cast_mode == CastMode::fire_and_forget && hand.cast_phase == CastPhase::charging) {
        return TriggerId::magic_charge;
    }
    if (hand.cast_mode == CastMode::concentration && hand.cast_phase == CastPhase::casting) {
        return trigger_id_for_spell(hand.spell);
    }
    return invalid_trigger;
}

void sanitize_effect_settings(EffectSettings& settings) noexcept {
    for (std::size_t index = 0; index < settings.body.size(); ++index) {
        EffectAdjustment& adjustment = settings.body[index];
        adjustment.hd_scale = bounded(adjustment.hd_scale, 0.0F, 2.0F, 1.0F);
        adjustment.classic_scale = bounded(adjustment.classic_scale, 0.0F, 2.0F, 1.0F);
        adjustment.duration_scale = effect_descriptors[index].transient
                                        ? bounded(adjustment.duration_scale, 0.25F, 2.0F, 1.0F)
                                        : 1.0F;
        if (!std::isfinite(adjustment.frequency_hz) ||
            (adjustment.frequency_hz != 0.0F &&
                (adjustment.frequency_hz < 20.0F || adjustment.frequency_hz > 1000.0F))) {
            adjustment.frequency_hz = 0.0F;
        }
    }

    for (std::size_t index = 0; index < settings.triggers.size(); ++index) {
        TriggerAdjustment& adjustment = settings.triggers[index];
        adjustment.strength_scale = bounded(adjustment.strength_scale, 0.0F, 2.0F, 1.0F);
        if (adjustment.start && (!std::isfinite(*adjustment.start) || *adjustment.start < 0.0F ||
                                    *adjustment.start > 1.0F)) {
            adjustment.start.reset();
        }
        if (adjustment.frequency_code &&
            (!trigger_descriptors[index].vibration || *adjustment.frequency_code > 255U)) {
            adjustment.frequency_code.reset();
        }
    }
}

bool tune_body_cue(BodyCue& cue, const EffectSettings& settings) noexcept {
    const EffectId id = effect_id_for_body_cue(cue);
    if (!valid_effect_id(id)) {
        return false;
    }
    if (!body_profile_admitted(settings, id)) {
        return false;
    }
    const EffectAdjustment& adjustment = settings.body[static_cast<std::size_t>(id)];

    const EffectDescriptor& descriptor = effect_descriptors[static_cast<std::size_t>(id)];
    cue.hd_scale = bounded(adjustment.hd_scale, 0.0F, 2.0F, 1.0F);
    cue.classic_scale =
        descriptor.classic ? bounded(adjustment.classic_scale, 0.0F, 2.0F, 1.0F) : 0.0F;
    cue.frequency_hz = adjustment.frequency_hz;
    const bool transient = descriptor.transient;
    cue.audio_haptic.reset();
    if (settings.audio_haptics_enabled && (transient || supports_held_audio_profile(id)) &&
        adjustment.audio_haptic &&
        inputnext::adapters::valid_audio_haptic_profile(*adjustment.audio_haptic)) {
        cue.audio_haptic = adjustment.audio_haptic;
    }
    cue.duration_scale = transient ? bounded(adjustment.duration_scale, 0.25F, 2.0F, 1.0F) : 1.0F;
    if (transient) {
        cue.duration_ms = scaled_milliseconds(cue.duration_ms, cue.duration_scale);
    }
    return cue.duration_ms != 0;
}

void tune_classic_recipe(const BodyCue& cue, ClassicRumbleTuning tuning) noexcept {
    const float classic_scale = bounded(cue.classic_scale, 0.0F, 2.0F, 1.0F);
    tuning.low *= classic_scale;
    tuning.high *= classic_scale;
    tuning.second_low *= classic_scale;
    tuning.second_high *= classic_scale;
    const float duration_scale = bounded(cue.duration_scale, 0.25F, 2.0F, 1.0F);
    tuning.duration_ms = scaled_milliseconds(tuning.duration_ms, duration_scale);
    tuning.peak_ms = scaled_milliseconds(tuning.peak_ms, duration_scale);
    tuning.gap_ms = scaled_milliseconds(tuning.gap_ms, duration_scale);
    tuning.second_duration_ms = scaled_milliseconds(tuning.second_duration_ms, duration_scale);
}

bool tune_trigger_payload(input_next_effects_trigger_v1& payload,
    TriggerId id,
    const EffectSettings& settings) noexcept {
    if (payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_OFF) {
        return true;
    }
    if (!settings.triggers_enabled || !valid_trigger_id(id)) {
        return false;
    }
    const TriggerAdjustment& adjustment = settings.triggers[static_cast<std::size_t>(id)];
    if (!adjustment.enabled) {
        return false;
    }
    const float strength_scale = bounded(adjustment.strength_scale, 0.0F, 2.0F, 1.0F);
    payload.strength = bounded(payload.strength * strength_scale, 0.0F, 1.0F, 0.0F);
    if (adjustment.start &&
        (payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK ||
            payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION) &&
        std::isfinite(*adjustment.start) && *adjustment.start >= 0.0F &&
        *adjustment.start <= 1.0F) {
        payload.start = *adjustment.start;
        payload.end = std::max(payload.end, payload.start);
    }
    if (trigger_descriptors[static_cast<std::size_t>(id)].vibration && adjustment.frequency_code &&
        *adjustment.frequency_code <= 255U) {
        payload.frequency = static_cast<float>(*adjustment.frequency_code);
    }
    return payload.strength > 0.0F;
}
} // namespace skyrim_adapter::detail
