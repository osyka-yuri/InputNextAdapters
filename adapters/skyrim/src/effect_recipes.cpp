#include "effect_recipes.hpp"
#include "voice_profile.hpp"

#include <skyrim_adapter/detail/ingress.hpp>
#include <skyrim_adapter/model.hpp>

#include <input_next_effects.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace skyrim_adapter::detail {
namespace {
using Milliseconds = std::chrono::milliseconds;

constexpr auto max_age = std::chrono::milliseconds{150};

float unit_value(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    return std::clamp(value, 0.0F, 1.0F);
}

bool fresh(MonoTime timestamp, MonoTime now) noexcept {
    return timestamp <= now && now - timestamp < max_age;
}

bool flag_set(const RawEvent& event, HitFlag flag) noexcept {
    return (event.flags & static_cast<std::uint32_t>(flag)) != 0;
}

bool melee(WeaponClass weapon) noexcept {
    switch (weapon) {
    case WeaponClass::unarmed:
    case WeaponClass::dagger:
    case WeaponClass::sword:
    case WeaponClass::axe:
    case WeaponClass::mace:
    case WeaponClass::two_handed:
        return true;
    case WeaponClass::none:
    case WeaponClass::bow:
    case WeaponClass::crossbow:
    case WeaponClass::unknown:
        return false;
    }
    return false;
}

void offer_rumble(ActionRecipe& recipe,
    RumbleShape shape,
    float low,
    float high,
    float second_low,
    float second_high,
    std::uint32_t duration_ms,
    std::uint32_t peak_ms,
    std::uint32_t gap_ms,
    std::uint32_t second_duration_ms,
    int priority) noexcept {
    recipe.rumble = {
        .shape = shape,
        .low = unit_value(low),
        .high = unit_value(high),
        .second_low = unit_value(second_low),
        .second_high = unit_value(second_high),
        .started_at = recipe.rumble.started_at,
        .duration_ms = duration_ms,
        .peak_ms = peak_ms,
        .gap_ms = gap_ms,
        .second_duration_ms = second_duration_ms,
        .priority = priority,
        .present = duration_ms != 0,
    };
}

void offer_trigger(TriggerRecipe& recipe,
    const input_next_effects_trigger_v1& payload,
    std::uint32_t duration_ms,
    int priority) noexcept {
    if (duration_ms != 0) {
        recipe = {
            .payload = payload,
            .duration_ms = duration_ms,
            .priority = priority,
            .present = true,
        };
    }
}

void offer_action_body(ActionRecipe& recipe,
    const Action& action,
    BodyCueKind kind,
    Milliseconds duration,
    int priority,
    EquipmentKind source_kind,
    WeaponClass weapon,
    SpellKind spell,
    CastMode cast_mode,
    bool shield_block = false) noexcept {
    if (duration.count() <= 0) {
        return;
    }

    BodyCue cue{};
    cue.kind = kind;
    cue.hand = action.event.hand;
    cue.source_kind = source_kind;
    cue.weapon = weapon;
    cue.spell = spell;
    cue.cast_mode = cast_mode;
    cue.source_id = action.event.source_id;
    cue.flags = action.event.flags;
    cue.shield_block = shield_block;
    cue.started_at = action.event.observed_at;
    cue.duration_ms = static_cast<std::uint32_t>(
        std::min<std::int64_t>(duration.count(), std::numeric_limits<std::uint32_t>::max()));
    cue.priority = priority;
    cue.sequence = action.event.sequence;
    cue.scale = 1.0F;
    recipe.body = cue;
}

std::uint32_t spell_duration(SpellKind spell) noexcept {
    switch (spell) {
    case SpellKind::fire:
        return 55;
    case SpellKind::frost:
        return 75;
    case SpellKind::shock:
        return 95;
    case SpellKind::healing:
    case SpellKind::ward:
    case SpellKind::generic:
        return 60;
    case SpellKind::unknown:
        return 0;
    }
    return 0;
}

input_next_effects_trigger_v1 spell_trigger(SpellKind spell) noexcept {
    switch (spell) {
    case SpellKind::fire:
        return trigger(INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION, 0.12F, 1.0F, 0.28F, 38.0F);
    case SpellKind::frost:
        return trigger(INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION, 0.12F, 1.0F, 0.36F, 18.0F);
    case SpellKind::shock:
        return trigger(INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION, 0.12F, 1.0F, 0.32F, 95.0F);
    case SpellKind::healing:
        return trigger(INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK, 0.12F, 1.0F, 0.18F);
    case SpellKind::ward:
        return trigger(INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK, 0.12F, 1.0F, 0.32F);
    case SpellKind::generic:
        return trigger(INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK, 0.12F, 1.0F, 0.20F);
    case SpellKind::unknown:
        return trigger_off();
    }
    return trigger_off();
}

struct RumbleLevels {
    float low{0.0F};
    float high{0.0F};
};

[[nodiscard]] RumbleLevels spell_rumble(SpellKind spell) noexcept {
    switch (spell) {
    case SpellKind::fire:
        return {.low = 0.06F, .high = 0.12F};
    case SpellKind::frost:
        return {.low = 0.10F, .high = 0.04F};
    case SpellKind::shock:
        return {.low = 0.02F, .high = 0.15F};
    case SpellKind::healing:
        return {.low = 0.03F, .high = 0.04F};
    case SpellKind::ward:
        return {.low = 0.06F, .high = 0.02F};
    case SpellKind::generic:
    case SpellKind::unknown:
        return {.low = 0.0F, .high = 0.0F};
    }
    return {.low = 0.0F, .high = 0.0F};
}
} // namespace

