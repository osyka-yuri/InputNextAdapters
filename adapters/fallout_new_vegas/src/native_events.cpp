#include "native_state.hpp"

#include "diagnostics.hpp"
#include "event_reducer.hpp"
#include "native_combat.hpp"
#include "native_game.hpp"
#include "native_observation.hpp"

#include <fallout_gamebryo/model.hpp>

#include <nvse/GameForms.h>
#include <nvse/GameObjects.h>
#include <nvse/GameProcess.h>
#include <nvse/GameUI.h>
#include <nvse/PluginAPI.h>

#include <chrono>
#include <cstdint>
#include <format>

namespace fallout_new_vegas {
namespace {
using fallout_gamebryo::HitOutcome;
using fallout_gamebryo::Menu;
using fallout_gamebryo::Mode;

void log_hit_sample(const ActorHitData& hit,
    const HitForms& forms,
    const NativeSignal& signal,
    std::uint64_t frame,
    fallout_gamebryo::MonoTime captured_at) noexcept {
    try {
        const auto time =
            std::chrono::duration_cast<std::chrono::milliseconds>(captured_at.time_since_epoch());
        log(std::format("hit frame={} ms={} source={:08X} target={:08X} weapon={:08X} "
                        "flags={:08X} hit-location={} health-damage={} limb-damage={} block-dt={} "
                        "contact={:08X} outcome={}",
            frame,
            time.count(),
            forms.source,
            forms.target,
            forms.weapon,
            hit.flags,
            hit.hitLocation,
            hit.healthDmg,
            hit.limbDmg,
            hit.blockDTMod,
            signal.hit_reference,
            static_cast<std::uint32_t>(signal.hit_outcome)));
    } catch (...) {
        log("hit diagnostics could not be formatted");
    }
}

void log_registration(const char* name, bool available) noexcept {
    try {
        log(std::format("native event {}: {}", available ? "restored" : "unavailable", name));
    } catch (...) {
        log("native event registration status could not be formatted");
    }
}
} // namespace

NativeState::EventSources NativeState::register_events() noexcept {
    const auto subscribe = [this](const char* name,
                               NVSEEventManagerInterface::NativeEventHandler handler) {
        const bool registered = events_->SetNativeEventHandler(name, handler);
        if (!registered) {
            log_registration(name, false);
        }
        return registered;
    };
    return {.shots = register_shot_events(false),
        .misc_stats =
            subscribe("ShowOff:OnPCMiscStatChange", &NativeState::showoff_misc_stat_change),
        .hit = subscribe("onhit", &NativeState::game_hit),
        .equip = subscribe("onactorequip", &NativeState::game_equip),
        .lockpick_break = subscribe("onlockpickbroken", &NativeState::game_lockpick_broken),
        .terminal_success = subscribe("onterminalhacked", &NativeState::game_terminal_hacked),
        .terminal_failure =
            subscribe("onterminalhackfailed", &NativeState::game_terminal_hack_failed)};
}

NativeState::ShotEvents NativeState::register_shot_events(bool rebind) noexcept {
    const auto subscribe = [this, rebind](const char* name,
                               NVSEEventManagerInterface::NativeEventHandler handler,
                               bool previously_registered) {
        // A load can retain handlers when xNVSE skips its serialization preload callback.
        if (rebind) {
            static_cast<void>(events_->RemoveNativeEventHandler(name, handler));
        }
        const bool registered = events_->SetNativeEventHandler(name, handler);
        if ((rebind && registered != previously_registered) || (!rebind && !registered)) {
            log_registration(name, registered);
        }
        return registered;
    };
    const ShotEvents result{.fire = subscribe("ShowOff:OnFireWeapon",
                                &NativeState::showoff_fire,
                                event_sources_.shots.fire),
        .projectile = subscribe("ShowOff:OnProjectileCreate",
            &NativeState::showoff_projectile_create,
            event_sources_.shots.projectile)};
    if (!result.available() && (!rebind || event_sources_.shots.available())) {
        log("shot feedback unavailable because both ShowOff firearm event sources are required");
    } else if (rebind && result.available() && !event_sources_.shots.available()) {
        log("shot feedback restored");
    }
    return result;
}

void NativeState::showoff_fire(TESObjectREFR* actor, void* parameters) {
    auto& state = instance();
    if (!state.accepting_events() || !state.event_sources_.shots.available() || !actor ||
        actor != current_player() || !state.last_observation_) {
        return;
    }
    auto* const* forms = static_cast<void* const*>(parameters);
    const auto* weapon = forms ? static_cast<TESObjectWEAP*>(forms[0]) : nullptr;
    state.enqueue(NativeSignal{.kind = NativeSignalKind::fire_weapon,
        .source = form_id(actor),
        .form = form_id(weapon),
        .weapon_class = classify_weapon(weapon),
        .projectiles_per_shot = weapon != nullptr && weapon->numProjectiles != 0U
                                    ? weapon->numProjectiles
                                    : std::uint8_t{1},
        .automatic = weapon != nullptr && weapon->IsAutomatic()});
}

void NativeState::showoff_projectile_create(TESObjectREFR* projectile, void* parameters) {
    auto& state = instance();
    if (!state.accepting_events() || !state.event_sources_.shots.available() || !projectile ||
        !parameters) {
        return;
    }
    auto* const* forms = static_cast<void* const*>(parameters);
    const auto* owner = static_cast<TESObjectREFR*>(forms[0]);
    if (!owner || owner != current_player()) {
        return;
    }
    const auto* weapon = static_cast<TESObjectWEAP*>(forms[1]);
    state.enqueue(NativeSignal{.kind = NativeSignalKind::projectile_create,
        .source = form_id(owner),
        .form = form_id(weapon),
        .projectile = form_id(projectile),
        .weapon_class = classify_weapon(weapon)});
}

void NativeState::on_dry_fire() noexcept {
    if (!accepting_events() || !last_observation_ || !last_observation_->focused ||
        !last_observation_->alive || last_observation_->mode != Mode::gameplay) {
        return;
    }
    const auto* player = current_player();
    if (!player) {
        return;
    }
    const auto* weapon = player->GetEquippedWeapon();
    if (form_id(weapon) != last_observation_->weapon.id || !last_observation_->weapon.drawn) {
        return;
    }
    const NativeSignal signal{.kind = NativeSignalKind::dry_fire,
        .source = form_id(player),
        .form = form_id(weapon),
        .weapon_class = last_observation_->weapon.kind};
    enqueue(signal);
}

void NativeState::game_hit([[maybe_unused]] TESObjectREFR* object, void* parameters) {
    auto& state = instance();
    if (!state.accepting_events() || !state.event_sources_.hit || !parameters) {
        return;
    }
    auto* const* arguments = static_cast<void* const*>(parameters);
    const auto* target = static_cast<TESObjectREFR*>(arguments[0]);
    const auto* source = static_cast<TESObjectREFR*>(arguments[1]);
    const auto* player = current_player();
    if (!target || !player || !player->baseProcess) {
        return;
    }
    const bool incoming = target == player;
    if (!incoming && source != player) {
        return;
    }
    const auto* hit_data =
        incoming ? player->baseProcess->GetHitData() : player->baseProcess->GetHitData254();
    if (!hit_data || hit_data->source != source || hit_data->target != target) {
        if (state.settings_.diagnostics) {
            log("hit data did not match the xNVSE event");
        }
        return;
    }
    const HitForms forms{.source = form_id(source),
        .target = form_id(target),
        .weapon = form_id(hit_data->weapon),
        .weapon_class = classify_weapon(hit_data->weapon)};
    if (const auto signal = read_hit_signal(*hit_data, forms, incoming)) {
        const auto captured_at = std::chrono::steady_clock::now();
        if (state.settings_.diagnostics) {
            log_hit_sample(*hit_data, forms, *signal, state.frame_, captured_at);
        }
        if (signal->confirmed_damage || signal->hit_outcome == HitOutcome::blocked) {
            state.enqueue(*signal, captured_at);
        }
    }
}

void NativeState::showoff_misc_stat_change(TESObjectREFR* actor, void* parameters) {
    auto& state = instance();
    if (!state.accepting_events() || !state.event_sources_.misc_stats || !actor ||
        actor != current_player() || !parameters) {
        return;
    }
    auto* const* values = static_cast<void* const*>(parameters);
    state.enqueue(NativeSignal{.kind = NativeSignalKind::misc_stat_change,
        .source = form_id(actor),
        .detail = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(values[0])),
        .delta = static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(values[1]))});
}

