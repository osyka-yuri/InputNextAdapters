#include "event_reducer.hpp"

#include <fallout_gamebryo/model.hpp>
#include <inputnext_adapters/body_program.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

namespace fallout_new_vegas {
namespace {
using fallout_gamebryo::Event;
using fallout_gamebryo::EventKind;
using fallout_gamebryo::HitOutcome;
using fallout_gamebryo::Menu;
using fallout_gamebryo::Mode;
using fallout_gamebryo::MonoTime;
using fallout_gamebryo::WeaponClass;

constexpr auto correlation_window = std::chrono::milliseconds{150};

// Match the full stat code from ShowOff 1.84 internal/decoding.h.
// NOLINTNEXTLINE(performance-enum-size)
enum class MiscStat : std::uint32_t {
    quests_completed = 0,
    locations_discovered = 1,
    locks_picked = 4,
    stimpaks_taken = 6,
    rad_x_taken = 7,
    rad_away_taken = 8,
    chems_taken = 9,
    doctor_bags_used = 26,
    challenges_completed = 27,
    items_crafted = 32,
    items_repaired = 34,
};

bool combat_mode(Mode mode) noexcept {
    return mode == Mode::gameplay || mode == Mode::vats_playback;
}

bool expired(MonoTime sampled_at, MonoTime now) noexcept {
    return sampled_at > now || sampled_at < now - correlation_window;
}

bool same_discharge(const NativeSignal& fire, const NativeSignal& projectile) noexcept {
    return fire.source == projectile.source && fire.form == projectile.form &&
           fire.mode == projectile.mode &&
           (fire.frame > projectile.frame ? fire.frame - projectile.frame
                                          : projectile.frame - fire.frame) <= 1U;
}

std::optional<EventKind> stat_action(const NativeSignal& signal) noexcept {
    if (signal.delta <= 0) {
        return std::nullopt;
    }
    const auto menu = signal.mode == Mode::menu ? signal.menu : Menu::none;
    switch (static_cast<MiscStat>(signal.detail)) {
    case MiscStat::locks_picked:
        return menu == Menu::lockpick ? std::optional{EventKind::pick_lock} : std::nullopt;
    case MiscStat::stimpaks_taken:
    case MiscStat::rad_x_taken:
    case MiscStat::rad_away_taken:
    case MiscStat::chems_taken:
    case MiscStat::doctor_bags_used:
        return menu == Menu::inventory ? std::optional{EventKind::aid} : std::nullopt;
    case MiscStat::items_crafted:
        return menu == Menu::crafting ? std::optional{EventKind::craft} : std::nullopt;
    case MiscStat::items_repaired:
        return menu == Menu::repair ? std::optional{EventKind::repair} : std::nullopt;
    case MiscStat::quests_completed:
    case MiscStat::locations_discovered:
    case MiscStat::challenges_completed:
        return signal.mode == Mode::gameplay ? std::optional{EventKind::progress} : std::nullopt;
    default:
        return std::nullopt;
    }
}
} // namespace

void SignalTranslator::reset(std::uint64_t epoch) noexcept {
    epoch_ = epoch;
    next_action_ = 1;
    cancel_pending_shots();
    recent_projectiles_ = {};
    recent_hits_ = {};
    recent_player_blasts_ = {};
    next_projectile_identity_ = 0;
    next_hit_identity_ = 0;
    next_blast_identity_ = 0;
}

void SignalTranslator::advance(MonoTime now) noexcept {
    expire(now);
}

void SignalTranslator::cancel_pending_shots() noexcept {
    pending_fires_ = {};
    pending_projectiles_ = {};
}

void SignalTranslator::expire(MonoTime now) noexcept {
    for (auto& fire : pending_fires_) {
        if (fire && expired(fire->signal.at, now)) {
            fire.reset();
        }
    }
    for (auto& projectile : pending_projectiles_) {
        if (projectile && expired(projectile->at, now)) {
            projectile.reset();
        }
    }
}

Event SignalTranslator::make_event(const NativeSignal& signal, EventKind kind) noexcept {
    return {.at = signal.at,
        .epoch = signal.epoch,
        .frame = signal.frame,
        .action = next_action_++,
        .mode = signal.mode,
        .menu = signal.menu,
        .kind = kind,
        .source = signal.source,
        .target = signal.target,
        .weapon = signal.form,
        .weapon_class = signal.weapon_class,
        .hit_outcome = signal.hit_outcome,
        .automatic = signal.automatic,
        .confirmed_damage = signal.confirmed_damage};
}

std::optional<Event> SignalTranslator::make_menu_event(const NativeSignal& signal,
    EventKind kind,
    std::uint32_t reference) noexcept {
    if (reference == 0U || signal.mode != Mode::menu || !menu_allows(signal.menu, kind)) {
        return std::nullopt;
    }
    return make_event(signal, kind);
}

std::optional<Event> SignalTranslator::match_fire(const NativeSignal& fire) noexcept {
    const auto projectile_count =
        std::max(1U, static_cast<unsigned int>(fire.projectiles_per_shot));
    auto remaining = projectile_count;
    for (auto& projectile : pending_projectiles_) {
        if (projectile && same_discharge(fire, *projectile)) {
            projectile.reset();
            if (--remaining == 0U) {
                break;
            }
        }
    }
    const bool paired = remaining < projectile_count;
    if (remaining != 0U) {
        const auto free =
            std::ranges::find_if(pending_fires_, [](const auto& slot) { return !slot; });
        if (free != pending_fires_.end()) {
            *free = PendingFire{.signal = fire, .remaining = remaining, .emitted = paired};
        }
    }
    return paired ? std::optional{make_event(fire, EventKind::shot)} : std::nullopt;
}

bool SignalTranslator::repeated_projectile(const NativeSignal& signal) noexcept {
    if (signal.projectile == 0U) {
        return false;
    }
    if (std::ranges::any_of(recent_projectiles_, [&](const ProjectileIdentity& previous) {
            return previous.reference == signal.projectile && !expired(previous.at, signal.at);
        })) {
        return true;
    }
    recent_projectiles_[next_projectile_identity_] = {.at = signal.at,
        .reference = signal.projectile};
    next_projectile_identity_ = (next_projectile_identity_ + 1U) % recent_projectiles_.size();
    return false;
}

std::optional<Event> SignalTranslator::match_projectile(const NativeSignal& projectile) noexcept {
    if (repeated_projectile(projectile)) {
        return std::nullopt;
    }
    auto* oldest = static_cast<std::optional<PendingFire>*>(nullptr);
    for (auto& candidate : pending_fires_) {
        if (candidate && same_discharge(candidate->signal, projectile) &&
            (oldest == nullptr || !*oldest || candidate->signal.at < (*oldest)->signal.at)) {
            oldest = &candidate;
        }
    }
    if (oldest != nullptr && *oldest) {
        auto& fire = **oldest;
        const auto event =
            fire.emitted ? std::nullopt : std::optional{make_event(fire.signal, EventKind::shot)};
        fire.emitted = true;
        if (--fire.remaining == 0U) {
            oldest->reset();
        }
        return event;
    }
    const auto free =
        std::ranges::find_if(pending_projectiles_, [](const auto& slot) { return !slot; });
    if (free != pending_projectiles_.end()) {
        *free = projectile;
    }
    return std::nullopt;
}

bool SignalTranslator::repeated_hit(const NativeSignal& signal) noexcept {
    if (signal.hit_reference == 0U) {
        return false;
    }
    if (std::ranges::any_of(recent_hits_, [&](const HitIdentity& previous) {
            return previous.reference == signal.hit_reference && previous.source == signal.source &&
                   previous.target == signal.target && previous.frame == signal.frame &&
                   previous.explosion == signal.explosion_damage;
        })) {
        return true;
    }
    recent_hits_[next_hit_identity_] = {.reference = signal.hit_reference,
        .source = signal.source,
        .target = signal.target,
        .frame = signal.frame,
        .explosion = signal.explosion_damage};
    next_hit_identity_ = (next_hit_identity_ + 1U) % recent_hits_.size();
    return false;
}

bool SignalTranslator::repeated_player_blast(const NativeSignal& signal) noexcept {
    if (std::ranges::any_of(recent_player_blasts_, [&](const BlastIdentity& previous) {
            return previous.reference == signal.hit_reference && !expired(previous.at, signal.at);
        })) {
        return true;
    }
    recent_player_blasts_[next_blast_identity_] = {.at = signal.at,
        .reference = signal.hit_reference};
    next_blast_identity_ = (next_blast_identity_ + 1U) % recent_player_blasts_.size();
    return false;
}

std::optional<Event> SignalTranslator::translate(const NativeSignal& signal,
    std::uint32_t player) noexcept {
    if (player == 0U || signal.epoch != epoch_) {
        return std::nullopt;
    }
    if (signal.kind == NativeSignalKind::hit_received) {
        return translate_hit(signal, player);
    }
    return signal.source == player ? translate_player_action(signal) : std::nullopt;
}

std::optional<Event> SignalTranslator::translate_hit(const NativeSignal& signal,
    std::uint32_t player) noexcept {
    const bool target_is_player = signal.target == player;
    const bool incoming = target_is_player && (signal.source != player || signal.explosion_damage);
    const bool outgoing = signal.source == player && signal.target != 0U && signal.target != player;
    const bool confirmed_block =
        incoming && !signal.explosion_damage && signal.hit_outcome == HitOutcome::blocked;
    if ((!signal.confirmed_damage && !confirmed_block) || !combat_mode(signal.mode) ||
        (!incoming && !outgoing) || repeated_hit(signal)) {
        return std::nullopt;
    }
    if (!signal.explosion_damage) {
        return make_event(signal, EventKind::hit);
    }
    if (incoming) {
        return make_event(signal, EventKind::explosion);
    }
    if (signal.hit_reference == 0U) {
        return make_event(signal, EventKind::hit);
    }
    return repeated_player_blast(signal)
               ? std::nullopt
               : std::optional{make_event(signal, EventKind::explosion_dealt)};
}

std::optional<Event> SignalTranslator::translate_player_action(
    const NativeSignal& signal) noexcept {
    const bool can_attack = combat_mode(signal.mode);
    switch (signal.kind) {
    case NativeSignalKind::fire_weapon:
    case NativeSignalKind::projectile_create:
        return translate_discharge(signal);

    case NativeSignalKind::dry_fire:
        if (signal.mode == Mode::gameplay && signal.form != 0U &&
            fallout_gamebryo::is_firearm(signal.weapon_class)) {
            return make_event(signal, EventKind::dry_fire);
        }
        break;
    case NativeSignalKind::melee_swing:
        if (can_attack && (signal.weapon_class == WeaponClass::unarmed ||
                              signal.weapon_class == WeaponClass::melee)) {
            return make_event(signal, EventKind::melee_swing);
        }
        break;
    case NativeSignalKind::thrown:
        if (can_attack && signal.weapon_class == WeaponClass::thrown) {
            return make_event(signal, EventKind::thrown);
        }
        break;
    case NativeSignalKind::misc_stat_change:
        if (const auto action = stat_action(signal)) {
            return make_event(signal, *action);
        }
        break;
    case NativeSignalKind::item_equipped:
        return make_menu_event(signal, EventKind::equip, signal.form);
    case NativeSignalKind::lockpick_broken:
        return make_menu_event(signal, EventKind::break_lockpick, signal.target);
    case NativeSignalKind::terminal_hacked:
        return make_menu_event(signal, EventKind::terminal_success, signal.target);
    case NativeSignalKind::terminal_hack_failed:
        return make_menu_event(signal, EventKind::terminal_failure, signal.target);
    case NativeSignalKind::vats_select:
        if (signal.mode == Mode::vats_selection) {
            return make_event(signal, EventKind::vats_select);
        }
        break;
    case NativeSignalKind::vats_accept:
        if (signal.mode == Mode::vats_playback) {
            auto event = make_event(signal, EventKind::vats_accept);
            event.mode = Mode::vats_selection;
            return event;
        }
        break;
    case NativeSignalKind::hit_received:
        break;
    }
    return std::nullopt;
}

std::optional<Event> SignalTranslator::translate_discharge(const NativeSignal& signal) noexcept {
    const bool firing = signal.kind == NativeSignalKind::fire_weapon;
    if (!combat_mode(signal.mode) || signal.form == 0U ||
        (firing && !fallout_gamebryo::is_firearm(signal.weapon_class))) {
        return std::nullopt;
    }
    expire(signal.at);
    return firing ? match_fire(signal) : match_projectile(signal);
}
} // namespace fallout_new_vegas