std::uint32_t elapsed_ms(MonoTime now, MonoTime started_at) noexcept {
    if (now < started_at) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    const auto elapsed = std::chrono::duration_cast<Milliseconds>(now - started_at).count();
    if (!std::in_range<std::uint32_t>(elapsed)) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(elapsed);
}

input_next_effects_trigger_v1
trigger(std::uint32_t mode, float start, float end, float strength, float frequency) noexcept {
    return {
        .mode = mode,
        .start = unit_value(start),
        .end = unit_value(end),
        .strength = unit_value(strength),
        .frequency = std::clamp(frequency, 0.0F, 255.0F),
    };
}

input_next_effects_trigger_v1 trigger_off() noexcept {
    return {
        .mode = INPUT_NEXT_EFFECTS_TRIGGER_OFF,
        .start = 0.0F,
        .end = 0.0F,
        .strength = 0.0F,
        .frequency = 0.0F,
    };
}

namespace {
void offer_action_rumble(ActionRecipe& recipe,
    RumbleShape shape,
    float low,
    float high,
    std::uint32_t duration_ms,
    std::uint32_t peak_ms,
    int priority) noexcept {
    offer_rumble(recipe, shape, low, high, 0.0F, 0.0F, duration_ms, peak_ms, 0, 0, priority);
}

void offer_hand_trigger(ActionRecipe& recipe,
    Hand hand,
    const input_next_effects_trigger_v1& payload,
    std::uint32_t duration_ms,
    int priority) noexcept {
    if (hand == Hand::left) {
        offer_trigger(recipe.left, payload, duration_ms, priority);
    } else if (hand == Hand::right) {
        offer_trigger(recipe.right, payload, duration_ms, priority);
    }
}

void weapon_swing_recipe(ActionRecipe& recipe,
    const Action& action,
    const Observation& observation) noexcept {
    if ((!observation.weapon_drawn && action.weapon != WeaponClass::unarmed) ||
        !action.source_known || action.source_kind != EquipmentKind::weapon ||
        !melee(action.weapon)) {
        return;
    }
    float low = 0.0F;
    float high = 0.0F;
    std::uint32_t duration = 0;
    switch (action.weapon) {
    case WeaponClass::unarmed:
        low = 0.14F;
        high = 0.08F;
        duration = 55;
        break;
    case WeaponClass::dagger:
        low = 0.06F;
        high = 0.19F;
        duration = 50;
        break;
    case WeaponClass::sword:
        low = 0.12F;
        high = 0.22F;
        duration = 60;
        break;
    case WeaponClass::axe:
        low = 0.16F;
        high = 0.20F;
        duration = 65;
        break;
    case WeaponClass::mace:
        low = 0.20F;
        high = 0.16F;
        duration = 75;
        break;
    case WeaponClass::two_handed:
        low = 0.26F;
        high = 0.20F;
        duration = 90;
        break;
    case WeaponClass::none:
    case WeaponClass::bow:
    case WeaponClass::crossbow:
    case WeaponClass::unknown:
        break;
    }
    offer_action_rumble(recipe, RumbleShape::steady, low, high, duration, 0, 30);
    offer_action_body(recipe,
        action,
        BodyCueKind::melee_swing,
        Milliseconds{duration},
        30,
        action.source_kind,
        action.weapon,
        SpellKind::unknown,
        CastMode::none);
}

void incoming_hit_recipe(ActionRecipe& recipe,
    const Action& action,
    const Observation& observation,
    std::uint32_t age) noexcept {
    float low = 0.45F;
    float high = 0.25F;
    std::uint32_t duration = 100;
    bool shield_block = false;
    if (flag_set(action.event, HitFlag::power_attack)) {
        low = 0.58F;
        high = 0.30F;
    }
    if (flag_set(action.event, HitFlag::blocked)) {
        const bool shield_equipped = observation.hands[0].equipped_id != 0 &&
                                     observation.hands[0].equipment == EquipmentKind::shield;
        const bool shield = observation.shield_blocking && shield_equipped;
        shield_block = shield;
        low = shield ? 0.55F : 0.35F;
        high = shield ? 0.18F : 0.28F;
        duration = shield ? 60U : 65U;
        if (shield && age < 60U) {
            offer_trigger(recipe.left,
                trigger(INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK, 0.08F, 0.90F, 0.64F),
                60U,
                70);
        }
    }
    offer_action_rumble(recipe, RumbleShape::steady, low, high, duration, 0, 70);
    offer_action_body(recipe,
        action,
        BodyCueKind::incoming_hit,
        Milliseconds{duration},
        70,
        action.source_kind,
        action.weapon,
        SpellKind::unknown,
        CastMode::none,
        shield_block);
}

void outgoing_hit_recipe(ActionRecipe& recipe,
    const Action& action,
    const Observation& observation) noexcept {
    if (!observation.weapon_drawn || !action.outgoing || !action.source_known ||
        action.source_kind != EquipmentKind::weapon || !melee(action.weapon)) {
        return;
    }
    float low = 0.0F;
    float high = 0.0F;
    std::uint32_t duration = 0;
    switch (action.weapon) {
    case WeaponClass::unarmed:
        low = 0.30F;
        high = 0.18F;
        duration = 70;
        break;
    case WeaponClass::dagger:
        low = 0.16F;
        high = 0.50F;
        duration = 60;
        break;
    case WeaponClass::sword:
        low = 0.30F;
        high = 0.42F;
        duration = 80;
        break;
    case WeaponClass::axe:
        low = 0.38F;
        high = 0.36F;
        duration = 90;
        break;
    case WeaponClass::mace:
        low = 0.48F;
        high = 0.30F;
        duration = 100;
        break;
    case WeaponClass::two_handed:
        low = 0.52F;
        high = 0.32F;
        duration = 110;
        break;
    case WeaponClass::none:
    case WeaponClass::bow:
    case WeaponClass::crossbow:
    case WeaponClass::unknown:
        return;
    }
    if (flag_set(action.event, HitFlag::power_attack)) {
        low = std::min(0.70F, low * 1.25F);
        high = std::min(0.70F, high * 1.25F);
        duration = std::min(120U, duration + 10U);
    }
    if (flag_set(action.event, HitFlag::blocked)) {
        low *= 0.5F;
        high *= 0.5F;
    }
    if (flag_set(action.event, HitFlag::bash_attack)) {
        duration = std::min(duration, 45U);
    }
    offer_action_rumble(recipe, RumbleShape::steady, low, high, duration, 0, 60);
    offer_action_body(recipe,
        action,
        BodyCueKind::melee_hit,
        Milliseconds{duration},
        60,
        action.source_kind,
        action.weapon,
        SpellKind::unknown,
        CastMode::none);
}

void hit_recipe(ActionRecipe& recipe,
    const Action& action,
    const Observation& observation,
    std::uint32_t age) noexcept {
    if (action.incoming) {
        incoming_hit_recipe(recipe, action, observation, age);
        return;
    }
    outgoing_hit_recipe(recipe, action, observation);
}

void spell_fire_recipe(ActionRecipe& recipe,
    const Action& action,
    const Observation& observation) noexcept {
    if (!observation.weapon_drawn || !action.source_known || action.spell == SpellKind::unknown ||
        (action.source_kind != EquipmentKind::spell && action.source_kind != EquipmentKind::staff &&
            action.source_kind != EquipmentKind::scroll) ||
        action.cast_mode != CastMode::fire_and_forget) {
        return;
    }
    const std::uint32_t duration = spell_duration(action.spell);
    if (duration == 0) {
        return;
    }
    offer_hand_trigger(recipe, action.event.hand, spell_trigger(action.spell), duration, 100);
    const auto rumble = spell_rumble(action.spell);
    offer_action_rumble(recipe,
        RumbleShape::fade,
        rumble.low,
        rumble.high,
        duration,
        std::min(25U, duration / 2U),
        45);
    if (action.event.hand != Hand::left && action.event.hand != Hand::right) {
        return;
    }
    offer_action_body(recipe,
        action,
        BodyCueKind::spell_release,
        Milliseconds{duration},
        45,
        action.source_kind,
        WeaponClass::none,
        action.spell,
        action.cast_mode);
    if (recipe.body && action.source_kind == EquipmentKind::spell && action.event.source_id != 0 &&
        confirmed_dual_spell(observation,
            CastMode::fire_and_forget,
            action.event.source_id,
            action.spell)) {
        recipe.body->dual_cast = true;
        recipe.body->hand = Hand::unknown;
    }
}
} // namespace

