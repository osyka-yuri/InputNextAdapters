#include <fallout_gamebryo/detail/body_effects.hpp>
#include <fallout_gamebryo/detail/trigger_effects.hpp>
#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/policy.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace fallout_gamebryo {
namespace {
using inputnext::adapters::MonoTime;

[[nodiscard]] bool finite_ratio(const std::optional<float>& ratio) noexcept {
    return ratio && std::isfinite(*ratio) && *ratio >= 0.0F && *ratio <= 1.0F;
}

[[nodiscard]] bool fresh(MonoTime timestamp, MonoTime now, std::chrono::milliseconds age) noexcept {
    return timestamp <= now && now - timestamp <= age;
}

[[nodiscard]] BodyEffect shot_effect(WeaponClass weapon) noexcept {
    switch (weapon) {
    case WeaponClass::pistol:
        return BodyEffect::shot_pistol;
    case WeaponClass::rifle:
        return BodyEffect::shot_rifle;
    case WeaponClass::shotgun:
        return BodyEffect::shot_shotgun;
    case WeaponClass::heavy:
        return BodyEffect::shot_heavy;
    case WeaponClass::energy:
        return BodyEffect::shot_energy;
    case WeaponClass::launcher:
        return BodyEffect::shot_launcher;
    case WeaponClass::none:
    case WeaponClass::unarmed:
    case WeaponClass::melee:
    case WeaponClass::thrown:
    case WeaponClass::generic_firearm:
        return BodyEffect::shot_generic;
    }
    return BodyEffect::shot_generic;
}

[[nodiscard]] bool confirms_damage_to_player(const Event& event, std::uint32_t player) noexcept {
    return event.target == player && event.confirmed_damage &&
           (event.kind == EventKind::explosion ||
               (event.kind == EventKind::hit && event.source != 0U));
}

[[nodiscard]] bool is_blocked_incoming_hit(const Event& event, std::uint32_t player) noexcept {
    return event.kind == EventKind::hit && event.source != 0U && event.source != player &&
           event.target == player && event.hit_outcome == HitOutcome::blocked;
}

[[nodiscard]] bool is_incoming_contact(const Event& event, std::uint32_t player) noexcept {
    return confirms_damage_to_player(event, player) || is_blocked_incoming_hit(event, player);
}

[[nodiscard]] bool is_confirmed_incoming_damage(const Event& event,
    const Observation& observation,
    MonoTime now) noexcept {
    return confirms_damage_to_player(event, observation.player) &&
           event.epoch == observation.epoch && event.frame <= observation.frame &&
           event.mode == observation.mode && event.at <= observation.at &&
           fresh(event.at, now, event_max_age) && std::isfinite(event.strength) &&
           event.strength > 0.0F && event.strength <= 1.5F;
}

} // namespace

Policy::Policy(const Settings& settings) noexcept : settings_(settings), led_policy_(settings.led) {
}

void Policy::clear_feedback() noexcept {
    body_.clear();
    triggers_.clear();
    airborne_apex_.reset();
    resources_.reset();
    critical_health_latched_ = false;
    last_heartbeat_.reset();
    last_gait_cue_.reset();
    gait_progress_.reset();
    reload_progress_.reset();
    led_policy_.reset();
}

void Policy::reset_effects() noexcept {
    clear_feedback();
    recent_action_count_ = 0U;
    previous_observation_.reset();
}

void Policy::reset() noexcept {
    reset_effects();
    body_.reset_voice_ids();
}

bool Policy::prepare_observation(const Observation& observation, MonoTime now) noexcept {
    const bool valid = observation.at <= now && now - observation.at <= snapshot_timeout &&
                       observation.mode != Mode::unavailable && observation.player != 0U &&
                       observation.epoch != 0U && observation.focused && observation.alive;
    if (!valid) {
        reset_effects();
        return false;
    }
    if (previous_observation_) {
        const auto& previous = *previous_observation_;
        const bool context_changed = previous.epoch != observation.epoch ||
                                     previous.player != observation.player ||
                                     observation.frame < previous.frame ||
                                     !fresh(previous.at, observation.at, snapshot_timeout);
        if (context_changed) {
            reset_effects();
        } else if (previous.mode != observation.mode || previous.menu != observation.menu) {
            // Action identities remain valid when a confirmed menu result enters gameplay.
            clear_feedback();
        }
    }
    return true;
}

