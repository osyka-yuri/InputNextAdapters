#include "../owner.hpp"

#include "../observation.hpp"

#include <skyrim_adapter/model.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace skyrim_adapter::native {
void NativeOwner::scheduled_task(NativeOwner* owner, NativeTicket ticket) noexcept {
    if (!owner) {
        return;
    }
    try {
        owner->run_native_task(ticket);
    } catch (...) {
        owner->enter_inert();
    }
    owner->complete_task();
}

void NativeOwner::run_native_task(NativeTicket ticket) {
    if (stopping_.load(std::memory_order_acquire) || inert_.load(std::memory_order_acquire)) {
        return;
    }

    const auto current_ticket = coordinator_.native_ticket();
    if (!current_ticket || current_ticket->epoch != ticket.epoch) {
        return;
    }

    if (install_pending_.exchange(false, std::memory_order_acq_rel)) {
        install_sinks();
    }
    if (inert_.load(std::memory_order_acquire)) {
        return;
    }
    install_footstep_sink();

    const auto started_at = std::chrono::steady_clock::now();
    auto capture = coordinator_.begin_read(ticket, started_at);
    if (!capture) {
        return;
    }

    std::uint64_t read_menu_revision{};
    {
        std::scoped_lock lock(menu_mutex_);
        read_menu_revision = menu_revision_;
    }

    const auto observation =
        read_observation(started_at, focused_.load(std::memory_order_acquire), led_settings_);
    if (!update_menu_state(observation, read_menu_revision)) {
        return;
    }

    std::array<Action, inbox_capacity> actions{};
    std::size_t action_count{};
    const auto conversion_time = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < capture->count; ++i) {
        if (const auto action = resolve_action(capture->events[i], observation, conversion_time)) {
            actions[action_count++] = *action;
        }
    }

    coordinator_.publish(*capture,
        observation,
        std::span<const Action>{actions.data(), action_count},
        std::chrono::steady_clock::now());
    notify_output();
}

void NativeOwner::complete_task() noexcept {
    {
        std::scoped_lock lock(schedule_mutex_);
        schedule_.complete();
    }
    schedule_cv_.notify_one();
}
} // namespace skyrim_adapter::native