ActionRecipe
action_recipe(const Action& action, const Observation& observation, MonoTime now) noexcept {
    ActionRecipe recipe{};
    recipe.rumble.started_at = action.event.observed_at;
    if (!fresh(action.event.observed_at, now)) {
        return recipe;
    }

    const std::uint32_t age = elapsed_ms(now, action.event.observed_at);
    switch (action.event.kind) {
    case EventKind::weapon_swing:
        weapon_swing_recipe(recipe, action, observation);
        break;
    case EventKind::footstep: {
        const auto attribution =
            current_footstep_attribution(observation, action.event.actor_id, now);
        if (!attribution || attribution->gait != action.gait ||
            attribution->mounted != action.mount_attributed ||
            (attribution->mounted && attribution->mount_id != action.attributed_mount_id)) {
            break;
        }
        const FootstepTuning tuning = footstep_tuning(attribution->gait, attribution->mounted);
        if (tuning.duration_ms == 0) {
            break;
        }
        offer_action_rumble(recipe,
            RumbleShape::fade,
            tuning.fallback_low,
            tuning.fallback_high,
            tuning.duration_ms,
            tuning.duration_ms / 2U,
            tuning.priority);
        offer_action_body(recipe,
            action,
            BodyCueKind::footstep,
            Milliseconds{tuning.duration_ms},
            tuning.priority,
            EquipmentKind::none,
            WeaponClass::none,
            SpellKind::unknown,
            CastMode::none);
        if (recipe.body) {
            recipe.body->gait = attribution->gait;
            recipe.body->mounted = attribution->mounted;
            recipe.body->source_id = attribution->actor_id;
        }
        break;
    }
    case EventKind::player_bow_shot: {
        const bool exact_crossbow =
            action.event.source_id != 0 && action.source_known &&
            action.source_kind == EquipmentKind::crossbow &&
            action.weapon == WeaponClass::crossbow &&
            std::ranges::any_of(observation.hands, [&action](const HandObservation& hand) {
                return hand.equipped_id == action.event.source_id &&
                       hand.equipment == EquipmentKind::crossbow &&
                       hand.weapon == WeaponClass::crossbow;
            });
        if (action.event.actor_id == observation.player_id && exact_crossbow) {
            offer_action_rumble(recipe, RumbleShape::fade, 0.60F, 0.32F, 95, 30, 55);
            offer_action_body(recipe,
                action,
                BodyCueKind::crossbow_shot,
                Milliseconds{95},
                55,
                EquipmentKind::crossbow,
                WeaponClass::crossbow,
                SpellKind::unknown,
                CastMode::none);
        }
        break;
    }
    case EventKind::hit:
        hit_recipe(recipe, action, observation, age);
        break;
    case EventKind::bow_draw:
        if (observation.weapon_drawn && action.source_known &&
            action.source_kind == EquipmentKind::bow && action.event.hand != Hand::left) {
            offer_action_rumble(recipe, RumbleShape::steady, 0.04F, 0.14F, 35, 0, 15);
            offer_action_body(recipe,
                action,
                BodyCueKind::bow_draw,
                Milliseconds{35},
                15,
                action.source_kind,
                WeaponClass::bow,
                SpellKind::unknown,
                CastMode::none);
        }
        break;
    case EventKind::bow_release:
        if (observation.weapon_drawn && action.source_known &&
            action.source_kind == EquipmentKind::bow && action.event.hand != Hand::left) {
            offer_hand_trigger(recipe, action.event.hand, trigger_off(), 60, 100);
            offer_action_rumble(recipe, RumbleShape::fade, 0.12F, 0.52F, 55, 20, 55);
            offer_action_body(recipe,
                action,
                BodyCueKind::bow_release,
                Milliseconds{55},
                55,
                action.source_kind,
                WeaponClass::bow,
                SpellKind::unknown,
                CastMode::none);
        }
        break;
    case EventKind::spell_fire:
        spell_fire_recipe(recipe, action, observation);
        break;
    case EventKind::voice_fire: {
        const VoiceProfile profile = voice_profile(action.event.shout_level);
        offer_action_rumble(recipe,
            RumbleShape::fade,
            profile.classic_low,
            profile.classic_high,
            profile.duration_ms,
            profile.classic_peak_ms,
            50);
        offer_action_body(recipe,
            action,
            BodyCueKind::voice_fire,
            Milliseconds{profile.duration_ms},
            50,
            action.source_kind,
            WeaponClass::none,
            SpellKind::unknown,
            CastMode::none);
        if (recipe.body) {
            recipe.body->shout_level = profile.level;
        }
    } break;
    case EventKind::spell_cast:
    case EventKind::voice_cast:
    case EventKind::begin_draw:
    case EventKind::end_draw:
    case EventKind::begin_sheathe:
    case EventKind::end_sheathe:
    case EventKind::equip:
        break;
    }
    return recipe;
}