bool Policy::duplicate_action(const Event& event, MonoTime now) noexcept {
    std::size_t retained{};
    for (std::size_t current{}; current < recent_action_count_; ++current) {
        if (fresh(recent_actions_[current].received_at, now, event_max_age)) {
            recent_actions_[retained++] = recent_actions_[current];
        }
    }
    recent_action_count_ = retained;

    for (std::size_t current{}; current < recent_action_count_; ++current) {
        const auto& previous = recent_actions_[current];
        if (event.action != 0U && previous.identity == event.action) {
            return true;
        }
        if (event.action == 0U && previous.identity == 0U && previous.frame == event.frame &&
            previous.mode == event.mode && previous.menu == event.menu &&
            previous.kind == event.kind && previous.source == event.source &&
            previous.target == event.target && previous.weapon == event.weapon) {
            return true;
        }
    }
    return false;
}

void Policy::remember_action(const Event& event, MonoTime now) noexcept {
    if (recent_action_count_ == recent_actions_.size()) {
        for (std::size_t index{}; index + 1U < recent_action_count_; ++index) {
            recent_actions_[index] = recent_actions_[index + 1U];
        }
        --recent_action_count_;
    }
    recent_actions_[recent_action_count_++] = RecentAction{
        .identity = event.action,
        .received_at = now,
        .frame = event.frame,
        .mode = event.mode,
        .menu = event.menu,
        .kind = event.kind,
        .source = event.source,
        .target = event.target,
        .weapon = event.weapon,
    };
}

void Policy::add_voice(BodyEffect effect,
    MonoTime started_at,
    MonoTime now,
    float strength,
    std::uint64_t action) noexcept {
    body_.add(settings_, effect, started_at, now, strength, action);
}

void Policy::add_cue(BodyEffect effect, MonoTime now) noexcept {
    add_voice(effect, now, now);
}

void Policy::update_resources(const Observation& observation,
    MonoTime now,
    bool confirmed_incoming_damage) noexcept {
    const bool health_valid = observation.health && std::isfinite(observation.health->current) &&
                              std::isfinite(observation.health->maximum) &&
                              observation.health->maximum > 0.0F &&
                              observation.health->current >= 0.0F &&
                              observation.health->current <= observation.health->maximum;
    const ResourceState current{
        .health_ratio =
            health_valid ? observation.health->current / observation.health->maximum : 0.0F,
        .action_points_ratio = finite_ratio(observation.action_points_ratio)
                                   ? observation.action_points_ratio.value_or(0.0F)
                                   : 0.0F,
        .radiation = observation.radiation && std::isfinite(*observation.radiation) &&
                             *observation.radiation >= 0.0F
                         ? std::clamp(*observation.radiation, 0.0F, 1.0F)
                         : 0.0F,
        .radiation_sources = observation.radiation_sources.value_or(0U),
        .health_valid = health_valid,
        .action_points_valid = finite_ratio(observation.action_points_ratio),
        .radiation_valid = observation.radiation && std::isfinite(*observation.radiation) &&
                           *observation.radiation >= 0.0F,
    };
    if (resources_) {
        update_resource_edges(*resources_, current, now, confirmed_incoming_damage);
    }
    critical_health_latched_ =
        health_valid &&
        (current.health_ratio < settings_.critical_enter ||
            (critical_health_latched_ && current.health_ratio < settings_.critical_exit));
    resources_ = current;

    if (!observation.in_combat || !critical_health_latched_) {
        last_heartbeat_.reset();
    } else if (confirmed_incoming_damage) {
        last_heartbeat_ = now;
    } else if (settings_.resource_cues &&
               (!last_heartbeat_ || now - *last_heartbeat_ >= std::chrono::milliseconds{900})) {
        add_cue(BodyEffect::critical_health, now);
        last_heartbeat_ = now;
    }
}