void NativeState::game_equip([[maybe_unused]] TESObjectREFR* object, void* parameters) {
    auto& state = instance();
    if (!state.accepting_events() || !state.event_sources_.equip || !parameters) {
        return;
    }
    auto* const* forms = static_cast<void* const*>(parameters);
    const auto* actor = static_cast<TESObjectREFR*>(forms[0]);
    const auto* item = static_cast<TESForm*>(forms[1]);
    if (!actor || !item || actor != current_player()) {
        return;
    }
    state.enqueue(NativeSignal{.kind = NativeSignalKind::item_equipped,
        .source = form_id(actor),
        .form = form_id(item)});
}

void NativeState::game_lockpick_broken([[maybe_unused]] TESObjectREFR* object, void* parameters) {
    auto& state = instance();
    if (!state.accepting_events() || !state.event_sources_.lockpick_break || !parameters ||
        !InterfaceManager::IsMenuVisible(kMenuType_LockPick)) {
        return;
    }
    auto* const* forms = static_cast<void* const*>(parameters);
    const auto* locked = static_cast<TESObjectREFR*>(forms[0]);
    if (!locked) {
        return;
    }
    state.enqueue(NativeSignal{.mode = Mode::menu,
        .menu = Menu::lockpick,
        .kind = NativeSignalKind::lockpick_broken,
        .source = form_id(current_player()),
        .target = form_id(locked)});
}