BodyCue body_phase_recipe(BodyCueKind kind,
    const Observation& observation,
    MonoTime started_at,
    float scale) noexcept {
    BodyCue cue{};
    cue.kind = kind;
    cue.started_at = started_at;
    cue.scale = unit_value(scale);

    EquipmentKind equipment = EquipmentKind::unknown;
    WeaponClass weapon = WeaponClass::unknown;
    switch (kind) {
    case BodyCueKind::bow_full_draw:
        equipment = EquipmentKind::bow;
        weapon = WeaponClass::bow;
        cue.bow_phase = BowPhase::drawn;
        cue.duration_ms = 45;
        cue.priority = 20;
        break;
    case BodyCueKind::none:
    case BodyCueKind::crossbow_shot:
    case BodyCueKind::footstep:
    case BodyCueKind::melee_swing:
    case BodyCueKind::melee_hit:
    case BodyCueKind::incoming_hit:
    case BodyCueKind::bow_draw:
    case BodyCueKind::bow_release:
    case BodyCueKind::bow_tension:
    case BodyCueKind::spell_release:
    case BodyCueKind::concentration:
    case BodyCueKind::voice_fire:
    case BodyCueKind::critical_health:
    case BodyCueKind::survival_cold:
    case BodyCueKind::survival_hunger:
    case BodyCueKind::survival_exhaustion:
    case BodyCueKind::transformation:
        return {};
    }

    const auto source =
        std::ranges::find_if(observation.hands, [equipment, weapon](const HandObservation& hand) {
            return hand.equipped_id != 0 && (hand.equipment == equipment || hand.weapon == weapon);
        });
    cue.source_kind = equipment;
    cue.weapon = weapon;
    if (source != observation.hands.end()) {
        cue.hand = source == observation.hands.begin() ? Hand::left : Hand::right;
        cue.source_id = source->equipped_id;
    }
    return cue;
}