void Policy::update_resource_edges(const ResourceState& previous,
    const ResourceState& current,
    MonoTime now,
    bool confirmed_incoming_damage) noexcept {
    const bool health_warning =
        settings_.resource_cues && current.health_valid && previous.health_valid &&
        previous.health_ratio > settings_.health_warning &&
        current.health_ratio <= settings_.health_warning && !confirmed_incoming_damage;
    if (health_warning) {
        add_cue(BodyEffect::health_warning, now);
    }
    const bool ap_warning = settings_.resource_cues && current.action_points_valid &&
                            previous.action_points_valid &&
                            previous.action_points_ratio > settings_.action_point_warning &&
                            current.action_points_ratio <= settings_.action_point_warning;
    if (ap_warning) {
        add_cue(BodyEffect::ap_warning, now);
    }
    const bool radiation_warning = settings_.resource_cues && current.radiation_valid &&
                                   previous.radiation_valid &&
                                   (current.radiation_sources > previous.radiation_sources ||
                                       current.radiation > previous.radiation + 0.01F) &&
                                   current.radiation >= settings_.radiation_warning;
    if (radiation_warning) {
        add_cue(BodyEffect::radiation, now);
    }
}

void Policy::update_landing(const Observation& observation, MonoTime now) noexcept {
    if (!observation.movement || observation.mode != Mode::gameplay ||
        !std::isfinite(observation.movement->height)) {
        airborne_apex_.reset();
        return;
    }
    const auto& current = *observation.movement;
    const bool follows_airborne = previous_observation_ && previous_observation_->movement &&
                                  previous_observation_->movement->airborne &&
                                  previous_observation_->movement->cell == current.cell;
    if (current.airborne) {
        airborne_apex_ = follows_airborne && airborne_apex_
                             ? std::max(*airborne_apex_, current.height)
                             : current.height;
        return;
    }
    if (follows_airborne && airborne_apex_) {
        const float drop = *airborne_apex_ - current.height;
        if (drop >= 48.0F) {
            const float strength = std::clamp(drop / 300.0F, 0.4F, 1.0F);
            add_voice(BodyEffect::landing, now, now, strength);
        }
    }
    airborne_apex_.reset();
}

void Policy::update_reload(const Observation& observation, MonoTime now) noexcept {
    if (observation.mode != Mode::gameplay || observation.menu != Menu::none ||
        !observation.weapon.drawn || observation.weapon.id == 0U || !previous_observation_) {
        reload_progress_.reset();
        return;
    }

    const auto& previous = *previous_observation_;
    if (previous.mode != Mode::gameplay || previous.menu != Menu::none || !previous.weapon.drawn ||
        previous.weapon.id != observation.weapon.id) {
        reload_progress_.reset();
        return;
    }

    if (!previous.reloading) {
        if (!observation.reloading) {
            return;
        }
        reload_progress_ = ReloadProgress{.initial_ammo = previous.weapon.loaded_ammo};
        add_voice(BodyEffect::reload_start, observation.at, now);
    }
    if (!reload_progress_) {
        return;
    }

    auto& progress = *reload_progress_;
    if (const auto loaded = observation.weapon.loaded_ammo) {
        if (!progress.initial_ammo) {
            progress.initial_ammo = loaded;
        } else if (*loaded > *progress.initial_ammo) {
            progress.magazine_increased = true;
        }
    }
    if (!observation.reloading) {
        if (progress.magazine_increased) {
            add_voice(BodyEffect::reload_finish, observation.at, now);
        }
        reload_progress_.reset();
    }
}

bool Policy::GaitProgress::Velocity::follows(const Velocity& previous) const noexcept {
    constexpr double maximum_speed_ratio = 3.0;
    constexpr double minimum_direction_cosine = 0.5; // 60 degrees
    const double dot = (x * previous.x) + (y * previous.y);
    return std::max(speed, previous.speed) <=
               maximum_speed_ratio * std::min(speed, previous.speed) &&
           dot >= (minimum_direction_cosine * speed * previous.speed);
}