void NativeState::game_terminal_hacked([[maybe_unused]] TESObjectREFR* object, void* parameters) {
    instance().enqueue_terminal_outcome(NativeSignalKind::terminal_hacked, parameters);
}

void NativeState::game_terminal_hack_failed([[maybe_unused]] TESObjectREFR* object,
    void* parameters) {
    instance().enqueue_terminal_outcome(NativeSignalKind::terminal_hack_failed, parameters);
}

void NativeState::enqueue_terminal_outcome(NativeSignalKind kind, void* parameters) noexcept {
    if (!accepting_events()) {
        return;
    }
    const bool hacked = kind == NativeSignalKind::terminal_hacked;
    const bool registered =
        hacked ? event_sources_.terminal_success : event_sources_.terminal_failure;
    if (!registered) {
        return;
    }
    const auto captured_at = std::chrono::steady_clock::now();
    const bool computers_visible = InterfaceManager::IsMenuVisible(kMenuType_Computers);
    const bool hacking_visible = InterfaceManager::IsMenuVisible(kMenuType_Hacking);
    const bool recent_terminal = recently_in_terminal(last_observation_, captured_at);
    if (settings_.diagnostics) {
        try {
            log(std::format("terminal callback {} frame={} computers-menu-visible={} "
                            "hacking-menu-visible={} recent-terminal={} has-arguments={}",
                hacked ? "hacked" : "lockout",
                frame_,
                computers_visible,
                hacking_visible,
                recent_terminal,
                parameters != nullptr));
        } catch (...) {
            log("terminal callback diagnostics could not be formatted");
        }
    }
    if ((!computers_visible && !hacking_visible && !recent_terminal) || !parameters) {
        return;
    }
    // xNVSE dispatches the player's hack result with the terminal as its sole argument.
    auto* const* forms = static_cast<void* const*>(parameters);
    const auto* terminal = static_cast<TESObjectREFR*>(forms[0]);
    if (!terminal || !terminal->baseForm || terminal->baseForm->typeID != kFormType_BGSTerminal) {
        return;
    }
    enqueue(NativeSignal{.mode = Mode::menu,
                .menu = Menu::terminal,
                .kind = kind,
                .source = form_id(current_player()),
                .target = form_id(terminal)},
        captured_at);
}
} // namespace fallout_new_vegas
