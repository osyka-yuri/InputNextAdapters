#include "skyrim_adapter/policy.hpp"

#include "skyrim_adapter/content_effects.hpp"
#include "skyrim_adapter/detail/body_cue_history.hpp"
#include "skyrim_adapter/detail/effect_mixer.hpp"
#include "skyrim_adapter/detail/ingress.hpp"
#include "skyrim_adapter/effect_settings.hpp"
#include "skyrim_adapter/model.hpp"
#include "skyrim_adapter/world_effects.hpp"

#include "effect_recipes.hpp"
#include "effect_tuning.hpp"

#include <input_next_effects.h>
#include <inputnext_adapters/led_policy.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <tuple>

namespace skyrim_adapter {
namespace {
using Milliseconds = std::chrono::milliseconds;
using detail::RumbleShape;

constexpr int held_priority = 10;
constexpr detail::BodyCueSlot left_transient_position = detail::BodyCueSlot::left_transient;
constexpr detail::BodyCueSlot right_transient_position = detail::BodyCueSlot::right_transient;
constexpr detail::BodyCueSlot body_transient_position = detail::BodyCueSlot::body_transient;

float unit_value(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    return std::clamp(value, 0.0F, 1.0F);
}

bool fresh(MonoTime timestamp, MonoTime now, Milliseconds maximum_age) noexcept {
    return timestamp <= now && now - timestamp <= maximum_age;
}

bool event_fresh(MonoTime timestamp, MonoTime now) noexcept {
    return fresh(timestamp, now, event_max_age) && now - timestamp < event_max_age;
}

bool valid_observation(const Observation& observation, MonoTime now) noexcept {
    return observation.player_id != 0 && observation.available && observation.alive &&
           observation.focused && observation.menus_clear &&
           fresh(observation.sampled_at, now, observation_max_age);
}

bool same_action(const Action& left, const Action& right) noexcept {
    const auto identity = [](const Action& action) noexcept {
        const RawEvent& event = action.event;
        return std::tie(event.kind,
            event.shout_level,
            event.hand,
            event.actor_id,
            event.target_id,
            event.source_id,
            event.projectile_id,
            event.flags,
            action.source_kind,
            action.weapon,
            action.spell,
            action.cast_mode,
            action.source_known,
            action.incoming,
            action.outgoing,
            action.attributed_player_id,
            action.attributed_mount_id,
            action.gait,
            action.mount_attributed);
    };

    if (identity(left) != identity(right)) {
        return false;
    }

    const std::uint64_t left_sequence = left.event.sequence;
    const std::uint64_t right_sequence = right.event.sequence;
    if (left_sequence != 0 || right_sequence != 0) {
        return left_sequence != 0 && left_sequence == right_sequence;
    }
    return left.event.observed_at == right.event.observed_at;
}

bool duplicate_within_window(const Action& left, const Action& right) noexcept {
    const auto distance = left.event.observed_at < right.event.observed_at
                              ? right.event.observed_at - left.event.observed_at
                              : left.event.observed_at - right.event.observed_at;
    return distance <= Milliseconds{20};
}

bool duplicate_crossbow_shot(const Action& left, const Action& right) noexcept {
    return left.event.kind == EventKind::player_bow_shot &&
           right.event.kind == EventKind::player_bow_shot &&
           left.event.actor_id == right.event.actor_id && left.event.source_id != 0 &&
           left.event.source_id == right.event.source_id && left.source_known &&
           right.source_known && left.source_kind == EquipmentKind::crossbow &&
           right.source_kind == EquipmentKind::crossbow && left.weapon == WeaponClass::crossbow &&
           right.weapon == WeaponClass::crossbow && duplicate_within_window(left, right);
}

bool dual_spell_fire(const Action& action, const Observation& observation) noexcept {
    return action.event.kind == EventKind::spell_fire &&
           (action.event.hand == Hand::left || action.event.hand == Hand::right) &&
           action.source_known && action.source_kind == EquipmentKind::spell &&
           action.event.source_id != 0 && action.spell != SpellKind::unknown &&
           action.cast_mode == CastMode::fire_and_forget &&
           confirmed_dual_spell(observation,
               CastMode::fire_and_forget,
               action.event.source_id,
               action.spell);
}

std::uint32_t remaining_ms(MonoTime now, MonoTime started_at, std::uint32_t duration_ms) noexcept {
    const std::uint32_t elapsed = detail::elapsed_ms(now, started_at);
    return elapsed >= duration_ms ? 0U : duration_ms - elapsed;
}

bool has_equipment(const Observation& observation,
    EquipmentKind equipment,
    WeaponClass weapon) noexcept {
    return std::ranges::any_of(observation.hands, [equipment, weapon](const HandObservation& hand) {
        return hand.equipped_id != 0 && (hand.equipment == equipment || hand.weapon == weapon);
    });
}

bool interruption_action(EventKind kind) noexcept {
    return kind == EventKind::equip || kind == EventKind::end_draw ||
           kind == EventKind::begin_sheathe || kind == EventKind::end_sheathe;
}

void offer_body_rumble(detail::RumbleEnvelope& choice,
    RumbleShape shape,
    float low,
    float high,
    float second_low,
    float second_high,
    MonoTime started_at,
    std::uint32_t duration_ms,
    std::uint32_t peak_ms,
    std::uint32_t gap_ms,
    std::uint32_t second_duration_ms,
    int priority,
    const BodyCue& cue) noexcept {
    detail::tune_classic_recipe(cue,
        detail::ClassicRumbleTuning{
            .low = low,
            .high = high,
            .second_low = second_low,
            .second_high = second_high,
            .duration_ms = duration_ms,
            .peak_ms = peak_ms,
            .gap_ms = gap_ms,
            .second_duration_ms = second_duration_ms,
        });
    detail::EffectMixer::offer_rumble(choice,
        static_cast<std::uint8_t>(shape),
        low,
        high,
        second_low,
        second_high,
        started_at,
        duration_ms,
        peak_ms,
        gap_ms,
        second_duration_ms,
        priority);
}

void tune_action_triggers(detail::ActionRecipe& recipe,
    const Action& action,
    const Observation& observation,
    MonoTime now,
    const EffectSettings& settings) noexcept {
    const TriggerId trigger_id = detail::trigger_id_for_action(action, observation);
    const auto tune = [&](detail::TriggerRecipe& trigger) noexcept {
        if (!trigger.present || trigger.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_OFF) {
            return;
        }
        if (!detail::tune_trigger_payload(trigger.payload, trigger_id, settings) ||
            remaining_ms(now, action.event.observed_at, trigger.duration_ms) == 0) {
            trigger = {};
        }
    };
    tune(recipe.left);
    tune(recipe.right);
}

const inputnext::adapters::LedSettings& sanitize_and_get_leds(PolicyConfig& config) noexcept {
    config.trigger_scale = unit_value(config.trigger_scale);
    config.rumble_scale = unit_value(config.rumble_scale);
    if (!inputnext::adapters::valid_led_settings(config.effects.leds)) {
        config.effects.leds.enabled = false;
    }
    detail::sanitize_effect_settings(config.effects);
    return config.effects.leds;
}

} // namespace

Policy::Policy() noexcept : config_{}, led_policy_{sanitize_and_get_leds(config_)} {
}

Policy::Policy(const PolicyConfig& config) noexcept
    : config_{config},
      led_policy_{sanitize_and_get_leds(config_)} {
}

void Policy::reset() noexcept {
    reset_effect_state();
    led_policy_.reset();
    event_history_ = {};
    next_event_stamp_ = 0;
    body_fresh_after_ = MonoTime{};
    body_fresh_after_set_ = false;
}

void Policy::reset_for_menu(std::optional<MonoTime> pause_started_at) noexcept {
    reset_effect_state();
    if (pause_started_at) {
        led_policy_.pause(*pause_started_at);
    }
    event_history_ = {};
    next_event_stamp_ = 0;
    body_fresh_after_ = MonoTime{};
    body_fresh_after_set_ = false;
}

void Policy::reset_effect_state() noexcept {
    world_effects_.reset();
    content_effects_.reset();
    body_history_.clear_history();
    dual_release_history_ = {};
    next_dual_release_stamp_ = 0;
    effect_mixer_.reset();
    phase_baseline_set_ = false;
    body_bow_phase_ = BowPhase::unknown;
    body_phase_baseline_set_ = false;
    critical_health_.reset();
}

void Policy::reset_body_history(MonoTime fresh_after) noexcept {
    world_effects_.reset();
    content_effects_.reset();
    body_history_.clear_history();
    dual_release_history_ = {};
    next_dual_release_stamp_ = 0;
    effect_mixer_.reset_rumble();
    body_bow_phase_ = BowPhase::unknown;
    body_phase_baseline_set_ = false;
    critical_health_.reset();
    body_fresh_after_ = fresh_after;
    body_fresh_after_set_ = true;
}

void Policy::offer_body_cue(detail::BodyCueSlot slot, BodyCue cue) noexcept {
    body_history_.offer(slot, cue);
}

bool Policy::prepare_step(StepContext& context) noexcept {
    const Observation& observation = context.observation;
    const MonoTime now = context.now;

    if (!valid_observation(observation, now)) {
        reset();
        return false;
    }

    // Collapse incoming-hit evidence before interruption/body/action processing can consume event
    // history. LED attribution is independent of haptic magnitude and does not assert damage.
    update_led(context);

    const bool body_enabled =
        config_.effects.body_enabled &&
        (!config_.effects.respect_game_vibration || observation.body_vibration_enabled);
    if (!body_enabled) {
        reset_body_history(observation.sampled_at);
    }

    context.interruption = has_fresh_interruption(context);
    if (context.interruption) {
        handle_interruption(context);
    }

    context.body_observation_fresh =
        body_enabled && (!body_fresh_after_set_ || observation.sampled_at > body_fresh_after_);
    if (context.body_observation_fresh) {
        body_history_.expire(now);
        body_history_.refresh_held(observation,
            now,
            context.interruption,
            config_.effects,
            config_.rumble_scale);
    }
    return true;
}

bool Policy::has_fresh_interruption(const StepContext& context) const noexcept {
    return std::ranges::any_of(context.actions, [this, &context](const Action& action) {
        return detail::action_attribution_matches(action, context.observation, context.now) &&
               event_fresh(action.event.observed_at, context.now) && !action_seen(action) &&
               interruption_action(action.event.kind);
    });
}

void Policy::handle_interruption(StepContext& context) noexcept {
    MonoTime admission_after = context.observation.sampled_at;
    for (const Action& action : context.actions) {
        if (event_fresh(action.event.observed_at, context.now)) {
            admission_after = std::max(admission_after, action.event.observed_at);
        }
    }

    reset_effect_state();
    for (const Action& action : context.actions) {
        if (detail::action_attribution_matches(action, context.observation, context.now) &&
            event_fresh(action.event.observed_at, context.now) &&
            interruption_action(action.event.kind) && !action_seen(action)) {
            event_history_[next_event_stamp_] = {.action = action, .valid = true};
            next_event_stamp_ = (next_event_stamp_ + 1) % event_history_.size();
        }
    }

    body_history_.fence_all_held_admission(admission_after);
}

bool Policy::action_seen(const Action& action) const noexcept {
    return std::ranges::any_of(event_history_, [&action](const EventStamp& stamp) {
        if (!stamp.valid) {
            return false;
        }
        const bool reused_sequence =
            action.event.sequence != 0 && stamp.action.event.sequence == action.event.sequence;
        if (reused_sequence) {
            return true;
        }
        return (same_action(stamp.action, action) &&
                   duplicate_within_window(stamp.action, action)) ||
               duplicate_crossbow_shot(stamp.action, action);
    });
}

void Policy::add_world_and_content_cues(StepContext& context) noexcept {
    add_world_effect_cue(context);
    add_content_effect_cues(context);
    context.phase_baseline_was_set = phase_baseline_set_;
    phase_baseline_set_ = true;
    add_bow_phase_cue(context);
}

void Policy::add_world_effect_cue(StepContext& context) noexcept {
    if (!context.body_observation_fresh) {
        return;
    }

    const Observation& observation = context.observation;
    const MonoTime now = context.now;
    if (const auto world_effect =
            world_effects_.step(observation, config_.rumble_scale, now, config_.effects)) {
        BodyCue cue = world_effect->body;
        if (detail::tune_body_cue(cue, config_.effects) && detail::body_cue_active(cue, now)) {
            offer_body_cue(body_transient_position, cue);
            RumbleShape shape = RumbleShape::fade;
            switch (world_effect->rumble.shape) {
            case WorldRumbleShape::steady:
                shape = RumbleShape::steady;
                break;
            case WorldRumbleShape::fade:
                shape = RumbleShape::fade;
                break;
            case WorldRumbleShape::double_pulse:
                shape = RumbleShape::double_pulse;
                break;
            case WorldRumbleShape::double_tap:
                shape = RumbleShape::double_tap;
                break;
            }
            const WorldRumble& rumble = world_effect->rumble;
            offer_body_rumble(context.effects.rumble_request,
                shape,
                rumble.low,
                rumble.high,
                rumble.second_low,
                rumble.second_high,
                observation.sampled_at,
                rumble.duration_ms,
                rumble.peak_ms,
                rumble.gap_ms,
                rumble.second_duration_ms,
                rumble.priority,
                cue);
        }
    }
}

void Policy::add_content_effect_cues(StepContext& context) noexcept {
    if (!context.body_observation_fresh) {
        return;
    }

    const Observation& observation = context.observation;
    const MonoTime now = context.now;
    const auto pulses = content_effects_.step(observation.content,
        observation.player_id,
        observation.sampled_at,
        now);
    for (const auto& optional_pulse : pulses) {
        if (!optional_pulse) {
            continue;
        }

        const ContentPulse& pulse = *optional_pulse;
        BodyCue cue{};
        switch (pulse.kind) {
        case ContentCueKind::cold:
            cue.kind = BodyCueKind::survival_cold;
            break;
        case ContentCueKind::hunger:
            cue.kind = BodyCueKind::survival_hunger;
            break;
        case ContentCueKind::exhaustion:
            cue.kind = BodyCueKind::survival_exhaustion;
            break;
        case ContentCueKind::transformation:
            cue.kind = BodyCueKind::transformation;
            break;
        }
        cue.beast = pulse.beast;
        cue.content_stage = pulse.stage;
        cue.started_at = pulse.started_at;
        cue.duration_ms = pulse.duration_ms;
        cue.priority = pulse.priority;
        cue.scale = config_.rumble_scale;
        if (detail::tune_body_cue(cue, config_.effects) && detail::body_cue_active(cue, now)) {
            offer_body_cue(body_transient_position, cue);
            offer_body_rumble(context.effects.rumble_request,
                RumbleShape::fade,
                pulse.low,
                pulse.high,
                0.0F,
                0.0F,
                pulse.started_at,
                pulse.duration_ms,
                0,
                0,
                0,
                pulse.priority,
                cue);
        }
    }
}

void Policy::add_bow_phase_cue(StepContext& context) noexcept {
    if (!context.body_observation_fresh) {
        return;
    }

    const Observation& observation = context.observation;
    const MonoTime now = context.now;
    if (!body_phase_baseline_set_) {
        body_bow_phase_ = observation.bow;
        body_phase_baseline_set_ = true;
        return;
    }

    if (observation.weapon_drawn &&
        has_equipment(observation, EquipmentKind::bow, WeaponClass::bow) &&
        (body_bow_phase_ == BowPhase::draw || body_bow_phase_ == BowPhase::attached) &&
        observation.bow == BowPhase::drawn) {
        BodyCue cue = detail::body_phase_recipe(BodyCueKind::bow_full_draw,
            observation,
            observation.sampled_at,
            config_.rumble_scale);
        if (detail::tune_body_cue(cue, config_.effects) && detail::body_cue_active(cue, now)) {
            offer_body_cue(body_transient_position, cue);
            offer_body_rumble(context.effects.rumble_request,
                RumbleShape::fade,
                0.07F,
                0.22F,
                0.0F,
                0.0F,
                observation.sampled_at,
                45,
                22,
                0,
                0,
                20,
                cue);
        }
    }
    body_bow_phase_ = observation.bow;
}

void Policy::process_actions(StepContext& context) noexcept {
    const Observation& observation = context.observation;
    const MonoTime now = context.now;
    if (context.interruption) {
        return;
    }

    for (const Action& action : context.actions) {
        if (!detail::action_attribution_matches(action, observation, now) ||
            !event_fresh(action.event.observed_at, now)) {
            continue;
        }

        if (this->action_seen(action)) {
            continue;
        }

        event_history_[next_event_stamp_] = {.action = action, .valid = true};
        next_event_stamp_ = (next_event_stamp_ + 1) % event_history_.size();
        process_action(context, action);
    }
}

void Policy::process_action(StepContext& context, const Action& action) noexcept {
    const Observation& observation = context.observation;
    const MonoTime now = context.now;

    if (action.event.kind == EventKind::bow_release) {
        fence_bow_release(action, observation);
    }

    const bool body_action_fresh =
        context.body_observation_fresh &&
        (!body_fresh_after_set_ || action.event.observed_at > body_fresh_after_);
    detail::ActionRecipe recipe = detail::action_recipe(action, observation, now);
    tune_action_triggers(recipe, action, observation, now, config_.effects);
    if (body_action_fresh && dual_spell_fire(action, observation) && recipe.body &&
        recipe.body->dual_cast && !record_or_coalesce_dual_release(action)) {
        recipe.body.reset();
        recipe.rumble = {};
    }

    offer_action_triggers(context, action, recipe);

    BodyCue cue{};
    const bool body_recipe_admitted =
        admit_action_body_recipe(action, recipe, body_action_fresh, now, cue);
    if (body_recipe_admitted && recipe.rumble.present) {
        offer_body_rumble(context.effects.rumble_request,
            recipe.rumble.shape,
            recipe.rumble.low,
            recipe.rumble.high,
            recipe.rumble.second_low,
            recipe.rumble.second_high,
            recipe.rumble.started_at,
            recipe.rumble.duration_ms,
            recipe.rumble.peak_ms,
            recipe.rumble.gap_ms,
            recipe.rumble.second_duration_ms,
            recipe.rumble.priority,
            cue);
    }
}

void Policy::fence_bow_release(const Action& action, const Observation& observation) noexcept {
    const MonoTime admission_after = std::max(observation.sampled_at, action.event.observed_at);
    const auto fence_hand = [this, admission_after](std::size_t index) noexcept {
        body_history_.fence_held_admission(index, admission_after);
    };
    if (action.event.hand == Hand::left) {
        fence_hand(0);
    } else if (action.event.hand == Hand::right) {
        fence_hand(1);
    } else {
        fence_hand(0);
        fence_hand(1);
    }
}

void Policy::offer_action_triggers(StepContext& context,
    const Action& action,
    const detail::ActionRecipe& recipe) noexcept {
    if (recipe.left.present) {
        detail::EffectMixer::offer_trigger(context.effects.left_request,
            recipe.left.payload,
            action.event.observed_at,
            recipe.left.duration_ms,
            recipe.left.priority);
    }
    if (recipe.right.present) {
        detail::EffectMixer::offer_trigger(context.effects.right_request,
            recipe.right.payload,
            action.event.observed_at,
            recipe.right.duration_ms,
            recipe.right.priority);
    }
}

bool Policy::admit_action_body_recipe(const Action& action,
    const detail::ActionRecipe& recipe,
    bool body_action_fresh,
    MonoTime now,
    BodyCue& cue) noexcept {
    bool body_recipe_admitted = false;
    if (recipe.body && body_action_fresh) {
        cue = *recipe.body;
        cue.scale = config_.rumble_scale;
        if (detail::tune_body_cue(cue, config_.effects) && detail::body_cue_active(cue, now)) {
            body_recipe_admitted = true;
            const bool hand_routed_cue =
                cue.kind == BodyCueKind::spell_release ||
                (cue.kind == BodyCueKind::melee_swing && cue.weapon != WeaponClass::two_handed);
            detail::BodyCueSlot position = body_transient_position;
            if (hand_routed_cue && cue.hand == Hand::left) {
                position = left_transient_position;
            } else if (hand_routed_cue && cue.hand == Hand::right) {
                position = right_transient_position;
            }
            offer_body_cue(position, cue);
        }
    } else if (!recipe.body && body_action_fresh && recipe.rumble.present &&
               action.event.kind == EventKind::spell_fire) {
        // A spell release without a known hand keeps its classic fallback but has no PCM route.
        cue.kind = BodyCueKind::spell_release;
        cue.spell = action.spell;
        cue.started_at = recipe.rumble.started_at;
        cue.duration_ms = recipe.rumble.duration_ms;
        cue.priority = recipe.rumble.priority;
        cue.scale = config_.rumble_scale;
        body_recipe_admitted =
            detail::tune_body_cue(cue, config_.effects) && detail::body_cue_active(cue, now);
    }
    return body_recipe_admitted;
}

bool Policy::record_or_coalesce_dual_release(const Action& action) noexcept {
    std::size_t matching_stamp = dual_release_history_.size();
    auto closest_distance = MonoTime::duration::max();
    for (std::size_t index = 0; index < dual_release_history_.size(); ++index) {
        const DualReleaseStamp& stamp = dual_release_history_[index];
        if (!stamp.valid || stamp.source_id != action.event.source_id ||
            stamp.spell != action.spell || stamp.hand == action.event.hand) {
            continue;
        }
        const MonoTime::duration distance = stamp.observed_at < action.event.observed_at
                                                ? action.event.observed_at - stamp.observed_at
                                                : stamp.observed_at - action.event.observed_at;
        if (distance <= Milliseconds{20} && distance < closest_distance) {
            closest_distance = distance;
            matching_stamp = index;
        }
    }

    if (matching_stamp != dual_release_history_.size()) {
        dual_release_history_[matching_stamp].valid = false;
        return false;
    }

    dual_release_history_[next_dual_release_stamp_] = {
        .source_id = action.event.source_id,
        .spell = action.spell,
        .hand = action.event.hand,
        .observed_at = action.event.observed_at,
        .valid = true,
    };
    next_dual_release_stamp_ = (next_dual_release_stamp_ + 1) % dual_release_history_.size();
    return true;
}

void Policy::process_held_state(StepContext& context) noexcept {
    const Observation& observation = context.observation;
    context.admitted_bow_observation = body_history_.has_admitted_bow_observation(observation);
    if (context.admitted_bow_observation) {
        detail::TriggerRecipe bow_trigger =
            detail::bow_draw_trigger(observation, context.phase_baseline_was_set);
        if (bow_trigger.present && detail::tune_trigger_payload(bow_trigger.payload,
                                       TriggerId::bow_draw,
                                       config_.effects)) {
            detail::EffectMixer::offer_trigger(context.effects.right_held,
                bow_trigger.payload,
                observation.sampled_at,
                bow_trigger.duration_ms,
                bow_trigger.priority);
        }
    }

    detail::TriggerRecipe blocking_trigger = detail::block_trigger(observation);
    const TriggerId blocking_trigger_id = detail::trigger_id_for_blocking(observation);
    if (blocking_trigger.present && detail::tune_trigger_payload(blocking_trigger.payload,
                                        blocking_trigger_id,
                                        config_.effects)) {
        detail::EffectMixer::offer_trigger(context.effects.left_held,
            blocking_trigger.payload,
            observation.sampled_at,
            blocking_trigger.duration_ms,
            blocking_trigger.priority);
    }

    process_held_magic(context);
    process_critical_health(context);
}

void Policy::update_led(StepContext& context) noexcept {
    inputnext::adapters::LedObservation led = context.observation.led;
    // All LED inputs share the observation timestamp captured at the start of the game task.
    led.sampled_at = context.observation.sampled_at;

    for (const Action& action : context.actions) {
        if (!action.incoming || action.event.kind != EventKind::hit ||
            !detail::action_attribution_matches(action, context.observation, context.now) ||
            !event_fresh(action.event.observed_at, context.now)) {
            continue;
        }
        if (!led.incoming_hit_at || action.event.observed_at > *led.incoming_hit_at) {
            led.incoming_hit_at = action.event.observed_at;
        }
    }

    context.frame.leds = led_policy_.update(led, context.now);
}

void Policy::process_held_magic(StepContext& context) noexcept {
    const Observation& observation = context.observation;
    if (!observation.weapon_drawn) {
        return;
    }

    for (std::size_t index = 0; index < observation.hands.size(); ++index) {
        if (!body_history_.held_admission_open(index, observation.sampled_at)) {
            continue;
        }
        process_held_magic_hand(context, index);
    }
}

void Policy::process_held_magic_hand(StepContext& context, std::size_t index) noexcept {
    const Observation& observation = context.observation;
    const MonoTime now = context.now;
    const bool body_observation_fresh = context.body_observation_fresh;
    detail::TriggerEnvelope& left_held = context.effects.left_held;
    detail::TriggerEnvelope& right_held = context.effects.right_held;

    detail::HeldMagicRecipe magic = detail::held_magic_recipe(observation.hands[index]);
    const TriggerId magic_trigger_id = detail::trigger_id_for_held_magic(observation.hands[index]);
    if (magic.trigger.present && magic.trigger.payload.mode != INPUT_NEXT_EFFECTS_TRIGGER_OFF &&
        !detail::tune_trigger_payload(magic.trigger.payload, magic_trigger_id, config_.effects)) {
        magic.trigger = {};
    }
    detail::TriggerEnvelope& channel = index == 0 ? left_held : right_held;
    if (magic.trigger.present) {
        detail::EffectMixer::offer_trigger(channel,
            magic.trigger.payload,
            observation.sampled_at,
            magic.trigger.duration_ms,
            magic.trigger.priority);
    }
    if (magic.rumble_present && body_observation_fresh) {
        const auto cue = body_history_.held_cue(index, now);
        if (cue) {
            offer_body_rumble(context.effects.held_rumble_request,
                RumbleShape::steady,
                magic.rumble_low,
                magic.rumble_high,
                0.0F,
                0.0F,
                observation.sampled_at,
                hold_ttl_ms,
                0,
                0,
                0,
                held_priority,
                *cue);
        }
    }
}

void Policy::process_critical_health(StepContext& context) noexcept {
    const auto cue = critical_health_.update(context.observation,
        context.now,
        context.body_observation_fresh,
        config_.effects,
        config_.rumble_scale);
    if (!cue) {
        return;
    }

    offer_body_cue(body_transient_position, *cue);
    offer_body_rumble(context.effects.rumble_request,
        RumbleShape::double_pulse,
        0.16F,
        0.02F,
        0.10F,
        0.01F,
        context.now,
        200,
        55,
        100,
        45,
        5,
        *cue);
}

void Policy::finish_step(StepContext& context) noexcept {
    const auto led_intent = context.frame.leds;
    context.frame = effect_mixer_.finish(context.now,
        context.effects,
        body_history_.materialize(context.now),
        bow_rumble_fallback(context),
        config_.trigger_scale,
        config_.rumble_scale);
    context.frame.leds = led_intent;
}

detail::RumbleFallback Policy::bow_rumble_fallback(const StepContext& context) const noexcept {
    const Observation& observation = context.observation;
    const MonoTime now = context.now;
    if (observation.weapon_drawn &&
        (observation.bow == BowPhase::draw || observation.bow == BowPhase::attached) &&
        context.admitted_bow_observation && context.body_observation_fresh &&
        has_equipment(observation, EquipmentKind::bow, WeaponClass::bow)) {
        std::optional<BodyCue> tension_cue;
        for (std::size_t index = 0; index < 2; ++index) {
            const auto candidate = body_history_.held_cue(index, now);
            if (candidate && candidate->kind == BodyCueKind::bow_tension &&
                candidate->bow_phase == observation.bow) {
                tension_cue = candidate;
                break;
            }
        }
        if (tension_cue) {
            float low = observation.bow == BowPhase::draw ? 0.03F : 0.04F;
            float high = observation.bow == BowPhase::draw ? 0.07F : 0.10F;
            float second_low = 0.0F;
            float second_high = 0.0F;
            std::uint32_t duration_ms = hold_ttl_ms;
            std::uint32_t peak_ms = 0;
            std::uint32_t gap_ms = 0;
            std::uint32_t second_duration_ms = 0;
            detail::tune_classic_recipe(*tension_cue,
                detail::ClassicRumbleTuning{
                    .low = low,
                    .high = high,
                    .second_low = second_low,
                    .second_high = second_high,
                    .duration_ms = duration_ms,
                    .peak_ms = peak_ms,
                    .gap_ms = gap_ms,
                    .second_duration_ms = second_duration_ms,
                });
            return {
                .low = unit_value(low),
                .high = unit_value(high),
                .ttl_ms = duration_ms,
                .present = true,
            };
        }
    }
    return {};
}

EffectFrame Policy::step(const Observation& observation,
    std::span<const Action> actions,
    MonoTime now) noexcept {
    StepContext context{
        .observation = observation,
        .actions = actions,
        .now = now,
    };
    if (!prepare_step(context)) {
        return context.frame;
    }
    add_world_and_content_cues(context);
    process_actions(context);
    process_held_state(context);
    finish_step(context);
    return context.frame;
}
} // namespace skyrim_adapter
