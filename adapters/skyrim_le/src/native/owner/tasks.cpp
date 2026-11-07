#include "native/diagnostics.hpp"
#include "native/owner.hpp"
#include "native/skse_sdk.hpp"
#include "native/translators.hpp"

#include "native/observation.hpp"

#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/native/task_schedule.hpp>
#include <skyrim_adapter/native/worker_iteration.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <span>

namespace skyrim_adapter::native {
class NativeOwner::ScheduledTask final : public TaskDelegate {
public:
    ScheduledTask(NativeOwner* owner, NativeTicket ticket) noexcept
        : owner_(owner),
          ticket_(ticket) {}

    void Run() override {
        if (owner_) {
            owner_->scheduled_task(ticket_);
        }
    }

    void Dispose() override { delete this; }

private:
    NativeOwner* owner_{};
    NativeTicket ticket_{};
};

WorkerDispatchResult NativeOwner::dispatch_reserved_task(
    const TaskSchedule::Reservation& reservation) {
    auto* task = new (std::nothrow) ScheduledTask{this, reservation.ticket};
    if (!task) {
        complete_task();
        enter_inert("unable to allocate scheduled task");
        return WorkerDispatchResult::failed;
    }
    try {
        if (!first_task_offered_logged_.exchange(true, std::memory_order_relaxed)) {
            diagnostics::write("first_task_submission=offered_to_skse");
        }
        task_interface_->AddTask(task);
    } catch (...) {
        // The interface may have retained the delegate before failing. Keep it alive and retain the owner.
        enter_inert("SKSE AddTask raised an exception");
        return WorkerDispatchResult::failed;
    }
    return WorkerDispatchResult::offered;
}

void NativeOwner::scheduled_task(NativeTicket ticket) noexcept {
    if (!first_task_run_logged_.exchange(true, std::memory_order_relaxed)) {
        diagnostics::write("first_task_run=observed");
    }
    try {
        run_native_task(ticket);
    } catch (...) {
        enter_inert("scheduled task exception");
    }
    record_output_diagnostics();
    complete_task();
}

void NativeOwner::run_native_task(NativeTicket ticket) {
    if (stopping_.load(std::memory_order_acquire) || inert_.load(std::memory_order_acquire)) {
        return;
    }

    const auto current_ticket = coordinator_.native_ticket();
    if (!current_ticket || current_ticket->epoch != ticket.epoch) {
        return;
    }

    install_sinks();
    const auto started_at = std::chrono::steady_clock::now();
    auto capture = coordinator_.begin_read(ticket, started_at);
    if (!capture) {
        return;
    }

    detail::MenuSnapshot menus{};
    std::uint64_t read_menu_revision{};
    {
        std::scoped_lock lock(menu_mutex_);
        menus = menu_snapshot_;
        read_menu_revision = menu_revision_;
    }

    detail::ObservationDiagnostics read_diagnostics{};
    const auto observation = read_observation(started_at,
        focused_.load(std::memory_order_acquire),
        menus,
        &read_diagnostics);
    record_observation_diagnostics(read_diagnostics);
    const std::uint32_t observation_gate =
        (observation.available ? 1U : 0U) | (observation.alive ? 1U << 1U : 0U) |
        (observation.focused ? 1U << 2U : 0U) | (observation.menus_clear ? 1U << 3U : 0U);
    const auto observation_bit = std::uint32_t{1} << observation_gate;
    if ((observation_gate_states_seen_.fetch_or(observation_bit, std::memory_order_relaxed) &
            observation_bit) == 0) {
        diagnostics::write("first_observation available=",
            observation.available ? 1U : 0U,
            " alive=",
            observation.alive ? 1U : 0U,
            " focused=",
            observation.focused ? 1U : 0U,
            " menus_clear=",
            observation.menus_clear ? 1U : 0U);
    }
    if (!update_menu_state(observation, read_menu_revision)) {
        return;
    }

    std::array<Action, inbox_capacity> actions{};
    std::size_t action_count{};
    const auto conversion_time = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < capture->count; ++index) {
        if (const auto action =
                resolve_action(capture->events[index], observation, conversion_time)) {
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
    try {
        {
            std::scoped_lock lock(schedule_mutex_);
            schedule_.complete();
        }
        schedule_cv_.notify_one();
    } catch (...) {
        enter_inert("scheduler completion exception");
    }
}
} // namespace skyrim_adapter::native