std::optional<Policy::GaitProgress::Velocity>
Policy::GaitProgress::velocity_to(const Movement& next, MonoTime sampled_at) const noexcept {
    const auto elapsed = sampled_at - at;
    if (next.cell != movement.cell || next.gait != movement.gait ||
        elapsed <= MonoTime::duration::zero() || elapsed > snapshot_timeout) {
        return std::nullopt;
    }
    const double seconds = std::chrono::duration<double>{elapsed}.count();
    const double velocity_x =
        (static_cast<double>(next.x) - static_cast<double>(movement.x)) / seconds;
    const double velocity_y =
        (static_cast<double>(next.y) - static_cast<double>(movement.y)) / seconds;
    const double speed = std::hypot(velocity_x, velocity_y);
    constexpr double minimum_speed = 4.0;
    // Allow three times the default run speed from FalloutNV.esm for speed bonuses.
    constexpr double maximum_speed = 77.0 * 4.0 * 3.0;
    if (speed >= minimum_speed && speed <= maximum_speed) {
        return Velocity{.x = velocity_x, .y = velocity_y, .speed = speed};
    }
    return std::nullopt;
}

void Policy::update_gait(const Observation& observation, MonoTime now) noexcept {
    if (gait_progress_ && observation.frame <= gait_progress_->frame) {
        return;
    }
    const auto& movement = observation.movement;
    if (!settings_.gait_cues || !movement || observation.in_combat ||
        observation.mode != Mode::gameplay || movement->gait == Gait::none ||
        movement->cell == 0U || movement->airborne) {
        gait_progress_.reset();
        return;
    }

    GaitProgress current{.movement = *movement, .at = observation.at, .frame = observation.frame};
    bool confirmed = false;
    if (gait_progress_) {
        const auto& previous = *gait_progress_;
        current.previous_velocity = previous.velocity_to(*movement, observation.at);
        current.matches_previous = current.previous_velocity && previous.previous_velocity &&
                                   current.previous_velocity->follows(*previous.previous_velocity);
        confirmed = current.matches_previous && previous.matches_previous;
    }
    gait_progress_ = current;
    constexpr auto minimum_cue_interval = std::chrono::milliseconds{620};
    if (!confirmed || (last_gait_cue_ && now - *last_gait_cue_ < minimum_cue_interval)) {
        return;
    }

    auto effect = BodyEffect::ordinary_step;
    if (movement->gait == Gait::sneak) {
        effect = BodyEffect::sneak_step;
    } else if (movement->gait == Gait::run) {
        effect = BodyEffect::run_step;
    }
    add_cue(effect, now);
    last_gait_cue_ = now;
}

void Policy::update_reaction(const Observation& observation, MonoTime now) noexcept {
    constexpr auto active_mode = [](Mode mode) {
        return mode == Mode::gameplay || mode == Mode::vats_playback;
    };
    if (!active_mode(observation.mode) || !observation.reaction || !previous_observation_ ||
        !active_mode(previous_observation_->mode) || !previous_observation_->reaction ||
        observation.frame <= previous_observation_->frame ||
        !fresh(previous_observation_->at, observation.at, snapshot_timeout) ||
        !fresh(observation.at, now, snapshot_timeout)) {
        return;
    }

    const auto before = *previous_observation_->reaction;
    const auto after = *observation.reaction;
    if (after == PlayerReaction::staggered && before == PlayerReaction::steady) {
        add_voice(BodyEffect::stagger, observation.at, now);
    } else if (after == PlayerReaction::knocked_down &&
               (before == PlayerReaction::steady || before == PlayerReaction::staggered)) {
        add_voice(BodyEffect::knockdown, observation.at, now);
    }
}

