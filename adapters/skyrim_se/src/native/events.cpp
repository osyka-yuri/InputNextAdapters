#include "owner.hpp"
#include "shout.hpp"

#include <skyrim_adapter/blocking_menus.hpp>
#include <skyrim_adapter/model.hpp>

#include <RE/B/BGSFootstepEvent.h>
#include <RE/B/BSTEvent.h>
#include <RE/H/HighProcessData.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESEquipEvent.h>
#include <RE/T/TESHitEvent.h>
#include <RE/T/TESPlayerBowShotEvent.h>

#include <SKSE/Events.h>
#include <SKSE/Interfaces.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string_view>

namespace skyrim_adapter::native {
namespace {
[[nodiscard]] std::optional<EventKind> event_kind(SKSE::ActionEvent::Type type) noexcept {
    switch (type) {
    case SKSE::ActionEvent::Type::kWeaponSwing:
        return EventKind::weapon_swing;
    case SKSE::ActionEvent::Type::kSpellCast:
        return EventKind::spell_cast;
    case SKSE::ActionEvent::Type::kSpellFire:
        return EventKind::spell_fire;
    case SKSE::ActionEvent::Type::kVoiceCast:
        return EventKind::voice_cast;
    case SKSE::ActionEvent::Type::kVoiceFire:
        return EventKind::voice_fire;
    case SKSE::ActionEvent::Type::kBowDraw:
        return EventKind::bow_draw;
    case SKSE::ActionEvent::Type::kBowRelease:
        return EventKind::bow_release;
    case SKSE::ActionEvent::Type::kBeginDraw:
        return EventKind::begin_draw;
    case SKSE::ActionEvent::Type::kEndDraw:
        return EventKind::end_draw;
    case SKSE::ActionEvent::Type::kBeginSheathe:
        return EventKind::begin_sheathe;
    case SKSE::ActionEvent::Type::kEndSheathe:
        return EventKind::end_sheathe;
    }
    return std::nullopt;
}

[[nodiscard]] Hand event_hand(SKSE::ActionEvent::Slot slot) noexcept {
    switch (slot) {
    case SKSE::ActionEvent::Slot::kLeft:
        return Hand::left;
    case SKSE::ActionEvent::Slot::kRight:
        return Hand::right;
    case SKSE::ActionEvent::Slot::kVoice:
        return Hand::voice;
    }
    return Hand::unknown;
}

[[nodiscard]] std::array<char, 64> copy_menu_name(const RE::MenuOpenCloseEvent& event) noexcept {
    std::array<char, 64> name{};
    const auto* data = event.menuName.c_str();
    if (!data) {
        return name;
    }
    const auto length = std::min(std::strlen(data), name.size() - 1);
    std::copy_n(data, length, name.data());
    return name;
}
} // namespace

void NativeOwner::on_message(const SKSE::MessagingInterface::Message& message) noexcept {
    if (inert_.load(std::memory_order_acquire)) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    switch (message.type) {
    case SKSE::MessagingInterface::kDataLoaded:
        coordinator_.control(Control::data_loaded, now);
        ticket_admitted_.store(true, std::memory_order_release);
        install_pending_.store(true, std::memory_order_release);
        notify_urgent();
        break;
    case SKSE::MessagingInterface::kPreLoadGame:
        coordinator_.control(Control::pre_load, now);
        notify_urgent();
        break;
    case SKSE::MessagingInterface::kPostLoadGame: {
        const auto encoded = reinterpret_cast<std::uintptr_t>(message.data);
        const bool succeeded = message.dataLen == 1 && encoded == 1;
        coordinator_.control(succeeded ? Control::load_succeeded : Control::load_failed, now);
        notify_urgent();
        break;
    }
    case SKSE::MessagingInterface::kNewGame:
        coordinator_.control(Control::new_game, now);
        notify_urgent();
        break;
    default:
        break;
    }
}

bool NativeOwner::update_menu_state(const Observation& observation,
    std::uint64_t read_menu_revision) noexcept {
    bool transitioned = false;
    {
        std::scoped_lock lock(menu_mutex_);
        if (read_menu_revision != menu_revision_) {
            return false;
        }

        const bool should_suppress = !observation.menus_clear;
        if (should_suppress != menu_suppressed_) {
            coordinator_.control(should_suppress ? Control::menu_suppressed : Control::menu_resumed,
                std::chrono::steady_clock::now());
            menu_suppressed_ = should_suppress;
            transitioned = true;
        }
    }
    if (transitioned) {
        notify_urgent();
        return false;
    }
    return true;
}

RE::BSEventNotifyControl NativeOwner::ProcessEvent(const SKSE::ActionEvent* event,
    [[maybe_unused]] RE::BSTEventSource<SKSE::ActionEvent>* event_source) {
    if (!event || inert_.load(std::memory_order_acquire)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto kind = event_kind(event->type.get());
    if (!kind) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto ticket = coordinator_.native_ticket();
    if (!ticket) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto observed_at = std::chrono::steady_clock::now();
    RawEvent copied{};
    copied.kind = *kind;
    copied.hand = event_hand(event->slot.get());
    copied.actor_id = event->actor ? event->actor->GetFormID() : 0;
    copied.source_id = event->sourceForm ? event->sourceForm->GetFormID() : 0;
    copied.observed_at = observed_at;
    if (*kind == EventKind::voice_fire) {
        if (const auto* player = RE::PlayerCharacter::GetSingleton();
            player && detail::is_player_voice_fire(*kind,
                          copied.actor_id,
                          player->GetFormID(),
                          copied.source_id)) {
            if (const auto* process = player->GetHighProcess(); process && process->currentShout) {
                copied.shout_level = detail::shout_level_for_source(copied.source_id,
                    process->currentShout->GetFormID(),
                    process->currentShoutVariation);
            }
        }
    }
    if (coordinator_.enqueue(*ticket, copied)) {
        notify_urgent();
    }
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl NativeOwner::ProcessEvent(const RE::TESHitEvent* event,
    [[maybe_unused]] RE::BSTEventSource<RE::TESHitEvent>* event_source) {
    if (!event || inert_.load(std::memory_order_acquire)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto ticket = coordinator_.native_ticket();
    const auto observed_at = std::chrono::steady_clock::now();
    if (!ticket) {
        return RE::BSEventNotifyControl::kContinue;
    }

    RawEvent copied{};
    copied.kind = EventKind::hit;
    copied.actor_id = event->cause ? event->cause->GetFormID() : 0;
    copied.target_id = event->target ? event->target->GetFormID() : 0;
    copied.source_id = event->source;
    copied.projectile_id = event->projectile;
    copied.flags = event->flags.underlying();
    copied.observed_at = observed_at;
    if (coordinator_.enqueue(*ticket, copied)) {
        notify_urgent();
    }
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl NativeOwner::ProcessEvent(const RE::TESEquipEvent* event,
    [[maybe_unused]] RE::BSTEventSource<RE::TESEquipEvent>* event_source) {
    if (!event || inert_.load(std::memory_order_acquire)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto ticket = coordinator_.native_ticket();
    const auto observed_at = std::chrono::steady_clock::now();
    if (!ticket) {
        return RE::BSEventNotifyControl::kContinue;
    }

    RawEvent copied{};
    copied.kind = EventKind::equip;
    copied.actor_id = event->actor ? event->actor->GetFormID() : 0;
    copied.source_id = event->baseObject;
    copied.flags = event->equipped ? 1U : 0U;
    copied.observed_at = observed_at;
    if (coordinator_.enqueue(*ticket, copied)) {
        notify_urgent();
    }
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl NativeOwner::ProcessEvent(const RE::MenuOpenCloseEvent* event,
    [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* event_source) {
    if (!event || inert_.load(std::memory_order_acquire)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto ticket = coordinator_.native_ticket();
    const auto observed_at = std::chrono::steady_clock::now();
    const bool opening = event->opening;
    const auto name = copy_menu_name(*event);
    const std::string_view copied_name{name.data()};
    const bool known_blocking = skyrim_adapter::is_known_blocking_menu(copied_name);

    {
        std::scoped_lock lock(menu_mutex_);
        ++menu_revision_;
        if (opening && known_blocking && !menu_suppressed_) {
            coordinator_.control(Control::menu_suppressed, observed_at);
            menu_suppressed_ = true;
        }
    }

    if (ticket) {
        notify_urgent();
    }
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl NativeOwner::ProcessEvent(const RE::BGSFootstepEvent* event,
    [[maybe_unused]] RE::BSTEventSource<RE::BGSFootstepEvent>* event_source) {
    if (!event || inert_.load(std::memory_order_acquire)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto ticket = coordinator_.native_ticket();
    const auto observed_at = std::chrono::steady_clock::now();
    if (!ticket) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto actor = event->actor.get();
    if (!actor) {
        return RE::BSEventNotifyControl::kContinue;
    }

    RawEvent copied{};
    copied.kind = EventKind::footstep;
    copied.actor_id = actor->GetFormID();
    copied.observed_at = observed_at;
    if (coordinator_.enqueue(*ticket, copied)) {
        notify_urgent();
    }
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl NativeOwner::ProcessEvent(const RE::TESPlayerBowShotEvent* event,
    [[maybe_unused]] RE::BSTEventSource<RE::TESPlayerBowShotEvent>* event_source) {
    if (!event || inert_.load(std::memory_order_acquire)) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const auto ticket = coordinator_.native_ticket();
    const auto observed_at = std::chrono::steady_clock::now();
    const auto* player = RE::PlayerCharacter::GetSingleton();
    if (!ticket || !player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    RawEvent copied{};
    copied.kind = EventKind::player_bow_shot;
    copied.actor_id = player->GetFormID();
    copied.source_id = event->weapon;
    copied.observed_at = observed_at;
    if (coordinator_.enqueue(*ticket, copied)) {
        notify_urgent();
    }
    return RE::BSEventNotifyControl::kContinue;
}
} // namespace skyrim_adapter::native
