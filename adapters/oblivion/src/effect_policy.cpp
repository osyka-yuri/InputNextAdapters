#include "effect_policy.hpp"

#include "body_effects.hpp"
#include "effect_model.hpp"
#include "effect_settings.hpp"
#include "model.hpp"

#include <input_next_effects.h>

#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

namespace oblivion_adapter {
namespace {
using namespace std::chrono_literals;
using inputnext::adapters::LedHealthSample;
using inputnext::adapters::LedObservation;
using inputnext::adapters::OutputFrame;
using inputnext::adapters::TriggerIntent;

constexpr auto held_trigger_ttl_ms = 100U;
constexpr float critical_health_enter = 0.20F;
constexpr float resource_depleted = 0.05F;

float trigger_scale(const EffectSettings& settings) noexcept {
    if (!settings.triggers_enabled) {
        return 0.0F;
    }
    return std::isfinite(settings.trigger_scale) ? std::clamp(settings.trigger_scale, 0.0F, 1.0F)
                                                 : 1.0F;
}

bool fresh(MonoTime sampled_at, MonoTime now) noexcept {
    return sampled_at <= now && now - sampled_at <= effect_max_age;
}

bool event_is_current(const EffectEvent& event,
    const EffectObservation& sample,
    MonoTime now) noexcept {
    if (event.epoch != sample.epoch || !fresh(event.at, now) ||
        (sample.frame != 0 && event.frame > sample.frame)) {
        return false;
    }
    return (event.mode == EffectMode::gameplay && sample.mode == EffectMode::gameplay) ||
           (menu_effect(event.kind, event.mode) &&
               (event.mode == sample.mode || sample.mode == EffectMode::gameplay));
}

CueOrigin cue_origin(const EffectEvent& event) noexcept {
    return {.at = event.at,
        .frame = event.frame,
        .sequence = event.sequence,
        .source_id = event.source_id,
        .target_id = event.target_id};
}

CueOrigin cue_origin(const EffectObservation& sample, MonoTime now) noexcept {
    return {.at = now, .frame = sample.frame, .target_id = sample.player_id};
}

auto event_identity(const EffectEvent& event) noexcept {
    return std::tuple{event.kind,
        event.epoch,
        event.mode,
        event.frame,
        event.actor_id,
        event.target_id,
        event.source_id,
        event.weapon,
        event.magic,
        std::bit_cast<std::uint32_t>(event.amount)};
}

struct WeaponEffects {
    std::optional<EffectId> swing;
    std::optional<EffectId> hit;
};

WeaponEffects weapon_effects(Weapon weapon) noexcept {
    using enum EffectId;
    switch (weapon) {
    case Weapon::unarmed:
        return {.swing = unarmed_swing, .hit = unarmed_hit};
    case Weapon::blade_one_hand:
        return {.swing = blade_one_hand_swing, .hit = blade_one_hand_hit};
    case Weapon::blade_two_hand:
        return {.swing = blade_two_hand_swing, .hit = blade_two_hand_hit};
    case Weapon::blunt_one_hand:
        return {.swing = blunt_one_hand_swing, .hit = blunt_one_hand_hit};
    case Weapon::blunt_two_hand:
        return {.swing = blunt_two_hand_swing, .hit = blunt_two_hand_hit};
    case Weapon::bow:
        return {.swing = std::nullopt, .hit = bow_hit};
    case Weapon::unknown:
    case Weapon::staff:
        return {};
    }
    return {};
}

EffectId reaction_effect(const EffectEvent& event, BowPhase phase) noexcept {
    switch (event.kind) {
    case EffectEventKind::recoil:
        return EffectId::recoil;
    case EffectEventKind::stagger:
        return EffectId::stagger;
    case EffectEventKind::dodge:
        return EffectId::dodge;
    case EffectEventKind::knockout:
        return EffectId::knockout;
    case EffectEventKind::bow_draw:
        return phase == BowPhase::drawing ? EffectId::bow_draw : EffectId::count;
    case EffectEventKind::bow_attach:
        return phase == BowPhase::attached ? EffectId::bow_attached : EffectId::count;
    case EffectEventKind::bow_shot:
        return event.weapon == Weapon::bow && event.target_id == 0 && event.source_id != 0
                   ? EffectId::bow_shot
                   : EffectId::count;
    default:
        return EffectId::count;
    }
}

EffectId gait_effect(Gait gait) noexcept {
    switch (gait) {
    case Gait::walk:
        return EffectId::walk;
    case Gait::run:
        return EffectId::run;
    case Gait::sneak:
        return EffectId::sneak;
    case Gait::mounted_walk:
        return EffectId::mounted_walk;
    case Gait::mounted_run:
        return EffectId::mounted_run;
    case Gait::none:
        return EffectId::count;
    }
    return EffectId::count;
}

EffectId magic_effect(MagicKind kind) noexcept {
    switch (kind) {
    case MagicKind::fire:
        return EffectId::fire;
    case MagicKind::frost:
        return EffectId::frost;
    case MagicKind::shock:
        return EffectId::shock;
    case MagicKind::healing:
        return EffectId::healing;
    case MagicKind::shield:
        return EffectId::shield;
    case MagicKind::generic:
        return EffectId::generic_magic;
    }
    return EffectId::generic_magic;
}

std::optional<EffectId> action_effect(EffectEventKind kind) noexcept {
    switch (kind) {
    case EffectEventKind::water_dive:
        return EffectId::water_dive;
    case EffectEventKind::water_surface:
        return EffectId::water_surface;
    case EffectEventKind::drink_potion:
        return EffectId::drink_potion;
    case EffectEventKind::eat_ingredient:
        return EffectId::eat_ingredient;
    case EffectEventKind::equip:
        return EffectId::equip;
    case EffectEventKind::unequip:
        return EffectId::unequip;
    case EffectEventKind::drop_item:
        return EffectId::drop_item;
    case EffectEventKind::create_potion:
        return EffectId::create_potion;
    case EffectEventKind::enchant:
        return EffectId::enchant;
    case EffectEventKind::create_spell:
        return EffectId::create_spell;
    case EffectEventKind::lock_opened:
        return EffectId::lock_opened;
    case EffectEventKind::pick_broken:
        return EffectId::pick_broken;
    case EffectEventKind::vampire_feed:
        return EffectId::vampire_feed;
    case EffectEventKind::soul_trap:
        return EffectId::soul_trap;
    case EffectEventKind::skill_up:
        return EffectId::skill_up;
    case EffectEventKind::quest_complete:
        return EffectId::quest_complete;
    case EffectEventKind::discovered:
        return EffectId::discovered;
    case EffectEventKind::trained:
        return EffectId::trained;
    case EffectEventKind::pickpocketed:
        return EffectId::pickpocketed;
    case EffectEventKind::gate_closed:
        return EffectId::gate_closed;
    default:
        return std::nullopt;
    }
}

bool same_target_frame(const EffectEvent& left, const EffectEvent& right) noexcept {
    return left.frame != 0 && left.frame == right.frame && left.target_id == right.target_id;
}

bool matches_contact(const EffectEvent& left, const EffectEvent& right) noexcept {
    return same_target_frame(left, right) && left.actor_id == right.actor_id &&
           (left.source_id == 0 || right.source_id == 0 || left.source_id == right.source_id);
}

bool valid_damage(const EffectEvent& event) noexcept {
    return event.kind == EffectEventKind::damage && std::isfinite(event.amount) &&
           event.amount > 0.0F;
}

bool incoming_contact(const EffectEvent& event, const EffectObservation& sample) noexcept {
    return event.target_id == sample.player_id &&
           (valid_damage(event) ||
               (event.kind == EffectEventKind::hit && event.actor_id != sample.player_id));
}

bool fall_damage(const EffectEvent& event, const EffectObservation& sample) noexcept {
    // Fall damage can name the victim as the attacker or omit the attacker.
    return event.target_id == sample.player_id && valid_damage(event) && event.source_id == 0 &&
           (event.actor_id == 0 || event.actor_id == sample.player_id);
}

bool ground_context(const EffectObservation& sample) noexcept {
    return !sample.swimming && sample.mounted && !*sample.mounted &&
           sample.ground != Ground::unknown;
}

const EffectEvent* block_contact(const EffectEvent& impact,
    const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    const EffectEvent* hit = nullptr;
    const EffectEvent* damage = nullptr;
    for (const auto& event : events) {
        if (event.frame != impact.frame || event.at > impact.at ||
            !incoming_contact(event, sample) || event.actor_id == sample.player_id ||
            !event_is_current(event, sample, now)) {
            continue;
        }
        auto*& report = event.kind == EffectEventKind::hit ? hit : damage;
        if (event.actor_id == 0 || (report && event_identity(event) != event_identity(*report))) {
            return nullptr;
        }
        if (!report) {
            report = &event;
        }
    }
    // The animation names only the defender. A matching hit/damage pair is one contact.
    if (hit && damage && !matches_contact(*hit, *damage)) {
        return nullptr;
    }
    return damage ? damage : hit;
}

const EffectEvent* find_hit_weapon(const EffectEvent& hit,
    const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    const EffectEvent* matched = nullptr;
    for (const auto& with : events) {
        if (with.kind != EffectEventKind::hit_with || !same_target_frame(hit, with) ||
            (with.actor_id != 0 && with.actor_id != hit.actor_id) || with.source_id == 0 ||
            with.source_id != hit.source_id || !weapon_effects(with.weapon).hit ||
            !event_is_current(with, sample, now)) {
            continue;
        }
        if (matched && matched->weapon != with.weapon) {
            return nullptr;
        }
        matched = &with;
    }
    return matched;
}

bool attack_magic(const EffectEvent& event) noexcept {
    return event.kind == EffectEventKind::magic_applied && event.source_id != 0 &&
           event.magic != MagicKind::healing && event.magic != MagicKind::shield;
}

MagicKind contact_magic(const EffectEvent& contact,
    const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    if (contact.actor_id == 0 || contact.actor_id == sample.player_id) {
        return MagicKind::generic;
    }
    const EffectEvent* matched = nullptr;
    for (const auto& applied : events) {
        if (!attack_magic(applied) || !matches_contact(contact, applied) ||
            !event_is_current(applied, sample, now)) {
            continue;
        }
        // Damage callbacks carry no source form. Use a matching application to
        // choose the impact profile, with a generic cue when applications conflict.
        if (matched &&
            (matched->source_id != applied.source_id || matched->magic != applied.magic)) {
            return MagicKind::generic;
        }
        matched = &applied;
    }
    return matched ? matched->magic : MagicKind::generic;
}

float impact_strength(const EffectEvent& event, const PlayerHealth& health) noexcept {
    if (!valid_damage(event)) {
        return 1.0F;
    }
    const float fraction = health.valid && std::isfinite(health.maximum) && health.maximum > 0.0F
                               ? std::clamp(event.amount / health.maximum, 0.0F, 1.0F)
                               : 0.1F;
    return 0.68F + (0.57F * std::sqrt(fraction));
}

bool observe_depletion(std::optional<float> current, std::optional<float>& previous) noexcept {
    if (current && !std::isfinite(*current)) {
        current.reset();
    }
    const auto last = std::exchange(previous, current);
    return last && *last > resource_depleted && current && *current <= resource_depleted;
}

} // namespace

EffectPolicy::EffectPolicy(const EffectSettings& settings) noexcept
    : body_(settings),
      trigger_scale_(trigger_scale(settings)),
      menu_effects_enabled_(settings.menu_effects_enabled),
      leds_enabled_(settings.led.enabled && inputnext::adapters::valid_led_settings(settings.led)),
      led_policy_(settings.led) {
}

void EffectPolicy::reset() noexcept {
    reset_body();
    epoch_.reset();
    last_now_.reset();
    led_policy_.reset();
    led_paused_ = false;
}

void EffectPolicy::reset_body() noexcept {
    body_.reset();
    held_ = {};
    gait_.reset();
    event_history_ = {};
    next_event_stamp_ = 0;
    ground_.reset();
    previous_mounted_.reset();
    critical_latched_ = false;
    next_heartbeat_at_.reset();
    previous_magicka_.reset();
    previous_fatigue_.reset();
    previous_mode_ = EffectMode::unavailable;
}

bool EffectPolicy::seen_or_remember(const EffectEvent& event) noexcept {
    const auto identity = event_identity(event);
    if (std::ranges::any_of(event_history_, [&](const auto& previous) {
            return previous && ((event.sequence != 0 && event.sequence == previous->sequence) ||
                                   (identity == event_identity(*previous) &&
                                       (event.frame != 0 || event.at == previous->at)));
        })) {
        return true;
    }
    event_history_[next_event_stamp_] = event;
    next_event_stamp_ = (next_event_stamp_ + 1U) % event_history_.size();
    return false;
}

void EffectPolicy::collect_combat(const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    const auto bow = gameplay_bow_phase(sample);
    for (const auto& event : events) {
        if (event.actor_id != sample.player_id || !event_is_current(event, sample, now)) {
            continue;
        }
        if (event.kind == EffectEventKind::attack) {
            const auto swing = weapon_effects(event.weapon).swing;
            // Use confirmed animation evidence when the worker skips game frames.
            const bool sampled_melee =
                sample.melee_attack && event.frame != 0 && event.frame <= sample.frame &&
                sample.frame - event.frame <= 1 && event.weapon == sample.weapon &&
                event.source_id == sample.weapon_id;
            if (swing && (event.melee_confirmed || sampled_melee) && !seen_or_remember(event)) {
                const bool power = event.melee_confirmed ? event.power_attack : sample.power_attack;
                body_.play(*swing, cue_origin(event), power ? 1.0F : 0.82F);
            }
        } else {
            const auto effect = reaction_effect(event, bow);
            if (effect != EffectId::count && !seen_or_remember(event)) {
                body_.play(effect, cue_origin(event));
            }
        }
    }
}

void EffectPolicy::collect_incoming_contact(const EffectEvent& event,
    const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now,
    bool blocked) noexcept {
    if (event.kind == EffectEventKind::hit &&
        std::ranges::any_of(events, [&](const EffectEvent& damage) {
            return valid_damage(damage) && matches_contact(event, damage) &&
                   event_is_current(damage, sample, now);
        })) {
        return;
    }
    if (seen_or_remember(event)) {
        return;
    }

    auto effect = EffectId::incoming_damage;
    if (blocked) {
        effect = EffectId::block_hit;
    } else {
        const auto elemental = magic_effect(contact_magic(event, sample, events, now));
        if (elemental != EffectId::generic_magic && body_.profile_enabled(elemental)) {
            effect = elemental;
        }
    }
    const auto priority =
        effect == EffectId::block_hit ? CuePriority::profile : CuePriority::impact;
    body_.play(effect, cue_origin(event), impact_strength(event, sample.health), priority);
}

void EffectPolicy::collect_player_hit(const EffectEvent& event,
    const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    if (event.kind != EffectEventKind::hit || event.actor_id != sample.player_id ||
        event.target_id == 0 || event.target_id == sample.player_id || seen_or_remember(event)) {
        return;
    }

    auto effect = EffectId::generic_hit;
    if (event.weapon == Weapon::bow && event.source_id != 0) {
        effect = EffectId::bow_hit;
    } else if (event.weapon == Weapon::unarmed && event.source_id == 0 &&
               (event.melee_confirmed || (sample.weapon == Weapon::unarmed && sample.melee_attack &&
                                             event.frame == sample.frame))) {
        effect = EffectId::unarmed_hit;
    } else if (const auto* with = find_hit_weapon(event, sample, events, now)) {
        if (seen_or_remember(*with)) {
            return;
        }
        effect = weapon_effects(with->weapon).hit.value_or(EffectId::generic_hit);
    }
    body_.play(effect, cue_origin(event));
}

void EffectPolicy::collect_fall_impact(const EffectEvent& event,
    const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    if (event.kind != EffectEventKind::fall_impact || event.target_id != sample.player_id ||
        sample.swimming || sample.mounted != false || seen_or_remember(event)) {
        return;
    }
    const bool damage_reported = std::ranges::any_of(events, [&](const EffectEvent& damage) {
        return fall_damage(damage, sample) && same_target_frame(event, damage) &&
               event_is_current(damage, sample, now);
    });
    if (!damage_reported) {
        body_.play(EffectId::landing, cue_origin(event));
    }
}

void EffectPolicy::collect_block_impacts(const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    for (const auto& impact : events) {
        if (impact.kind != EffectEventKind::block_impact || impact.actor_id != sample.player_id ||
            impact.frame == 0 || !event_is_current(impact, sample, now) ||
            seen_or_remember(impact)) {
            continue;
        }
        if (const auto* blocked = block_contact(impact, sample, events, now)) {
            collect_incoming_contact(*blocked, sample, events, now, true);
        } else {
            body_.play(EffectId::block_hit, cue_origin(impact));
        }
    }
}

void EffectPolicy::collect_contacts(const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    collect_block_impacts(sample, events, now);
    for (const auto& event : events) {
        if (!event_is_current(event, sample, now)) {
            continue;
        }
        if (incoming_contact(event, sample)) {
            collect_incoming_contact(event, sample, events, now, false);
            continue;
        }
        collect_player_hit(event, sample, events, now);
        collect_fall_impact(event, sample, events, now);
    }
}

void EffectPolicy::collect_magic(const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    for (const auto& event : events) {
        const bool cast = event.kind == EffectEventKind::cast && event.actor_id == sample.player_id;
        const bool applied =
            event.kind == EffectEventKind::magic_applied && event.target_id == sample.player_id;
        if ((!cast && !applied) || event.source_id == 0 || !event_is_current(event, sample, now) ||
            seen_or_remember(event)) {
            continue;
        }
        if (applied && std::ranges::any_of(events, [&](const EffectEvent& other) {
                if (!event_is_current(other, sample, now)) {
                    return false;
                }
                if (event.actor_id != 0 && attack_magic(event) && incoming_contact(other, sample) &&
                    matches_contact(event, other)) {
                    return true;
                }
                const bool consumed = other.kind == EffectEventKind::drink_potion ||
                                      other.kind == EffectEventKind::eat_ingredient;
                return consumed && other.actor_id == sample.player_id &&
                       (event.actor_id == 0 || event.actor_id == sample.player_id) &&
                       other.source_id == event.source_id && other.frame != 0 &&
                       other.frame == event.frame;
            })) {
            continue;
        }
        const auto effect = applied && event.magic == MagicKind::generic
                                ? EffectId::magic_applied
                                : magic_effect(event.magic);
        body_.play(effect,
            cue_origin(event),
            cast && event.cast_origin == CastOrigin::power ? 1.15F : 1.0F);
    }
}

void EffectPolicy::collect_actions(const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    for (const auto& event : events) {
        const auto effect = action_effect(event.kind);
        if (effect && event.actor_id == sample.player_id &&
            (event.mode == EffectMode::gameplay || menu_effects_enabled_) &&
            event_is_current(event, sample, now) && !seen_or_remember(event)) {
            body_.play(*effect, cue_origin(event));
        }
    }
}

void EffectPolicy::collect_movement(const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    if (previous_mounted_ && sample.mounted && previous_mounted_ != sample.mounted) {
        body_.play(*sample.mounted ? EffectId::mount : EffectId::dismount, cue_origin(sample, now));
    }
    previous_mounted_ = sample.mounted;

    if (!ground_context(sample)) {
        ground_.reset();
        return;
    }
    const bool continuous =
        ground_ && ground_->cell_id == sample.cell_id && fresh(ground_->at, sample.sampled_at);
    const auto old_peak = continuous ? ground_->airborne_peak : std::nullopt;
    auto position_z = sample.position_z;
    if (position_z && !std::isfinite(*position_z)) {
        position_z.reset();
    }
    if (sample.ground == Ground::airborne) {
        if (old_peak && (!position_z || *position_z < *old_peak)) {
            position_z = old_peak;
        }
        ground_ = GroundSample{.at = sample.sampled_at,
            .cell_id = sample.cell_id,
            .airborne_peak = position_z};
        return;
    }

    // Retain the airborne apex across frames to catch a gradual but substantial descent.
    if (old_peak && position_z && *old_peak - *position_z >= 32.0F &&
        !std::ranges::any_of(events, [&](const EffectEvent& event) {
            return event.frame == sample.frame && event_is_current(event, sample, now) &&
                   ((event.kind == EffectEventKind::fall_impact &&
                        event.target_id == sample.player_id) ||
                       fall_damage(event, sample));
        })) {
        body_.play(EffectId::landing, cue_origin(sample, now));
    }
    ground_ = GroundSample{.at = sample.sampled_at,
        .cell_id = sample.cell_id,
        .airborne_peak = std::nullopt};
}

void EffectPolicy::collect_footfall(const EffectObservation& sample) noexcept {
    const auto effect = gait_effect(sample.gait);
    if (!body_.profile_enabled(effect)) {
        gait_.reset();
    } else if (gait_.observe(sample) != Gait::none) {
        body_.play(effect, cue_origin(sample, sample.sampled_at));
    }
}

void EffectPolicy::collect_condition(const EffectObservation& sample, MonoTime now) noexcept {
    if (!sample.health.valid || !std::isfinite(sample.health.ratio) ||
        sample.health.ratio >= critical_health_exit) {
        critical_latched_ = false;
    } else if (sample.health.ratio < critical_health_enter) {
        critical_latched_ = true;
    }
    if (!critical_latched_ || !sample.in_combat) {
        next_heartbeat_at_.reset();
        body_.cancel(EffectId::heartbeat);
    } else if (!next_heartbeat_at_ || now >= *next_heartbeat_at_) {
        body_.play(EffectId::heartbeat, cue_origin(sample, now));
        next_heartbeat_at_ = now + 950ms;
    }

    if (observe_depletion(sample.magicka_ratio, previous_magicka_)) {
        body_.play(EffectId::magicka_depleted, cue_origin(sample, now));
    }
    if (observe_depletion(sample.fatigue_ratio, previous_fatigue_)) {
        body_.play(EffectId::fatigue_depleted, cue_origin(sample, now));
    }
}

void EffectPolicy::update_held(const EffectObservation& sample, MonoTime now) noexcept {
    const auto update = [now](HeldBodyEffect& held, EffectId effect, std::uint32_t source) {
        if (held.effect != effect || held.source_id != source) {
            held = {.effect = effect, .started_at = now, .source_id = source};
        }
    };
    const auto bow = gameplay_bow_phase(sample);
    if (bow != BowPhase::drawing) {
        body_.cancel(EffectId::bow_draw);
    }
    if (bow != BowPhase::attached) {
        body_.cancel(EffectId::bow_attached);
    }
    auto bow_effect = EffectId::count;
    if (bow == BowPhase::drawing) {
        bow_effect = EffectId::bow_draw;
    } else if (bow == BowPhase::attached) {
        bow_effect = EffectId::bow_attached;
    }
    update(held_[0], bow_effect, sample.weapon_id);
    update(held_[1], sample.casting ? EffectId::casting : EffectId::count, sample.weapon_id);
}

void EffectPolicy::apply_triggers(OutputFrame& frame,
    const EffectObservation& sample) const noexcept {
    if (trigger_scale_ <= 0.0F) {
        return;
    }
    if (sample.blocking) {
        frame.left = TriggerIntent{.payload = {.mode = INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK,
                                       .start = sample.shield ? 0.13F : 0.22F,
                                       .end = 0.84F,
                                       .strength = (sample.shield ? 0.42F : 0.30F) * trigger_scale_,
                                       .frequency = 0.0F},
            .ttl_ms = held_trigger_ttl_ms};
    }
    const auto bow = gameplay_bow_phase(sample);
    if (bow != BowPhase::none) {
        const bool attached = bow == BowPhase::attached;
        frame.right = TriggerIntent{.payload = {.mode = INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK,
                                        .start = attached ? 0.34F : 0.18F,
                                        .end = 0.96F,
                                        .strength = (attached ? 0.54F : 0.36F) * trigger_scale_,
                                        .frequency = 0.0F},
            .ttl_ms = held_trigger_ttl_ms};
    }
}

std::optional<inputnext::adapters::LedIntent> EffectPolicy::update_leds(
    const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    if (sample.mode != EffectMode::gameplay) {
        led_policy_.pause(now);
        led_paused_ = true;
        return std::nullopt;
    }
    LedObservation led{};
    led.sampled_at = sample.sampled_at;
    led.subject_id = sample.player_id;
    led.in_combat = sample.in_combat;
    if (sample.sneak_detected) {
        led.stealth = inputnext::adapters::LedStealthSample{.sneaking = true,
            .detection_score = *sample.sneak_detected ? 100 : 0};
    }
    if (sample.health.valid && std::isfinite(sample.health.current) &&
        std::isfinite(sample.health.maximum) && sample.health.maximum > 0.0F) {
        led.health =
            LedHealthSample{.current = sample.health.current, .maximum = sample.health.maximum};
    }
    for (const auto& event : events) {
        if (incoming_contact(event, sample) && event_is_current(event, sample, now) &&
            (!led.incoming_hit_at || event.at > *led.incoming_hit_at)) {
            led.incoming_hit_at = event.at;
        }
    }
    return std::exchange(led_paused_, false) ? led_policy_.resume(led, now)
                                             : led_policy_.update(led, now);
}

OutputFrame EffectPolicy::update(const EffectObservation& sample,
    std::span<const EffectEvent> events,
    MonoTime now) noexcept {
    if (sample.player_id == 0 || !sample.alive || sample.mode == EffectMode::unavailable ||
        sample.mode == EffectMode::loading || !fresh(sample.sampled_at, now)) {
        reset();
        return {};
    }
    if ((epoch_ && *epoch_ != sample.epoch) || (last_now_ && now < *last_now_)) {
        reset();
    }
    epoch_ = sample.epoch;
    last_now_ = now;

    const bool gameplay = sample.mode == EffectMode::gameplay;
    if (previous_mode_ != sample.mode && !gameplay) {
        reset_body();
    }
    previous_mode_ = sample.mode;

    OutputFrame frame{};
    body_.expire(now);
    if (gameplay) {
        update_held(sample, now);
        collect_combat(sample, events, now);
        collect_contacts(sample, events, now);
        collect_magic(sample, events, now);
        collect_movement(sample, events, now);
        collect_footfall(sample);
        collect_condition(sample, now);
        apply_triggers(frame, sample);
    }
    collect_actions(sample, events, now);
    body_.render(frame, held_, now);
    if (leds_enabled_) {
        frame.leds = update_leds(sample, events, now);
    }
    return frame;
}

} // namespace oblivion_adapter
