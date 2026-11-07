#include "diagnostics.hpp"
#include "owner.hpp"
#include "skse_sdk.hpp"
#include "translators.hpp"

#include <skyrim_adapter/model.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace skyrim_adapter::native {
namespace {
[[nodiscard]] std::string_view bounded_string(const char* text, std::size_t capacity) noexcept {
    if (!text) {
        return {};
    }
    std::size_t length{};
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    if (length == capacity) {
        return {};
    }
    return {text, length};
}

} // namespace

EventResult NativeOwner::ReceiveEvent(SKSEActionEvent* event,
    EventDispatcher<SKSEActionEvent>* /*dispatcher*/) {
    try {
        if (!event || inert_.load(std::memory_order_acquire)) {
            return kEvent_Continue;
        }

        const auto kind = detail::action_event_kind(event->type);
        if (!kind) {
            return kEvent_Continue;
        }
        const auto ticket = coordinator_.native_ticket();
        if (!ticket) {
            return kEvent_Continue;
        }

        RawEvent copied{};
        copied.kind = *kind;
        copied.hand = detail::event_hand(event->slot);
        copied.actor_id = event->actor ? event->actor->formID : 0;
        copied.source_id = event->sourceForm ? event->sourceForm->formID : 0;
        copied.observed_at = std::chrono::steady_clock::now();
        if (coordinator_.enqueue(*ticket, copied)) {
            if (!first_action_logged_.exchange(true, std::memory_order_relaxed)) {
                diagnostics::write("first_action_accepted kind=",
                    static_cast<unsigned>(copied.kind));
            }
            notify_urgent();
        }
    } catch (...) {
        enter_inert("action event callback exception");
    }
    return kEvent_Continue;
}

EventResult NativeOwner::ReceiveEvent(TESHitEvent* event,
    EventDispatcher<TESHitEvent>* /*dispatcher*/) {
    try {
        if (!event || inert_.load(std::memory_order_acquire)) {
            return kEvent_Continue;
        }

        const auto ticket = coordinator_.native_ticket();
        if (!ticket) {
            return kEvent_Continue;
        }

        RawEvent copied{};
        copied.kind = EventKind::hit;
        copied.actor_id = event->caster ? event->caster->formID : 0;
        copied.target_id = event->target ? event->target->formID : 0;
        copied.source_id = event->sourceFormID;
        copied.projectile_id = event->projectileFormID;
        copied.flags = event->flags;
        copied.observed_at = std::chrono::steady_clock::now();
        if (coordinator_.enqueue(*ticket, copied)) {
            if (!first_hit_logged_.exchange(true, std::memory_order_relaxed)) {
                diagnostics::write("first_hit_accepted");
            }
            notify_urgent();
        }
    } catch (...) {
        enter_inert("hit event callback exception");
    }
    return kEvent_Continue;
}

EventResult NativeOwner::ReceiveEvent(MenuOpenCloseEvent* event,
    EventDispatcher<MenuOpenCloseEvent>* /*dispatcher*/) {
    try {
        if (!event || inert_.load(std::memory_order_acquire)) {
            return kEvent_Continue;
        }

        const auto observed_at = std::chrono::steady_clock::now();
        const auto name = bounded_string(event->menuName.data, detail::menu_name_capacity);
        {
            std::scoped_lock lock(menu_mutex_);
            ++menu_revision_;
            detail::apply_menu_event(menu_snapshot_, event->menuName.data, event->opening);
            const bool should_suppress =
                detail::menu_event_requires_immediate_suppression(menu_snapshot_,
                    name,
                    event->opening);
            if (should_suppress && !menu_suppressed_) {
                coordinator_.control(Control::menu_suppressed, observed_at);
                menu_suppressed_ = true;
            }
        }
        if (coordinator_.native_ticket()) {
            notify_urgent();
        }
    } catch (...) {
        enter_inert("menu event callback exception");
    }
    return kEvent_Continue;
}

bool NativeOwner::update_menu_state(const Observation& observation,
    std::uint64_t read_menu_revision) noexcept {
    try {
        bool transitioned = false;
        {
            std::scoped_lock lock(menu_mutex_);
            if (read_menu_revision != menu_revision_) {
                return false;
            }

            const bool should_suppress = !observation.menus_clear;
            if (should_suppress != menu_suppressed_) {
                coordinator_.control(should_suppress ? Control::menu_suppressed
                                                     : Control::menu_resumed,
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
    } catch (...) {
        enter_inert("menu state reconciliation exception");
        return false;
    }
}
} // namespace skyrim_adapter::native
