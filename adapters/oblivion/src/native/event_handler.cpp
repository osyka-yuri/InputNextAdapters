#include "event_handler.hpp"
#include "memory_guard.hpp"

#include "diagnostics.hpp"
#include "effect_model.hpp"
#include "interfaces.hpp"
#include "magic_forms.hpp"
#include "model.hpp"
#include "observation.hpp"
#include "player_state.hpp"
#include "task_handler.hpp"

#include <EventManager.h>
#include <GameActorValues.h>
#include <GameForms.h>
#include <GameObjects.h>
#include <PluginAPI.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <span>

namespace oblivion_adapter::native {
namespace {

struct FormFacts {
    std::uint32_t id{};
    std::uint8_t type{};
};

FormFacts form_facts(const void* pointer) noexcept {
    if (!pointer) {
        return {};
    }
    return guarded_game_read([&] {
        const auto* form = static_cast<const TESForm*>(pointer);
        return FormFacts{.id = form->refID, .type = form->typeID};
    });
}

// OnHitWith lacks an attacker. Keep its weapon candidates for targets hit by the
// player in the same frame.
struct ContactEvidence {
    std::array<EffectEvent, effect_inbox_capacity> sources{};
    std::array<std::uint32_t, effect_inbox_capacity> targets{};
    std::size_t source_count{};
    std::size_t target_count{};
    bool truncated{};
};

ContactEvidence contacts;

bool active_game_thread() noexcept {
    const auto& interfaces = PluginInterfaceManager::instance();
    return interfaces.on_game_thread() && interfaces.is_session_active();
}

bool equipment_type(std::uint8_t type) noexcept {
    return type == kFormType_Weapon || type == kFormType_Ammo || type == kFormType_Armor ||
           type == kFormType_Clothing || type == kFormType_Light;
}

void remember_contact_source(const void* target, const void* source) noexcept {
    if (contacts.source_count == contacts.sources.size()) {
        contacts.truncated = true;
        return;
    }
    const auto victim = form_facts(target);
    const auto item = form_facts(source);
    if (victim.id == 0 || item.id == 0) {
        return;
    }
    const auto weapon = PlayerStateReader::read_weapon(static_cast<const TESForm*>(source));
    if (weapon == Weapon::unknown) {
        return;
    }
    contacts.sources[contacts.source_count++] = {
        .kind = EffectEventKind::hit_with,
        .at = std::chrono::steady_clock::now(),
        .mode = EffectMode::gameplay,
        .target_id = victim.id,
        .source_id = item.id,
        .weapon = weapon,
    };
}

bool translate_hit(EffectEvent& event,
    const void* target,
    const void* attacker,
    PlayerCharacter* player) noexcept {
    const bool player_projectile =
        attacker != player &&
        PlayerStateReader::read_projectile_actor(static_cast<const TESForm*>(attacker), player);
    if (attacker != player && target != player && !player_projectile) {
        return false;
    }
    event.target_id = form_facts(target).id;
    if (event.target_id == 0) {
        return false;
    }
    if (player_projectile) {
        event.source_id = form_facts(attacker).id;
        event.weapon = Weapon::bow;
    } else if (attacker == player) {
        const auto equipped = PlayerStateReader::read_equipped_weapon(player, event.source_id);
        if (equipped == Weapon::unarmed) {
            event.weapon = equipped;
        }
        if (contacts.target_count < contacts.targets.size()) {
            contacts.targets[contacts.target_count++] = event.target_id;
        }
    } else {
        event.actor_id = form_facts(attacker).id;
    }
    return true;
}

struct MagicFormArguments {
    const TESForm* effect_form{};
    const TESForm* counterpart{};
};

bool translate_magic(EffectEvent& event,
    const MagicFormArguments& forms,
    const PlayerCharacter* player) noexcept {
    // Skip self-applications to avoid duplicating cast and consumption cues.
    if (event.kind == EffectEventKind::magic_applied && forms.counterpart == player) {
        return false;
    }
    if (!read_magic_form(forms.effect_form, event)) {
        return false;
    }
    if (event.kind == EffectEventKind::magic_applied) {
        event.target_id = event.actor_id;
        event.actor_id = form_facts(forms.counterpart).id;
    } else {
        event.target_id = form_facts(forms.counterpart).id;
    }
    return true;
}

bool translate_action(EffectEvent& event,
    const void* arg0,
    const void* arg1,
    PlayerCharacter* player,
    const TESObjectREFR* calling_object) noexcept {
    using enum EffectEventKind;
    switch (event.kind) {
    case create_potion:
    case enchant:
    case create_spell:
    case quest_complete:
    case discovered:
        event.source_id = form_facts(arg0).id;
        return event.source_id != 0;
    case soul_trap:
        return detail::read_player_soul_trap(arg0, arg1, calling_object, event);
    case vampire_feed:
        return true; // xOBSE dispatches this game event only for the player.
    case fall_impact:
        event.target_id = event.actor_id;
        return arg0 == player;
    case skill_up: {
        const auto code = reinterpret_cast<std::uintptr_t>(arg0);
        event.source_id = static_cast<std::uint32_t>(code);
        return code >= kActorVal_Armorer && code <= kActorVal_Speechcraft;
    }
    case drink_potion:
    case eat_ingredient:
    case equip:
    case unequip:
    case drop_item: {
        if (arg0 != player) {
            return false;
        }
        const auto item = form_facts(arg1);
        event.source_id = item.id;
        if (event.kind == drink_potion) {
            return item.id != 0 && item.type == kFormType_AlchemyItem;
        }
        if (event.kind == eat_ingredient) {
            return item.id != 0 && item.type == kFormType_Ingredient;
        }
        return item.id != 0 && (event.kind == drop_item || equipment_type(item.type));
    }
    default:
        if (arg0 != player) {
            return false;
        }
        if (event.kind == attack) {
            event.weapon = PlayerStateReader::read_equipped_weapon(player, event.source_id);
        }
        return true;
    }
}

EffectMode event_mode(EffectEventKind kind) noexcept {
    const auto mode = PlayerStateReader::read_effect_mode();
    if (mode == EffectMode::gameplay) {
        // Use the last sampled menu if crafting closed it before the callback.
        const auto previous = TaskHandler::last_effect_mode();
        switch (kind) {
        case EffectEventKind::create_potion:
        case EffectEventKind::enchant:
        case EffectEventKind::create_spell:
            return menu_effect(kind, previous) ? previous : EffectMode::unavailable;
        default:
            break;
        }
    }
    return mode == EffectMode::gameplay || menu_effect(kind, mode) ? mode : EffectMode::unavailable;
}

void dispatch(EffectEventKind kind,
    void const* arg0,
    void const* arg1,
    TESObjectREFR const* calling_object) noexcept {
    if (!active_game_thread()) {
        return;
    }
    PlayerCharacter* player = PlayerStateReader::get_player();
    if (!player) {
        return;
    }
    // Filter callbacks by player identity before reading the menu state.
    using enum EffectEventKind;
    switch (kind) {
    case attack:
    case bow_draw:
    case bow_attach:
    case recoil:
    case stagger:
    case dodge:
    case knockout:
    case fall_impact:
    case water_dive:
    case water_surface:
    case drink_potion:
    case eat_ingredient:
    case equip:
    case unequip:
    case drop_item:
        if (arg0 != player) {
            return;
        }
        break;
    case cast:
    case magic_applied:
        if (calling_object != player) {
            return;
        }
        break;
    default:
        break;
    }

    if (kind == hit_with) {
        if (TaskHandler::last_effect_mode() == EffectMode::gameplay) {
            remember_contact_source(arg0, arg1);
        }
        return;
    }

    EffectEvent event{
        .kind = kind,
        .at = std::chrono::steady_clock::now(),
        .actor_id = form_facts(player).id,
    };
    if (event.actor_id == 0 || !PlayerStateReader::is_player_pointer_valid(player)) {
        return;
    }
    if (kind == hit && !translate_hit(event, arg0, arg1, player)) {
        return;
    }
    event.mode = event_mode(kind);
    if (event.mode == EffectMode::unavailable) {
        return;
    }
    if (kind == cast || kind == magic_applied) {
        if (!translate_magic(event,
                {.effect_form = static_cast<const TESForm*>(arg0),
                    .counterpart = static_cast<const TESForm*>(arg1)},
                player)) {
            return;
        }
    } else if (kind != hit && !translate_action(event, arg0, arg1, player, calling_object)) {
        return;
    }
    if (kind == bow_attach) {
        // xOBSE dispatches before setting the attached action. Scan before the arrow launches.
        TaskHandler::note_bow_attachment(player, event);
    }
    TaskHandler::record(event);
}

template <EffectEventKind Kind>
void callback(void* arg0, void* arg1, TESObjectREFR* calling_object) noexcept {
    dispatch(Kind, arg0, arg1, calling_object);
}

struct Subscription {
    const char* name;
    EventManager::EventFunc callback;
};

using enum EffectEventKind;
constexpr std::array<Subscription, 28> subscriptions{{
    {.name = "onhealthdamage", .callback = EventHandler::on_health_damage},
    {.name = "onhit", .callback = callback<hit>},
    {.name = "onhitwith", .callback = callback<hit_with>},
    {.name = "onattack", .callback = callback<attack>},
    {.name = "onbowattack", .callback = callback<bow_draw>},
    {.name = "onbowarrowattach", .callback = callback<bow_attach>},
    {.name = "onmagiccast", .callback = callback<cast>},
    {.name = "onmagicapply", .callback = callback<magic_applied>},
    {.name = "onrecoil", .callback = callback<recoil>},
    {.name = "onstagger", .callback = callback<stagger>},
    {.name = "ondodge", .callback = callback<dodge>},
    {.name = "onknockout", .callback = callback<knockout>},
    {.name = "onfallimpact", .callback = callback<fall_impact>},
    {.name = "onwaterdive", .callback = callback<water_dive>},
    {.name = "onwatersurface", .callback = callback<water_surface>},
    {.name = "ondrinkpotion", .callback = callback<drink_potion>},
    {.name = "oneatingredient", .callback = callback<eat_ingredient>},
    {.name = "onactorequip", .callback = callback<equip>},
    {.name = "onactorunequip", .callback = callback<unequip>},
    {.name = "onactordrop", .callback = callback<drop_item>},
    {.name = "oncreatepotion", .callback = callback<create_potion>},
    {.name = "onenchant", .callback = callback<enchant>},
    {.name = "oncreatespell", .callback = callback<create_spell>},
    {.name = "onvampirefeed", .callback = callback<vampire_feed>},
    {.name = "onsoultrap", .callback = callback<soul_trap>},
    {.name = "onskillup", .callback = callback<skill_up>},
    {.name = "onquestcomplete", .callback = callback<quest_complete>},
    {.name = "onmapmarkeradd", .callback = callback<discovered>},
}};

} // namespace

void EventHandler::register_events(OBSEEventManagerInterface* event_mgr) noexcept {
    if (!event_mgr || !event_mgr->RegisterEvent) {
        return;
    }
    for (const auto& subscription : subscriptions) {
        // xOBSE matches callbacks by event and filters. Unregistering could remove
        // another plugin's handler.
        if (!event_mgr->RegisterEvent(subscription.name,
                subscription.callback,
                nullptr,
                nullptr,
                nullptr)) {
            std::array<char, 112> message{};
            const int written = std::snprintf(message.data(),
                message.size(),
                "xOBSE event unavailable: %s",
                subscription.name);
            if (written >= 0) {
                diagnostics::write(message.data());
            }
        }
    }
}

void EventHandler::flush_contacts() noexcept {
    if (!contacts.truncated) {
        const std::span targets{contacts.targets.data(), contacts.target_count};
        for (const auto& event : std::span{contacts.sources.data(), contacts.source_count}) {
            if (std::ranges::find(targets, event.target_id) != targets.end()) {
                TaskHandler::record(event);
            }
        }
    }
    discard_contacts();
}

void EventHandler::discard_contacts() noexcept {
    contacts.source_count = 0;
    contacts.target_count = 0;
    contacts.truncated = false;
}

DamageEvent EventHandler::interpret_health_damage(void* arg0,
    void* arg1,
    TESObjectREFR* this_object,
    PlayerCharacter* player) noexcept {
    if (!player || this_object != static_cast<TESObjectREFR*>(player)) {
        return {};
    }
    const auto amount = detail::decode_damage_payload(arg0);
    if (!std::isfinite(amount) || amount <= 0.0F) {
        return {};
    }
    return classify_damage_event({
        .damage = amount,
        .victim_ptr = this_object,
        .player_ptr = player,
        .attacker_ptr = arg1,
        .victim_form_id = form_facts(this_object).id,
        .attacker_form_id = form_facts(arg1).id,
    });
}

void EventHandler::on_health_damage(void* arg0, void* arg1, TESObjectREFR* this_object) noexcept {
    if (!active_game_thread()) {
        return;
    }
    auto* player = PlayerStateReader::get_player();
    if (!player || this_object != static_cast<TESObjectREFR*>(player) ||
        !PlayerStateReader::is_game_mode() || !PlayerStateReader::is_player_pointer_valid(player)) {
        return;
    }
    const auto event = interpret_health_damage(arg0, arg1, this_object, player);
    if (ObservationManager::instance().record_damage_event(event)) {
        TaskHandler::record({
            .kind = EffectEventKind::damage,
            .at = std::chrono::steady_clock::now(),
            .mode = EffectMode::gameplay,
            .actor_id = event.attacker_form_id,
            .target_id = event.victim_form_id,
            .amount = event.damage,
        });
    }
}

} // namespace oblivion_adapter::native
