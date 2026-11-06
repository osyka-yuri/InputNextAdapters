#include "../owner.hpp"

#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/native/worker_iteration.hpp>

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>

namespace skyrim_adapter::native {
namespace {
constexpr auto watchdog_interval = std::chrono::milliseconds{20};
} // namespace

void NativeOwner::notify_urgent() noexcept {
    {
        std::scoped_lock lock(schedule_mutex_);
        schedule_.notify_urgent();
    }
    schedule_cv_.notify_one();
}

void NativeOwner::notify_output() noexcept {
    {
        std::scoped_lock lock(schedule_mutex_);
        schedule_.notify_output();
    }
    schedule_cv_.notify_one();
}

void NativeOwner::worker_main() noexcept {
    try {
        while (!stopping_.load(std::memory_order_acquire)) {
            const auto now = std::chrono::steady_clock::now();
            const bool is_focused = foreground_is_game();
            const bool previous = focused_.exchange(is_focused, std::memory_order_acq_rel);
            if (previous != is_focused) {
                coordinator_.control(is_focused ? Control::focus_gained : Control::focus_lost, now);
                notify_urgent();
            }
            {
                std::scoped_lock lock(schedule_mutex_);
                (void)schedule_.consume_output_wake();
            }
            const auto scheduling_time = std::chrono::steady_clock::now();
            const auto iteration = run_worker_iteration(coordinator_,
                schedule_,
                schedule_mutex_,
                stopping_,
                inert_,
                scheduling_time,
                std::nullopt,
                true,
                [this](const TaskSchedule::Reservation& reservation) {
                    try {
                        const auto value = reservation;
                        task_interface_->AddTask(std::function<void()>{
                            [this, value] { NativeOwner::scheduled_task(this, value.ticket); },
                        });
                    } catch (...) {
                        complete_task();
                        enter_inert();
                        return WorkerDispatchResult::failed;
                    }
                    return WorkerDispatchResult::offered;
                });
            if (iteration.dispatch_failed || stopping_.load(std::memory_order_acquire)) {
                break;
            }

            std::unique_lock lock(schedule_mutex_);
            schedule_cv_.wait_for(lock, watchdog_interval, [this] {
                return stopping_.load(std::memory_order_acquire) ||
                       schedule_.wake_pending(ticket_admitted_.load(std::memory_order_acquire));
            });
        }
    } catch (...) {
        enter_inert();
    }

    while (!coordinator_.close_and_drain()) {
        std::unique_lock lock(schedule_mutex_);
        schedule_cv_.wait_for(lock, watchdog_interval);
    }
}

bool NativeOwner::foreground_is_game() noexcept {
    auto* const foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }

    DWORD process_id{};
    if (GetWindowThreadProcessId(foreground, &process_id) == 0 || process_id == 0) {
        return false;
    }
    return process_id == GetCurrentProcessId();
}
} // namespace skyrim_adapter::native
