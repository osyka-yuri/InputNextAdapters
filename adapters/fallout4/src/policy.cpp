#include "fallout4_adapter/policy.hpp"

#include "fallout4_adapter/effect_settings.hpp"
#include "fallout4_adapter/model.hpp"

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include "effect_recipes.hpp"

#include <input_next_effects.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace fallout4_adapter {
namespace {
using inputnext::adapters::TriggerIntent;
constexpr TriggerAdjustment fallback_trigger_adjustment{};

[[nodiscard]] float bounded_scale(float value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0F, 1.0F) : 0.0F;
}

[[nodiscard]] float
bounded_setting(float value, float fallback, float minimum, float maximum) noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

void sanitize_effect_settings(EffectSettings& settings) noexcept {
    if (!inputnext::adapters::valid_led_settings(settings.leds)) {
        settings.leds.enabled = false;
    }
    for (auto& adjustment : settings.body) {
        adjustment.hd_scale = bounded_setting(adjustment.hd_scale, 1.0F, 0.0F, 2.0F);
        adjustment.classic_scale = bounded_setting(adjustment.classic_scale, 1.0F, 0.0F, 2.0F);
        adjustment.duration_scale = bounded_setting(adjustment.duration_scale, 1.0F, 0.25F, 2.0F);
        if (adjustment.audio_haptic &&
            !inputnext::adapters::valid_audio_haptic_profile(*adjustment.audio_haptic)) {
            adjustment.audio_haptic.reset();
        }
        if (adjustment.frequency_hz) {
            if (std::isfinite(*adjustment.frequency_hz)) {
                *adjustment.frequency_hz = std::clamp(*adjustment.frequency_hz, 20.0F, 1000.0F);
            } else {
                adjustment.frequency_hz.reset();
            }
        }
    }

    for (auto& adjustment : settings.triggers) {
        adjustment.strength_scale = bounded_setting(adjustment.strength_scale, 1.0F, 0.0F, 2.0F);
        if (adjustment.resistance_start) {
            if (std::isfinite(*adjustment.resistance_start)) {
                *adjustment.resistance_start = std::clamp(*adjustment.resistance_start, 0.0F, 1.0F);
            } else {
                adjustment.resistance_start.reset();
            }
        }
        if (adjustment.vibration_frequency_code &&
            (*adjustment.vibration_frequency_code == 0 ||
                *adjustment.vibration_frequency_code > 255)) {
            adjustment.vibration_frequency_code.reset();
        }
    }
}

[[nodiscard]] std::optional<std::size_t> body_index(BodyEffect cue) noexcept {
    const auto index = static_cast<std::size_t>(cue);
    return index < body_effect_count ? std::optional{index} : std::nullopt;
}

