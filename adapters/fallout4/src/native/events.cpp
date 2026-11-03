#include "animation_cues.hpp"
#include "bindings.hpp"
#include "hit_facts.hpp"
#include "owner.hpp"

#include "fallout4_adapter/model.hpp"
#include "fallout4_adapter/session.hpp"

#include <RE/B/BSAnimationGraphEvent.h>
#include <RE/B/BSTEvent.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/T/TESHitEvent.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

namespace fallout4_adapter::native {
namespace {
constexpr std::array<std::string_view, 13> relevant_menu_names{
    "PauseMenu",
    "VATSMenu",
    "PipboyMenu",
    "PowerArmorModMenu",
    "TerminalMenu",
    "ContainerMenu",
    "BarterMenu",
    "LockpickingMenu",
    "SleepWaitMenu",
    "BookMenu",
    "DialogueMenu",
    "WorkshopMenu",
    "LevelUpMenu",
};

[[nodiscard]] bool relevant_menu_event(const RE::MenuOpenCloseEvent& event) noexcept {
    const std::string_view name = event.menuName;
    return std::ranges::find(relevant_menu_names, name) != relevant_menu_names.end();
}
} // namespace

RE::BSEventNotifyControl NativeOwner::ProcessEvent(const WeaponFiredEvent& event,
    [[maybe_unused]] RE::BSTEventSource<WeaponFiredEvent>* event_source) {
    if (pipeline_.inert()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto decoded = decode_weapon_fired(event);
    if (!decoded || decoded->shooter_address == 0 || decoded->weapon_identity == 0) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto player = pipeline_.callback_player();
    if (!player || decoded->shooter_address != player->address) {
        return RE::BSEventNotifyControl::kContinue;
    }

    PolicyEvent copied{};
    copied.kind = EventKind::shot;
    copied.arrived_at = std::chrono::steady_clock::now();
    copied.weapon_identity = decoded->weapon_identity;
    static_cast<void>(pipeline_.queue_event(player->epoch, player->ui_generation, copied));
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl NativeOwner::ProcessEvent(const RE::TESHitEvent& event,
    [[maybe_unused]] RE::BSTEventSource<RE::TESHitEvent>* event_source) {
    if (pipeline_.inert()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto player = pipeline_.callback_player();
    if (!player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto target_address = reinterpret_cast<std::uintptr_t>(event.target.get());
    const auto cause_address = reinterpret_cast<std::uintptr_t>(event.cause.get());
    std::optional<HitFlags> flags;
    std::optional<float> health_damage;
    if (event.usesHitData) {
        flags.emplace(event.hitData.flags);
        health_damage = event.hitData.healthDamage;
    }

    auto copied = normalize_hit_event(
        HitAddresses{.player = player->address, .target = target_address, .cause = cause_address},
        flags,
        player->weapon_identity,
        health_damage);
    if (!copied) {
        return RE::BSEventNotifyControl::kContinue;
    }

    copied->arrived_at = std::chrono::steady_clock::now();
    static_cast<void>(pipeline_.queue_event(player->epoch, player->ui_generation, *copied));
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl NativeOwner::ProcessEvent(const RE::MenuOpenCloseEvent& event,
    [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* event_source) {
    if (!pipeline_.inert() && relevant_menu_event(event)) {
        pipeline_.queue_control(Control::context_changed, std::chrono::steady_clock::now());
    }
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl NativeOwner::ProcessEvent(const RE::BSAnimationGraphEvent& event,
    RE::BSTEventSource<RE::BSAnimationGraphEvent>* source) {
    if (pipeline_.inert() || !pipeline_.accepts_animation(source)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto cue = animation_cue(event.tag);
    if (!cue) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto player = pipeline_.callback_player();
    if (!player || !cue->allowed(player->power_armor)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    PolicyEvent copied{};
    copied.kind = cue->kind;
    copied.arrived_at = std::chrono::steady_clock::now();
    copied.power_armor = player->power_armor;
    copied.sprinting = player->sprinting;
    static_cast<void>(pipeline_.queue_event(player->epoch, player->ui_generation, copied, source));
    return RE::BSEventNotifyControl::kContinue;
}
} // namespace fallout4_adapter::native
