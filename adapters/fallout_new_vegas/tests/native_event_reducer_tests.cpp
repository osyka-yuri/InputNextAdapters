#include "../src/event_reducer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fallout_gamebryo/model.hpp>
#include <inputnext_adapters/body_program.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace {
using namespace std::chrono_literals;
using fallout_gamebryo::Event;
using fallout_gamebryo::EventKind;
using fallout_gamebryo::HitOutcome;
using fallout_gamebryo::Menu;
using fallout_gamebryo::Mode;
using fallout_gamebryo::MonoTime;
using fallout_gamebryo::WeaponClass;
using fallout_new_vegas::NativeSignal;
using fallout_new_vegas::NativeSignalKind;
using fallout_new_vegas::SignalTranslator;

constexpr std::uint32_t player = 0x14;
constexpr std::uint32_t npc = 0x201;
constexpr std::uint32_t rifle = 0x14A;

[[nodiscard]] NativeSignal fire(MonoTime captured_at = MonoTime{}, std::uint64_t frame = 40) {
    return NativeSignal{.at = captured_at,
        .epoch = 3,
        .frame = frame,
        .mode = Mode::gameplay,
        .kind = NativeSignalKind::fire_weapon,
        .source = player,
        .form = rifle,
        .weapon_class = WeaponClass::rifle};
}

[[nodiscard]] NativeSignal projectile(MonoTime captured_at = MonoTime{}, std::uint64_t frame = 40) {
    auto signal = fire(captured_at, frame);
    signal.kind = NativeSignalKind::projectile_create;
    signal.weapon_class = WeaponClass::none;
    return signal;
}

Event require_event(const std::optional<Event>& event) {
    if (!event) {
        throw std::runtime_error{"Expected a confirmed game event"};
    }
    return *event;
}

TEST_CASE("A player shot requires paired fire and projectile signals", "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(3);
    const auto shot_signal = fire(MonoTime{} + 10ms);

    CHECK_FALSE(translator.translate(shot_signal, player));
    const auto shot = require_event(translator.translate(projectile(MonoTime{} + 12ms), player));
    CHECK(shot.kind == EventKind::shot);
    CHECK(shot.source == player);
    CHECK(shot.weapon == rifle);
    CHECK(shot.weapon_class == WeaponClass::rifle);
    CHECK(shot.at == shot_signal.at);
}

TEST_CASE("Projectile-before-fire order and pellets still yield one shot", "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(3);
    CHECK_FALSE(translator.translate(projectile(MonoTime{} + 20ms), player));
    auto discharge = fire(MonoTime{} + 21ms);
    discharge.projectiles_per_shot = 13;
    discharge.weapon_class = WeaponClass::shotgun;
    const auto shot = require_event(translator.translate(discharge, player));
    CHECK(shot.kind == EventKind::shot);

    for (int pellet = 0; pellet < 12; ++pellet) {
        CHECK_FALSE(translator.translate(projectile(MonoTime{} + 22ms + 1ms * pellet), player));
    }
    discharge.at += 20ms;
    CHECK_FALSE(translator.translate(discharge, player));
}

TEST_CASE("Distinct discharges in one frame each consume one projectile", "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(3);
    const auto first_fire = fire(MonoTime{} + 10ms);
    const auto second_fire = fire(MonoTime{} + 11ms);
    CHECK_FALSE(translator.translate(first_fire, player));
    CHECK_FALSE(translator.translate(second_fire, player));

    auto created = projectile(MonoTime{} + 12ms);
    created.projectile = 0xFF001234;
    const auto first_shot = require_event(translator.translate(created, player));
    CHECK(first_shot.at == first_fire.at);
    CHECK_FALSE(translator.translate(created, player));

    ++created.projectile;
    created.at += 1ms;
    const auto second_shot = require_event(translator.translate(created, player));
    CHECK(second_shot.at == second_fire.at);
    CHECK(second_shot.action != first_shot.action);
}

TEST_CASE("NPC shots and projectiles never become player feedback", "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(3);
    auto npc_fire = fire(MonoTime{} + 10ms);
    npc_fire.source = npc;
    auto npc_projectile = projectile(MonoTime{} + 11ms);
    npc_projectile.source = npc;
    CHECK_FALSE(translator.translate(npc_fire, player));
    CHECK_FALSE(translator.translate(npc_projectile, player));
    CHECK_FALSE(translator.translate(fire(MonoTime{} + 12ms), player));
}

TEST_CASE("Unpaired fire signals expire and cannot become late shots", "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(3);
    CHECK_FALSE(translator.translate(fire(MonoTime{} + 1ms), player));
    translator.advance(MonoTime{} + 152ms);
    CHECK_FALSE(translator.translate(projectile(MonoTime{} + 153ms), player));
}