[[nodiscard]] std::optional<TriggerProfile> trigger_profile_for(WeaponFamily family) noexcept {
    switch (family) {
    case WeaponFamily::pistol:
        return TriggerProfile::pistol;
    case WeaponFamily::rifle:
        return TriggerProfile::rifle;
    case WeaponFamily::shotgun:
        return TriggerProfile::shotgun;
    case WeaponFamily::heavy:
        return TriggerProfile::heavy;
    case WeaponFamily::energy:
        return TriggerProfile::energy;
    case WeaponFamily::generic_firearm:
        return TriggerProfile::generic_firearm;
    case WeaponFamily::unarmed:
        return TriggerProfile::unarmed;
    case WeaponFamily::melee:
        return TriggerProfile::melee;
    case WeaponFamily::two_handed_melee:
        return TriggerProfile::two_handed_melee;
    case WeaponFamily::rotary:
        return TriggerProfile::rotary;
    case WeaponFamily::flame:
        return TriggerProfile::flame;
    case WeaponFamily::cryo:
        return TriggerProfile::cryo;
    case WeaponFamily::launcher:
        return TriggerProfile::launcher;
    case WeaponFamily::none:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] const TriggerAdjustment* trigger_adjustment_for(const EffectSettings& settings,
    WeaponFamily family) noexcept {
    const auto profile = trigger_profile_for(family);
    if (!profile) {
        // Unclassified firearms use the fallback shot accent without a held or configurable profile.
        return family == WeaponFamily::none ? &fallback_trigger_adjustment : nullptr;
    }
    const auto index = static_cast<std::size_t>(*profile);
    return index < settings.triggers.size() ? &settings.triggers[index] : nullptr;
}

[[nodiscard]] bool
fresh(MonoTime sampled_at, MonoTime now, std::chrono::milliseconds maximum_age) noexcept {
    return sampled_at <= now && now - sampled_at <= maximum_age;
}

struct FeedbackParams {
    float start{0.0F};
    float strength{0.0F};
    std::uint32_t ttl_ms{effect_ttl_max_ms};
};

struct ShotPulseParams {
    detail::ShotTriggerRecipe recipe{};
    float trigger_scale{};
    float profile_scale{1.0F};
    std::uint32_t ttl_ms{};
};

[[nodiscard]] TriggerIntent make_feedback(FeedbackParams params) noexcept {
    TriggerIntent intent{};
    intent.payload.mode = INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK;
    intent.payload.start = std::clamp(params.start, 0.0F, 1.0F);
    intent.payload.end = 1.0F;
    intent.payload.strength = std::clamp(params.strength, 0.0F, 1.0F);
    intent.payload.frequency = 0.0F;
    intent.ttl_ms = std::clamp(params.ttl_ms, 1U, effect_ttl_max_ms);
    return intent;
}

[[nodiscard]] bool shot_trigger_context_available(const Observation& observation) noexcept {
    return observation.mode == Mode::gameplay && observation.weapon.identity != 0 &&
           observation.weapon.drawn && observation.weapon.ready && !observation.reloading &&
           !observation.blocking &&
           (observation.attack_trigger == TriggerSide::left ||
               observation.attack_trigger == TriggerSide::right);
}

[[nodiscard]] TriggerIntent make_shot_pulse(ShotPulseParams params) noexcept {
    TriggerIntent intent{};
    intent.payload.mode = INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION;
    intent.payload.start = std::clamp(params.recipe.start, 0.0F, 1.0F);
    intent.payload.end = 1.0F;
    intent.payload.strength =
        std::clamp(params.recipe.strength * bounded_scale(params.trigger_scale) *
                       std::clamp(params.profile_scale, 0.0F, 2.0F),
            0.0F,
            0.44F);
    // The SDK defines this as a device parameter, not a physical-frequency value.
    intent.payload.frequency = params.recipe.device_frequency;
    intent.ttl_ms = std::clamp(params.ttl_ms, 1U, effect_ttl_max_ms);
    return intent;
}

void assign_trigger(OutputFrame& frame, TriggerSide side, TriggerIntent intent) noexcept {
    if (side == TriggerSide::left) {
        frame.left = intent;
    } else if (side == TriggerSide::right) {
        frame.right = intent;
    }
}

void populate_held_trigger_profiles(OutputFrame& frame,
    const Observation& observation,
    WeaponFamily family,
    bool ranged_profile,
    bool melee_profile,
    float trigger_scale,
    const TriggerAdjustment& adjustment) noexcept {
    const auto attack_side = observation.attack_trigger;
    const auto secondary_side =
        observation.aim_trigger != attack_side ? observation.aim_trigger : TriggerSide::none;
    if (!(ranged_profile || melee_profile) || !observation.weapon.ready || observation.reloading) {
        return;
    }

    const auto trigger = detail::trigger_recipe_for(family);
    const float automatic_scale = ranged_profile && observation.weapon.automatic ? 0.90F : 1.0F;
    if (!observation.blocking && attack_side != TriggerSide::none) {
        assign_trigger(frame,
            attack_side,
            make_feedback({
                .start = adjustment.resistance_start.value_or(trigger.resistance_start),
                .strength =
                    trigger.strength * trigger_scale * adjustment.strength_scale * automatic_scale,
            }));
    }

    const bool secondary_active = (ranged_profile && observation.aiming && !observation.blocking) ||
                                  (melee_profile && observation.blocking);
    if (secondary_active && secondary_side != TriggerSide::none) {
        assign_trigger(frame,
            secondary_side,
            make_feedback({
                .start = trigger.secondary_start,
                .strength = trigger.secondary_strength * trigger_scale * adjustment.strength_scale,
            }));
    }
}

[[nodiscard]] bool is_vats(Mode mode) noexcept {
    return mode == Mode::vats_selection || mode == Mode::vats_playback;
}

[[nodiscard]] bool valid_mode(Mode mode) noexcept {
    switch (mode) {
    case Mode::suspended:
    case Mode::gameplay:
    case Mode::vats_selection:
    case Mode::vats_playback:
    case Mode::transition:
    case Mode::menu:
        return true;
    }
    return false;
}

[[nodiscard]] bool allows_event(EventKind kind, Mode mode) noexcept {
    if (mode == Mode::gameplay) {
        return true;
    }
    if (mode == Mode::vats_playback) {
        return kind != EventKind::footstep;
    }
    return mode == Mode::vats_selection && kind == EventKind::hit_received;
}

[[nodiscard]] bool has_ranged_profile(WeaponFamily family) noexcept {
    switch (family) {
    case WeaponFamily::pistol:
    case WeaponFamily::rifle:
    case WeaponFamily::shotgun:
    case WeaponFamily::heavy:
    case WeaponFamily::energy:
    case WeaponFamily::generic_firearm:
    case WeaponFamily::rotary:
    case WeaponFamily::flame:
    case WeaponFamily::cryo:
    case WeaponFamily::launcher:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool has_melee_profile(WeaponFamily family) noexcept {
    return family == WeaponFamily::unarmed || family == WeaponFamily::melee ||
           family == WeaponFamily::two_handed_melee;
}

[[nodiscard]] BodyEffect shot_cue(WeaponFamily family) noexcept {
    switch (family) {
    case WeaponFamily::pistol:
        return BodyEffect::pistol_shot;
    case WeaponFamily::rifle:
        return BodyEffect::rifle_shot;
    case WeaponFamily::shotgun:
        return BodyEffect::shotgun_shot;
    case WeaponFamily::heavy:
        return BodyEffect::heavy_shot;
    case WeaponFamily::energy:
        return BodyEffect::energy_shot;
    case WeaponFamily::rotary:
        return BodyEffect::rotary_shot;
    case WeaponFamily::flame:
        return BodyEffect::flame_shot;
    case WeaponFamily::cryo:
        return BodyEffect::cryo_shot;
    case WeaponFamily::launcher:
        return BodyEffect::launcher_shot;
    default:
        return BodyEffect::generic_shot;
    }
}

[[nodiscard]] bool valid_event(const PolicyEvent& event, const Observation& observation) noexcept {
    if (event.sequence == 0 || !std::isfinite(event.magnitude) || event.magnitude < 0.0F ||
        event.magnitude > 1.0F) {
        return false;
    }
    return event.kind != EventKind::shot ||
           (event.weapon_identity != 0 && observation.weapon.identity != 0 &&
               event.weapon_identity == observation.weapon.identity);
}

struct LedEventEvidence {
    std::optional<MonoTime> incoming_hit_at;
    std::optional<MonoTime> confirmed_damage_at;
};

[[nodiscard]] LedEventEvidence collapse_led_event_evidence(std::span<const PolicyEvent> events,
    const Observation& observation,
    MonoTime now) noexcept {
    LedEventEvidence evidence{};
    for (const auto& event : events) {
        if (event.kind != EventKind::hit_received || !fresh(event.arrived_at, now, event_max_age) ||
            !allows_event(event.kind, observation.mode) || !valid_event(event, observation)) {
            continue;
        }
        if (event.confirmed_positive_health_damage) {
            if (!evidence.confirmed_damage_at || event.arrived_at > *evidence.confirmed_damage_at) {
                evidence.confirmed_damage_at = event.arrived_at;
            }
        } else if (!event.health_damage_known &&
                   (!evidence.incoming_hit_at || event.arrived_at > *evidence.incoming_hit_at)) {
            evidence.incoming_hit_at = event.arrived_at;
        }
    }
    return evidence;
}

[[nodiscard]] BodyEffect incoming_hit_cue(const PolicyEvent& event) noexcept {
    if (event.explosion) {
        return BodyEffect::explosive_hit;
    }
    if (event.blocked) {
        return BodyEffect::blocked_hit;
    }
    if (event.crippled_limb) {
        return BodyEffect::crippled_hit;
    }
    return BodyEffect::incoming_hit;
}

struct OutgoingHitCue {
    BodyEffect effect{};
    std::optional<int> priority_override;
};

[[nodiscard]] OutgoingHitCue outgoing_hit_cue(const PolicyEvent& event) noexcept {
    if (event.blocked) {
        return {.effect = BodyEffect::blocked_hit, .priority_override = std::nullopt};
    }
    if (event.critical) {
        return {.effect = BodyEffect::critical_hit, .priority_override = std::nullopt};
    }
    if (event.bash) {
        return {.effect = BodyEffect::bash, .priority_override = std::nullopt};
    }
    if (event.power_attack) {
        return {.effect = BodyEffect::power_attack, .priority_override = std::nullopt};
    }
    if (event.explosion) {
        return {
            .effect = BodyEffect::explosive_hit,
            .priority_override = detail::recipe_for(BodyEffect::outgoing_hit).priority,
        };
    }
    if (event.crippled_limb) {
        return {.effect = BodyEffect::crippled_hit, .priority_override = std::nullopt};
    }
    if (event.melee) {
        return {.effect = BodyEffect::melee_hit, .priority_override = std::nullopt};
    }
    return {.effect = BodyEffect::outgoing_hit, .priority_override = std::nullopt};
}

[[nodiscard]] BodyEffect footstep_cue(const PolicyEvent& event) noexcept {
    if (event.power_armor) {
        return BodyEffect::footstep;
    }
    return event.sprinting ? BodyEffect::sprint_step : BodyEffect::ordinary_step;
}

const inputnext::adapters::LedSettings& sanitize_and_get_leds(PolicyConfig& config) noexcept {
    config.trigger_scale = bounded_scale(config.trigger_scale);
    config.body_scale = bounded_scale(config.body_scale);
    sanitize_effect_settings(config.effects);
    return config.effects.leds;
}

} // namespace

Policy::Policy() noexcept : config_{}, led_policy_{sanitize_and_get_leds(config_)} {
}

Policy::Policy(const PolicyConfig& config) noexcept
    : config_{config},
      led_policy_{sanitize_and_get_leds(config_)} {
}

void Policy::clear_voices() noexcept {
    body_voice_mixer_.clear();
    last_shot_at_.reset();
}

void Policy::reset_resources() noexcept {
    resource_cue_tracker_.reset();
}

void Policy::reset_mechanic_baseline() noexcept {
    previous_mechanics_.reset();
}

void Policy::reset_effects() noexcept {
    reset_effects_preserving_led();
    led_policy_.reset();
}

void Policy::reset_effects_preserving_led() noexcept {
    body_voice_mixer_.reset();
    last_shot_at_.reset();
    reset_resources();
    reset_mechanic_baseline();
    landing_detector_.reset();
    previous_weapon_identity_.reset();
}

void Policy::reset_preserving_led() noexcept {
    reset_effects_preserving_led();
    previous_mode_.reset();
}

void Policy::pause_led_for_context(MonoTime now) noexcept {
    led_policy_.pause(now);
}

void Policy::reset() noexcept {
    reset_effects();
    previous_mode_.reset();
}

void Policy::add_cue(BodyEffect cue,
    MonoTime now,
    MonoTime started_at,
    float intensity,
    std::optional<int> priority_override,
    float duration_multiplier) noexcept {
    const auto index = body_index(cue);
    if (!index) {
        return;
    }
    body_voice_mixer_.add({.effect = cue,
                              .started_at = started_at,
                              .intensity = intensity,
                              .priority_override = priority_override,
                              .duration_multiplier = duration_multiplier},
        now,
        config_.effects.body[*index],
        {.scale = bounded_scale(config_.body_scale),
            .body_enabled = config_.effects.body_enabled,
            .audio_haptics_enabled = config_.effects.audio_haptics_enabled});
}

void Policy::update_landing(const Observation& observation, MonoTime now) noexcept {
    const auto cue = landing_detector_.update(observation.mode,
        observation.landing,
        observation.sampled_at,
        observation_max_age);
    if (cue) {
        add_cue(cue->effect,
            now,
            cue->started_at,
            cue->intensity,
            cue->priority_override,
            cue->duration_multiplier);
    }
}

void Policy::update_resources(const Observation& observation, MonoTime now) noexcept {
    if (!config_.resource_cues) {
        return;
    }

    const auto cues =
        resource_cue_tracker_.update({.health_ratio = observation.health_ratio,
                                         .action_points_ratio = observation.action_points_ratio,
                                         .critical_count = observation.critical_count},
            now);
    for (std::size_t index = 0; index < cues.count; ++index) {
        const auto& cue = cues.values[index];
        add_cue(cue.effect,
            now,
            cue.started_at,
            cue.intensity,
            cue.priority_override,
            cue.duration_multiplier);
    }
}

void Policy::update_mechanic_cues(const Observation& observation, MonoTime now) noexcept {
    if (observation.mode != Mode::gameplay) {
        reset_mechanic_baseline();
        return;
    }

    if (previous_mechanics_) {
        const auto previous = *previous_mechanics_;
        if (previous.weapon_identity != 0 && observation.weapon.identity != 0 &&
            previous.weapon_identity == observation.weapon.identity && !previous.reloading &&
            observation.reloading) {
            add_cue(BodyEffect::reload_start, now, now);
        }
        if (previous.power_armor != observation.power_armor) {
            add_cue(observation.power_armor ? BodyEffect::power_armor_enter
                                            : BodyEffect::power_armor_exit,
                now,
                now);
        }
    }

    previous_mechanics_ = MechanicBaseline{
        .weapon_identity = observation.weapon.identity,
        .reloading = observation.reloading,
        .power_armor = observation.power_armor,
    };
}

void Policy::add_event(const PolicyEvent& event,
    const Observation& observation,
    MonoTime now) noexcept {
    if (!valid_event(event, observation)) {
        return;
    }

    const float intensity = 0.62F + (0.38F * event.magnitude);
    switch (event.kind) {
    case EventKind::shot: {
        add_cue(shot_cue(observation.weapon.family), now, event.arrived_at, intensity);
        const auto* adjustment = trigger_adjustment_for(config_.effects, observation.weapon.family);
        if (config_.effects.triggers_enabled && config_.trigger_scale > 0.0F && adjustment &&
            adjustment->enabled && shot_trigger_context_available(observation) &&
            (!last_shot_at_ || event.arrived_at > *last_shot_at_)) {
            last_shot_at_ = event.arrived_at;
        }
        break;
    }
    case EventKind::hit_received: {
        // Incoming damage owns the highest policy role even when a specialized
        // blocked or crippled recipe intentionally uses a softer waveform.
        const int incoming_damage_priority = detail::recipe_for(BodyEffect::incoming_hit).priority;
        add_cue(incoming_hit_cue(event),
            now,
            event.arrived_at,
            intensity,
            incoming_damage_priority);
        break;
    }
    case EventKind::hit_dealt: {
        const auto selection = outgoing_hit_cue(event);
        add_cue(selection.effect, now, event.arrived_at, intensity, selection.priority_override);
        break;
    }
    case EventKind::footstep:
        add_cue(footstep_cue(event), now, event.arrived_at, intensity);
        break;
    }
}

void Policy::populate_triggers(OutputFrame& frame,
    const Observation& observation,
    MonoTime now) const noexcept {
    const auto family = observation.weapon.family;
    const bool ranged_profile = has_ranged_profile(family);
    const bool melee_profile = has_melee_profile(family);
    const auto* adjustment = trigger_adjustment_for(config_.effects, family);
    if (!config_.enabled || !config_.effects.triggers_enabled ||
        observation.mode != Mode::gameplay || !(config_.trigger_scale > 0.0F) || !adjustment ||
        !adjustment->enabled || observation.weapon.identity == 0 || !observation.weapon.drawn) {
        return;
    }

    populate_held_trigger_profiles(frame,
        observation,
        family,
        ranged_profile,
        melee_profile,
        config_.trigger_scale,
        *adjustment);

    if (!shot_trigger_context_available(observation) || !last_shot_at_ || *last_shot_at_ > now) {
        return;
    }

    auto shot_recipe = detail::shot_trigger_recipe_for(family, observation.weapon.automatic);
    if (adjustment->vibration_frequency_code) {
        shot_recipe.device_frequency = static_cast<float>(*adjustment->vibration_frequency_code);
    }
    const auto expires_at = *last_shot_at_ + std::chrono::milliseconds{shot_recipe.duration_ms};
    if (now >= expires_at) {
        return;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(expires_at - now);
    if (remaining.count() == 0) {
        return;
    }
    assign_trigger(frame,
        observation.attack_trigger,
        make_shot_pulse({
            .recipe = shot_recipe,
            .trigger_scale = config_.trigger_scale,
            .profile_scale = adjustment->strength_scale,
            .ttl_ms = static_cast<std::uint32_t>(remaining.count()),
        }));
}

OutputFrame Policy::make_frame(const Observation& observation, MonoTime now) noexcept {
    OutputFrame frame{};
    populate_triggers(frame, observation, now);
    body_voice_mixer_.populate(frame,
        now,
        config_.enabled && config_.effects.body_enabled && bounded_scale(config_.body_scale) > 0.0F,
        effect_ttl_max_ms);
    return frame;
}

bool Policy::prepare_observation(const Observation& observation, MonoTime now) noexcept {
    if (!config_.enabled || !valid_mode(observation.mode) || observation.mode == Mode::suspended ||
        observation.mode == Mode::transition ||
        !fresh(observation.sampled_at, now, observation_max_age)) {
        reset();
        return false;
    }
    if (observation.mode == Mode::menu) {
        reset_preserving_led();
        pause_led_for_context(now);
        return false;
    }
    return true;
}

OutputFrame Policy::update(const Observation& observation,
    std::span<const PolicyEvent> events,
    MonoTime now) noexcept {
    if (!prepare_observation(observation, now)) {
        return {};
    }
    const auto led_event_evidence = collapse_led_event_evidence(events, observation, now);

    body_voice_mixer_.expire(now);

    const bool mode_changed = previous_mode_ && *previous_mode_ != observation.mode;
    const bool weapon_changed =
        previous_weapon_identity_ && *previous_weapon_identity_ != observation.weapon.identity;
    if (mode_changed || weapon_changed) {
        clear_voices();
        reset_mechanic_baseline();
    }
    previous_weapon_identity_ = observation.weapon.identity;

    if (!config_.effects.triggers_enabled || !(config_.trigger_scale > 0.0F) ||
        !shot_trigger_context_available(observation)) {
        last_shot_at_.reset();
    }

    if (config_.ui_cues && previous_mode_) {
        if (*previous_mode_ == Mode::gameplay && is_vats(observation.mode)) {
            add_cue(BodyEffect::vats_enter, now, now);
        } else if (is_vats(*previous_mode_) && observation.mode == Mode::gameplay) {
            add_cue(BodyEffect::vats_exit, now, now);
        }
    }
    previous_mode_ = observation.mode;

    update_resources(observation, now);
    for (const auto& event : events) {
        if (!fresh(event.arrived_at, now, event_max_age) ||
            !allows_event(event.kind, observation.mode)) {
            continue;
        }
        add_event(event, observation, now);
    }
    update_mechanic_cues(observation, now);
    update_landing(observation, now);
    auto frame = make_frame(observation, now);
    if (observation.mode == Mode::gameplay) {
        auto led_observation = observation.led;
        // The outer, admitted sample timestamp is authoritative for every copied LED fact.
        led_observation.sampled_at = observation.sampled_at;
        led_observation.incoming_hit_at = led_event_evidence.incoming_hit_at;
        led_observation.confirmed_damage_at = led_event_evidence.confirmed_damage_at;
        frame.leds = led_policy_.update(led_observation, now);
    } else {
        // VATS and other non-gameplay modes must not retain gameplay light output.
        led_policy_.reset();
    }
    return frame;
}
} // namespace fallout4_adapter