void Policy::apply_menu_event(const Event& event, MonoTime now) noexcept {
    switch (event.kind) {
    case EventKind::aid:
        add_voice(BodyEffect::aid, event.at, now, event.strength, event.action);
        break;
    case EventKind::equip:
        add_voice(BodyEffect::equip, event.at, now, event.strength, event.action);
        break;
    case EventKind::repair:
        add_voice(BodyEffect::repair, event.at, now, event.strength, event.action);
        break;
    case EventKind::craft:
        add_voice(BodyEffect::craft, event.at, now, event.strength, event.action);
        break;
    case EventKind::pick_lock:
        add_voice(BodyEffect::pick_lock, event.at, now, event.strength, event.action);
        break;
    case EventKind::break_lockpick:
        add_voice(BodyEffect::break_lockpick, event.at, now, event.strength, event.action);
        break;
    case EventKind::terminal_success:
        add_voice(BodyEffect::terminal_success, event.at, now, event.strength, event.action);
        break;
    case EventKind::terminal_failure:
        add_voice(BodyEffect::terminal_failure, event.at, now, event.strength, event.action);
        break;
    default:
        break;
    }
}

void Policy::apply_hit_event(const Event& event,
    const Observation& observation,
    MonoTime now) noexcept {
    const bool incoming = event.target == observation.player;
    auto effect = incoming ? BodyEffect::hit_taken : BodyEffect::hit_dealt;
    if (incoming) {
        if (event.hit_outcome == HitOutcome::blocked) {
            effect = BodyEffect::block_impact;
        } else if (event.hit_outcome == HitOutcome::crippled) {
            effect = BodyEffect::crippled_hit;
        }
    } else if (event.hit_outcome == HitOutcome::sneak_critical) {
        effect = BodyEffect::sneak_critical;
    } else if (event.hit_outcome == HitOutcome::critical) {
        effect = BodyEffect::critical_hit;
    } else if (event.weapon_class == WeaponClass::unarmed ||
               event.weapon_class == WeaponClass::melee) {
        effect = BodyEffect::melee_hit;
    }
    const float strength = std::min(event.strength, 1.5F);
    if (event.target != 0U) {
        add_voice(effect, event.at, now, strength, event.action);
    }
}

void Policy::apply_shot_event(const Event& event,
    const Observation& observation,
    MonoTime now) noexcept {
    auto weapon = event.weapon_class;
    if (weapon == WeaponClass::none) {
        weapon = observation.weapon.kind;
    }
    if (event.weapon != 0U && observation.weapon.id != 0U &&
        event.weapon != observation.weapon.id) {
        return;
    }
    const float automatic_scale = event.automatic ? 0.84F : 1.0F;
    const float shot_strength = std::clamp(event.strength * automatic_scale, 0.0F, 1.5F);
    add_voice(shot_effect(weapon), event.at, now, shot_strength, event.action);

    triggers_.on_shot(observation, event, weapon);
}

void Policy::apply_combat_event(const Event& event,
    const Observation& observation,
    MonoTime now) noexcept {
    switch (event.kind) {
    case EventKind::shot:
        apply_shot_event(event, observation, now);
        return;
    case EventKind::hit:
        if (event.confirmed_damage || is_blocked_incoming_hit(event, observation.player)) {
            apply_hit_event(event, observation, now);
        }
        return;
    case EventKind::explosion:
        if (event.target == observation.player && event.confirmed_damage) {
            add_voice(BodyEffect::explosion, event.at, now, event.strength, event.action);
        }
        return;
    case EventKind::explosion_dealt:
        if (event.confirmed_damage && event.source == observation.player && event.target != 0U &&
            event.target != observation.player) {
            add_voice(BodyEffect::explosion_dealt, event.at, now, event.strength, event.action);
        }
        return;
    case EventKind::dry_fire:
        add_voice(BodyEffect::dry_fire, event.at, now, event.strength, event.action);
        return;
    case EventKind::melee_swing:
        add_voice(event.weapon_class == WeaponClass::unarmed ? BodyEffect::unarmed_swing
                                                             : BodyEffect::melee_swing,
            event.at,
            now,
            event.strength,
            event.action);
        return;
    case EventKind::thrown:
        add_voice(BodyEffect::throwing, event.at, now, event.strength, event.action);
        return;
    case EventKind::vats_select:
    case EventKind::vats_accept:
    case EventKind::aid:
    case EventKind::equip:
    case EventKind::repair:
    case EventKind::craft:
    case EventKind::pick_lock:
    case EventKind::break_lockpick:
    case EventKind::terminal_success:
    case EventKind::terminal_failure:
        return;
    case EventKind::progress:
        add_voice(BodyEffect::progress, event.at, now, event.strength, event.action);
        return;
    }
}