TEST_CASE("A reload transition cancels both sides of an incomplete shot pair",
    "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(3);
    CHECK_FALSE(translator.translate(fire(MonoTime{} + 10ms), player));
    translator.cancel_pending_shots();
    CHECK_FALSE(translator.translate(projectile(MonoTime{} + 11ms), player));

    translator.cancel_pending_shots();
    CHECK_FALSE(translator.translate(projectile(MonoTime{} + 20ms), player));
    translator.cancel_pending_shots();
    CHECK_FALSE(translator.translate(fire(MonoTime{} + 21ms), player));
}

TEST_CASE("A dry-fire signal requires the player's firearm in gameplay", "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(3);
    auto signal = fire(MonoTime{} + 10ms);
    signal.kind = NativeSignalKind::dry_fire;
    const auto click = require_event(translator.translate(signal, player));
    CHECK(click.kind == EventKind::dry_fire);
    CHECK(click.weapon == rifle);
    CHECK(click.at == signal.at);

    SECTION("another actor") {
        signal.source = npc;
    }
    SECTION("a menu") {
        signal.mode = Mode::menu;
    }
    SECTION("VATS playback") {
        signal.mode = Mode::vats_playback;
    }
    SECTION("an absent weapon") {
        signal.form = 0U;
    }
    SECTION("a thrown weapon") {
        signal.weapon_class = WeaponClass::thrown;
    }
    CHECK_FALSE(translator.translate(signal, player));
}

TEST_CASE("Incoming and outgoing OnHit signals are player-attributed", "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(8);
    NativeSignal incoming{.at = MonoTime{} + 10ms,
        .epoch = 8,
        .frame = 2,
        .mode = Mode::gameplay,
        .kind = NativeSignalKind::hit_received,
        .source = npc,
        .target = player};
    CHECK_FALSE(translator.translate(incoming, player));
    incoming.confirmed_damage = true;
    const auto incoming_hit = require_event(translator.translate(incoming, player));
    CHECK(incoming_hit.kind == EventKind::hit);
    CHECK(incoming_hit.target == player);
    CHECK(incoming_hit.confirmed_damage);

    auto outgoing = incoming;
    outgoing.frame = 3;
    outgoing.source = player;
    outgoing.target = npc;
    outgoing.weapon_class = WeaponClass::rifle;
    outgoing.confirmed_damage = false;
    CHECK_FALSE(translator.translate(outgoing, player));
    outgoing.confirmed_damage = true;
    const auto outgoing_hit = require_event(translator.translate(outgoing, player));
    CHECK(outgoing_hit.source == player);
    CHECK(outgoing_hit.target == npc);
    CHECK(outgoing_hit.weapon_class == WeaponClass::rifle);
    CHECK(outgoing_hit.confirmed_damage);
}

TEST_CASE("Only a repeated identified impact is collapsed within a game frame",
    "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(8);
    const NativeSignal hit{.at = MonoTime{} + 10ms,
        .epoch = 8,
        .frame = 4,
        .mode = Mode::gameplay,
        .kind = NativeSignalKind::hit_received,
        .source = player,
        .target = npc,
        .hit_reference = 0x404U,
        .confirmed_damage = true};
    const auto first = require_event(translator.translate(hit, player));
    CHECK_FALSE(translator.translate(hit, player));

    auto second = hit;
    ++second.hit_reference;
    CHECK(require_event(translator.translate(second, player)).action != first.action);
    CHECK_FALSE(translator.translate(hit, player));

    auto unidentified = hit;
    unidentified.hit_reference = 0U;
    const auto first_unidentified = require_event(translator.translate(unidentified, player));
    CHECK(require_event(translator.translate(unidentified, player)).action !=
          first_unidentified.action);

    auto next_game_frame = hit;
    ++next_game_frame.frame;
    next_game_frame.at += 16ms;
    CHECK(translator.translate(next_game_frame, player));
}

TEST_CASE("Confirmed incoming combat outcomes preserve their hit facts", "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(3);
    const NativeSignal block{.epoch = 3,
        .frame = 40,
        .mode = Mode::gameplay,
        .kind = NativeSignalKind::hit_received,
        .source = npc,
        .target = player,
        .hit_outcome = HitOutcome::blocked};
    const auto blocked = require_event(translator.translate(block, player));
    CHECK(blocked.kind == EventKind::hit);
    CHECK(blocked.hit_outcome == HitOutcome::blocked);
    CHECK_FALSE(blocked.confirmed_damage);

    auto damage = block;
    damage.hit_outcome = HitOutcome::crippled;
    CHECK_FALSE(translator.translate(damage, player));
    damage.confirmed_damage = true;
    CHECK(require_event(translator.translate(damage, player)).hit_outcome == HitOutcome::crippled);
    damage.source = player;
    damage.target = npc;
    damage.hit_outcome = HitOutcome::critical;
    CHECK(require_event(translator.translate(damage, player)).hit_outcome == HitOutcome::critical);
    damage.hit_outcome = HitOutcome::sneak_critical;
    CHECK(require_event(translator.translate(damage, player)).hit_outcome ==
          HitOutcome::sneak_critical);
}

