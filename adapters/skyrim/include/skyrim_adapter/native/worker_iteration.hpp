#pragma once

#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/native/task_schedule.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace skyrim_adapter::native {

enum class WorkerDispatchResult : std::uint8_t { offered, failed };
enum class WorkerWaitReason : std::uint8_t { worker_wake, task_pool };

struct WorkerIterationResult {
    std::optional<NativeTicket> ticket_after_tick;
    WorkerWaitReason wait_reason{WorkerWaitReason::worker_wake};
    bool stale_reservation{};
    bool dispatch_failed{};
};

struct NoReservationHook {
    void operator()([[maybe_unused]] const TaskSchedule::Reservation& reservation) const noexcept {}
};

// The hook is a narrow deterministic fence for testing an epoch change after reservation and
// before the production ticket revalidation. The dispatcher is the adapter's native AddTask call.
template <class Dispatcher, class AfterReservation = NoReservationHook>
[[nodiscard]] WorkerIterationResult run_worker_iteration(Coordinator& coordinator,
    TaskSchedule& schedule,
    std::mutex& schedule_mutex,
    const std::atomic<bool>& stopping,
    const std::atomic<bool>& inert,
    MonoTime scheduling_time,
    std::optional<MonoTime> supplied_tick_time,
    bool task_pool_available,
    Dispatcher&& dispatch,
    AfterReservation&& after_reservation = {}) {
    WorkerIterationResult result{};
    const auto ticket = coordinator.native_ticket();
    std::optional<TaskSchedule::Reservation> reservation;
    if (ticket && task_pool_available && !stopping.load(std::memory_order_acquire) &&
        !inert.load(std::memory_order_acquire)) {
        {
            std::scoped_lock lock(schedule_mutex);
            reservation = schedule.reserve(scheduling_time, ticket);
        }

        if (reservation) {
            std::forward<AfterReservation>(after_reservation)(*reservation);
            const auto current_ticket = coordinator.native_ticket();
            const bool stopping_now = stopping.load(std::memory_order_acquire);
            const bool inert_now = inert.load(std::memory_order_acquire);
            if (stopping_now || inert_now || !current_ticket ||
                current_ticket->epoch != reservation->ticket.epoch) {
                {
                    std::scoped_lock lock(schedule_mutex);
                    schedule.complete();
                    if (!stopping_now && !inert_now) {
                        schedule.notify_urgent();
                    }
                }
                result.stale_reservation = true;
            } else {
                result.dispatch_failed = std::forward<Dispatcher>(dispatch)(*reservation) ==
                                         WorkerDispatchResult::failed;
            }
        }
    } else if (ticket && !task_pool_available && !stopping.load(std::memory_order_acquire) &&
               !inert.load(std::memory_order_acquire)) {
        result.wait_reason = WorkerWaitReason::task_pool;
    }

    if (supplied_tick_time) {
        coordinator.tick(*supplied_tick_time);
    } else {
        coordinator.tick();
    }
    result.ticket_after_tick = coordinator.native_ticket();
    return result;
}

} // namespace skyrim_adapter::native