TriggerRecipe bow_draw_trigger(const Observation& observation, bool baseline_set) noexcept {
    if (!baseline_set || !observation.weapon_drawn ||
        (observation.bow != BowPhase::draw && observation.bow != BowPhase::attached) ||
        !std::ranges::any_of(observation.hands, [](const HandObservation& hand) {
            return hand.equipped_id != 0 &&
                   (hand.equipment == EquipmentKind::bow || hand.weapon == WeaponClass::bow);
        })) {
        return {};
    }
    return {
        .payload = trigger(INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK, 0.12F, 0.42F, 0.28F),
        .duration_ms = hold_ttl_ms,
        .priority = 10,
        .present = true,
    };
}

TriggerRecipe block_trigger(const Observation& observation) noexcept {
    if (!observation.weapon_drawn || !observation.blocking) {
        return {};
    }
    const bool has_shield = std::ranges::any_of(observation.hands, [](const HandObservation& hand) {
        return hand.equipped_id != 0 && hand.equipment == EquipmentKind::shield;
    });
    const bool has_weapon = std::ranges::any_of(observation.hands, [](const HandObservation& hand) {
        return hand.equipped_id != 0 && hand.equipment == EquipmentKind::weapon;
    });
    if (observation.shield_blocking && has_shield) {
        return {
            .payload = trigger(INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK, 0.08F, 0.90F, 0.40F),
            .duration_ms = hold_ttl_ms,
            .priority = 10,
            .present = true,
        };
    }
    if (has_weapon) {
        return {
            .payload = trigger(INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK, 0.08F, 0.90F, 0.22F),
            .duration_ms = hold_ttl_ms,
            .priority = 10,
            .present = true,
        };
    }
    return {};
}