TEST_CASE("One player blast yields one outgoing cue across multiple targets",
    "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(3);
    NativeSignal blast{.at = MonoTime{} + 10ms,
        .epoch = 3,
        .frame = 40,
        .mode = Mode::gameplay,
        .kind = NativeSignalKind::hit_received,
        .source = player,
        .target = npc,
        .hit_reference = 0x412U,
        .confirmed_damage = true,
        .explosion_damage = true};
    CHECK(require_event(translator.translate(blast, player)).kind == EventKind::explosion_dealt);

    blast.target = npc + 1U;
    ++blast.frame;
    blast.at += 20ms;
    CHECK_FALSE(translator.translate(blast, player));

    auto self_damage = blast;
    self_damage.target = player;
    CHECK(require_event(translator.translate(self_damage, player)).kind == EventKind::explosion);

    ++blast.hit_reference;
    CHECK(require_event(translator.translate(blast, player)).kind == EventKind::explosion_dealt);
    blast.at += 151ms;
    ++blast.frame;
    CHECK(require_event(translator.translate(blast, player)).kind == EventKind::explosion_dealt);

    blast.hit_reference = 0U;
    CHECK(require_event(translator.translate(blast, player)).kind == EventKind::hit);
    blast.source = npc;
    CHECK_FALSE(translator.translate(blast, player));
}

TEST_CASE("Confirmed blast damage distinguishes incoming and outgoing cues",
    "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(3);
    NativeSignal blast{.at = MonoTime{} + 10ms,
        .epoch = 3,
        .frame = 4,
        .mode = Mode::gameplay,
        .kind = NativeSignalKind::hit_received,
        .target = player,
        .hit_reference = 0x405U,
        .explosion_damage = true};
    CHECK_FALSE(translator.translate(blast, player));
    blast.confirmed_damage = true;
    CHECK(require_event(translator.translate(blast, player)).kind == EventKind::explosion);
    CHECK_FALSE(translator.translate(blast, player));

    blast.source = player;
    CHECK(require_event(translator.translate(blast, player)).kind == EventKind::explosion);
    blast.target = npc;
    CHECK(require_event(translator.translate(blast, player)).kind == EventKind::explosion_dealt);
    blast.source = 0x203U;
    CHECK_FALSE(translator.translate(blast, player));
}

TEST_CASE("NPC contacts are ignored and melee swings require melee weapons",
    "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(1);
    NativeSignal hit{.at = MonoTime{} + 1ms,
        .epoch = 1,
        .frame = 2,
        .mode = Mode::gameplay,
        .kind = NativeSignalKind::hit_received,
        .source = npc,
        .target = 0x203};
    CHECK_FALSE(translator.translate(hit, player));

    hit.kind = NativeSignalKind::melee_swing;
    hit.source = player;
    hit.weapon_class = WeaponClass::melee;
    const auto swing = require_event(translator.translate(hit, player));
    CHECK(swing.kind == EventKind::melee_swing);
    hit.frame++;
    hit.weapon_class = WeaponClass::rifle;
    CHECK_FALSE(translator.translate(hit, player));
}

TEST_CASE("Only a positive player lock-count change confirms a lockpick success",
    "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(1);
    NativeSignal change{.at = MonoTime{} + 1ms,
        .epoch = 1,
        .frame = 9,
        .mode = Mode::menu,
        .menu = Menu::lockpick,
        .kind = NativeSignalKind::misc_stat_change,
        .source = player,
        .detail = 4,
        .delta = 1};
    const auto opened = require_event(translator.translate(change, player));
    CHECK(opened.kind == EventKind::pick_lock);
    CHECK(opened.source == player);
    CHECK(opened.menu == Menu::lockpick);

    change.delta = 0;
    CHECK_FALSE(translator.translate(change, player));
    change.delta = -1;
    CHECK_FALSE(translator.translate(change, player));
    change.delta = 1;
    change.source = npc;
    CHECK_FALSE(translator.translate(change, player));
}

TEST_CASE("A confirmed lockpick break belongs to the player's active lock menu",
    "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(1);
    NativeSignal signal{.at = MonoTime{} + 1ms,
        .epoch = 1,
        .frame = 9,
        .mode = Mode::menu,
        .menu = Menu::lockpick,
        .kind = NativeSignalKind::lockpick_broken,
        .source = player,
        .target = 0x78U};
    const auto broken = require_event(translator.translate(signal, player));
    CHECK(broken.kind == EventKind::break_lockpick);
    CHECK(broken.source == player);
    CHECK(broken.target == signal.target);

    SECTION("outside the lock menu") {
        signal.menu = Menu::inventory;
    }
    SECTION("without a lock reference") {
        signal.target = 0U;
    }
    SECTION("for another actor") {
        signal.source = npc;
    }
    CHECK_FALSE(translator.translate(signal, player));
}