void Policy::apply_event(const Event& event,
    const Observation& observation,
    MonoTime now) noexcept {
    const bool incoming_damage = confirms_damage_to_player(event, observation.player);
    const bool incoming_contact = is_incoming_contact(event, observation.player);
    const bool valid = fresh(event.at, now, event_max_age) && event.epoch == observation.epoch &&
                       event.frame <= observation.frame && event.at <= observation.at &&
                       (event.source == observation.player || incoming_contact) &&
                       std::isfinite(event.strength) && event.strength > 0.0F &&
                       event.strength <= 1.5F;
    if (!valid) {
        return;
    }

    const bool menu_event = settings_.menu_effects && settings_.ui_cues &&
                            menu_allows(event.menu, event.kind) && event.mode == Mode::menu &&
                            ((observation.mode == Mode::menu && event.menu == observation.menu) ||
                                observation.mode == Mode::gameplay);
    const bool vats_select = event.kind == EventKind::vats_select &&
                             event.mode == Mode::vats_selection &&
                             observation.mode == Mode::vats_selection;
    const bool vats_accept =
        event.kind == EventKind::vats_accept &&
        ((event.mode == observation.mode && observation.mode == Mode::vats_selection) ||
            (event.mode == Mode::vats_selection && observation.mode == Mode::vats_playback));
    const bool combat_mode = observation.mode == Mode::gameplay ||
                             observation.mode == Mode::vats_playback ||
                             (observation.mode == Mode::vats_selection && incoming_damage);
    const bool combat_event = combat_mode && event.mode == observation.mode;
    if (!menu_event && !vats_select && !vats_accept && !combat_event) {
        return;
    }
    if (duplicate_action(event, now)) {
        return;
    }
    remember_action(event, now);
    if (menu_event) {
        apply_menu_event(event, now);
    } else if (vats_select) {
        add_voice(BodyEffect::vats_select, event.at, now, event.strength, event.action);
    } else if (vats_accept) {
        add_voice(BodyEffect::vats_accept, event.at, now, event.strength, event.action);
    } else {
        apply_combat_event(event, observation, now);
    }
}

inputnext::adapters::OutputFrame Policy::update(const Observation& observation,
    std::span<const Event> events,
    MonoTime now) noexcept {
    inputnext::adapters::OutputFrame frame{};
    if (events.size() > event_capacity) {
        reset_effects();
        return frame;
    }
    if (!settings_.enabled || !prepare_observation(observation, now)) {
        return frame;
    }

    const bool gameplay = observation.mode == Mode::gameplay ||
                          observation.mode == Mode::vats_selection ||
                          observation.mode == Mode::vats_playback;
    std::optional<MonoTime> incoming_hit_at;
    if (gameplay) {
        for (const auto& event : events) {
            if (is_confirmed_incoming_damage(event, observation, now) &&
                (!incoming_hit_at || event.at > *incoming_hit_at)) {
                incoming_hit_at = event.at;
            }
        }
        update_resources(observation, now, incoming_hit_at.has_value());
        update_landing(observation, now);
        update_reload(observation, now);
        update_gait(observation, now);
        update_reaction(observation, now);
    }

    for (const auto& event : events) {
        apply_event(event, observation, now);
    }

    if (gameplay) {
        const inputnext::adapters::LedObservation led{.sampled_at = observation.at,
            .subject_id = observation.player,
            .health = observation.health,
            .stealth = observation.stealth,
            .radiation_sources = observation.radiation_sources,
            .in_combat = observation.in_combat,
            .incoming_hit_at = incoming_hit_at,
            .confirmed_damage_at = incoming_hit_at};
        frame.leds = led_policy_.update(led, now);
    } else {
        led_policy_.pause(now);
    }
    triggers_.write(frame, observation, settings_, now);
    body_.write(frame, now);
    previous_observation_ = observation;
    return frame;
}
} // namespace fallout_gamebryo