HeldMagicRecipe held_magic_recipe(const HandObservation& hand) noexcept {
    HeldMagicRecipe recipe{};
    if (hand.caster_source_id == 0 || hand.spell == SpellKind::unknown) {
        return recipe;
    }
    if (hand.cast_mode == CastMode::fire_and_forget && hand.cast_phase == CastPhase::charging) {
        recipe.trigger = {
            .payload = trigger(INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK, 0.12F, 1.0F, 0.34F),
            .duration_ms = hold_ttl_ms,
            .priority = 10,
            .present = true,
        };
        return recipe;
    }
    if (hand.cast_mode != CastMode::concentration || hand.cast_phase != CastPhase::casting) {
        return recipe;
    }

    switch (hand.spell) {
    case SpellKind::fire:
        recipe.trigger = {
            .payload = trigger(INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION, 0.12F, 1.0F, 0.28F, 38.0F),
            .duration_ms = hold_ttl_ms,
            .priority = 10,
            .present = true,
        };
        recipe.rumble_low = 0.06F;
        recipe.rumble_high = 0.12F;
        recipe.rumble_present = true;
        break;
    case SpellKind::frost:
        recipe.trigger = {
            .payload = trigger(INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION, 0.12F, 1.0F, 0.36F, 18.0F),
            .duration_ms = hold_ttl_ms,
            .priority = 10,
            .present = true,
        };
        recipe.rumble_low = 0.10F;
        recipe.rumble_high = 0.04F;
        recipe.rumble_present = true;
        break;
    case SpellKind::shock:
        recipe.trigger = {
            .payload = trigger(INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION, 0.12F, 1.0F, 0.32F, 95.0F),
            .duration_ms = hold_ttl_ms,
            .priority = 10,
            .present = true,
        };
        recipe.rumble_low = 0.02F;
        recipe.rumble_high = 0.15F;
        recipe.rumble_present = true;
        break;
    case SpellKind::healing:
        recipe.trigger = {
            .payload = trigger(INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK, 0.12F, 1.0F, 0.18F),
            .duration_ms = hold_ttl_ms,
            .priority = 10,
            .present = true,
        };
        recipe.rumble_low = 0.03F;
        recipe.rumble_high = 0.04F;
        recipe.rumble_present = true;
        break;
    case SpellKind::ward:
        recipe.trigger = {
            .payload = trigger(INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK, 0.12F, 1.0F, 0.32F),
            .duration_ms = hold_ttl_ms,
            .priority = 10,
            .present = true,
        };
        recipe.rumble_low = 0.06F;
        recipe.rumble_high = 0.02F;
        recipe.rumble_present = true;
        break;
    case SpellKind::generic:
        recipe.trigger = {
            .payload = trigger(INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK, 0.12F, 1.0F, 0.20F),
            .duration_ms = hold_ttl_ms,
            .priority = 10,
            .present = true,
        };
        break;
    case SpellKind::unknown:
        break;
    }
    return recipe;
}

} // namespace skyrim_adapter::detail