TEST_CASE("Terminal outcomes require a player and a terminal reference in the computer menu",
    "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(3);
    NativeSignal signal{.at = MonoTime{} + 20ms,
        .epoch = 3,
        .frame = 40,
        .mode = Mode::menu,
        .menu = Menu::terminal,
        .kind = NativeSignalKind::terminal_hacked,
        .source = player,
        .target = 0x372U};

    CHECK(require_event(translator.translate(signal, player)).kind == EventKind::terminal_success);
    signal.kind = NativeSignalKind::terminal_hack_failed;
    const auto locked = require_event(translator.translate(signal, player));
    CHECK(locked.kind == EventKind::terminal_failure);
    CHECK(locked.at == signal.at);
    CHECK(locked.target == signal.target);

    SECTION("unrelated menus") {
        signal.menu = Menu::inventory;
    }
    SECTION("outside a menu") {
        signal.mode = Mode::gameplay;
    }
    SECTION("another actor") {
        signal.source = npc;
    }
    SECTION("no terminal") {
        signal.target = 0U;
    }
    SECTION("a prior save") {
        --signal.epoch;
    }
    CHECK_FALSE(translator.translate(signal, player));
}

TEST_CASE("Verified miscellaneous stat changes map only to supported cues",
    "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(4);
    NativeSignal change{.at = MonoTime{} + 1ms,
        .epoch = 4,
        .frame = 5,
        .mode = Mode::gameplay,
        .kind = NativeSignalKind::misc_stat_change,
        .source = player,
        .delta = 1};

    change.detail = 8;
    CHECK_FALSE(translator.translate(change, player));
    change.mode = Mode::menu;
    change.menu = Menu::inventory;
    const auto aid = require_event(translator.translate(change, player));
    CHECK(aid.kind == EventKind::aid);
    CHECK(aid.mode == Mode::menu);
    CHECK(aid.menu == Menu::inventory);

    change.detail = 32;
    change.menu = Menu::crafting;
    const auto craft = require_event(translator.translate(change, player));
    CHECK(craft.kind == EventKind::craft);
    CHECK(craft.menu == Menu::crafting);

    change.detail = 34;
    change.menu = Menu::repair;
    const auto repair = require_event(translator.translate(change, player));
    CHECK(repair.kind == EventKind::repair);
    CHECK(repair.menu == Menu::repair);

    change.detail = 27;
    change.mode = Mode::gameplay;
    change.menu = Menu::none;
    const auto progress = require_event(translator.translate(change, player));
    CHECK(progress.kind == EventKind::progress);
    CHECK(progress.mode == Mode::gameplay);

    change.detail = 10;
    CHECK_FALSE(translator.translate(change, player));
}

TEST_CASE("Player equip and typed VATS mode changes use distinct events", "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(4);
    NativeSignal signal{.at = MonoTime{} + 1ms,
        .epoch = 4,
        .frame = 6,
        .mode = Mode::gameplay,
        .kind = NativeSignalKind::item_equipped,
        .source = player,
        .form = rifle};
    CHECK_FALSE(translator.translate(signal, player));
    signal.mode = Mode::menu;
    signal.menu = Menu::inventory;
    const auto equipped = require_event(translator.translate(signal, player));
    CHECK(equipped.kind == EventKind::equip);
    CHECK(equipped.weapon == rifle);

    signal.kind = NativeSignalKind::vats_select;
    signal.mode = Mode::vats_selection;
    signal.menu = Menu::none;
    const auto selected = require_event(translator.translate(signal, player));
    CHECK(selected.kind == EventKind::vats_select);

    signal.kind = NativeSignalKind::vats_accept;
    signal.mode = Mode::vats_playback;
    const auto accepted = require_event(translator.translate(signal, player));
    CHECK(accepted.kind == EventKind::vats_accept);
    CHECK(accepted.mode == Mode::vats_selection);

    signal.source = npc;
    CHECK_FALSE(translator.translate(signal, player));
}

TEST_CASE("Signals from an earlier save epoch are discarded", "[new-vegas][signals]") {
    SignalTranslator translator;
    translator.reset(12);
    const NativeSignal old_hit{.at = MonoTime{} + 1ms,
        .epoch = 11,
        .mode = Mode::gameplay,
        .kind = NativeSignalKind::hit_received,
        .target = player};
    CHECK_FALSE(translator.translate(old_hit, player));
}
} // namespace
