#include "native/diagnostics.hpp"
#include "native/owner.hpp"
#include "native/skse_sdk.hpp"

#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/native/worker_iteration.hpp>

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

namespace skyrim_adapter::native {
namespace {
constexpr auto watchdog_interval = std::chrono::milliseconds{20};

} // namespace

void NativeOwner::notify_urgent() noexcept {
    try {
        {
            std::scoped_lock lock(schedule_mutex_);
            schedule_.notify_urgent();
        }
        schedule_cv_.notify_one();
    } catch (...) {
        enter_inert("urgent scheduler notification exception");
    }
}

void NativeOwner::notify_output() noexcept {
    try {
        {
            std::scoped_lock lock(schedule_mutex_);
            schedule_.notify_output();
        }
        schedule_cv_.notify_one();
    } catch (...) {
        enter_inert("output scheduler notification exception");
    }
}

void NativeOwner::worker_main() noexcept {
    try {
        while (!stopping_.load(std::memory_order_acquire)) {
            const auto now = std::chrono::steady_clock::now();
            const bool is_focused = foreground_is_game();
            update_focus(now, is_focused);
            {
                std::scoped_lock lock(schedule_mutex_);
                (void)schedule_.consume_output_wake();
            }
            const auto ticket = coordinator_.native_ticket();
            const auto* task_pool = ticket ? BSTaskPool::GetSingleton() : nullptr;
            if (ticket && !task_pool) {
                report_task_pool_unavailable();
            } else if (ticket) {
                report_task_pool_available();
            }
            const auto scheduling_time = std::chrono::steady_clock::now();
            const auto iteration = run_worker_iteration(coordinator_,
                schedule_,
                schedule_mutex_,
                stopping_,
                inert_,
                scheduling_time,
                std::nullopt,
                task_pool != nullptr,
                [this](const TaskSchedule::Reservation& reservation) {
                    return dispatch_reserved_task(reservation);
                });
            record_output_diagnostics();
            if (stopping_.load(std::memory_order_acquire)) {
                break;
            }
            if (iteration.wait_reason == WorkerWaitReason::task_pool) {
                std::unique_lock lock(schedule_mutex_);
                wait_for_task_pool(lock);
                continue;
            }

            std::unique_lock lock(schedule_mutex_);
            wait_for_worker_wake(lock);
        }
    } catch (...) {
        enter_inert("worker thread exception");
    }

    while (!coordinator_.close_and_drain()) {
        std::unique_lock lock(schedule_mutex_);
        schedule_cv_.wait_for(lock, watchdog_interval);
    }
    record_output_diagnostics();
}

void NativeOwner::update_focus(MonoTime now, bool is_focused) {
    const bool previous = focused_.exchange(is_focused, std::memory_order_acq_rel);
    if (previous != is_focused) {
        coordinator_.control(is_focused ? Control::focus_gained : Control::focus_lost, now);
        notify_urgent();
    }
}

void NativeOwner::report_task_pool_unavailable() {
    constexpr std::uint32_t unavailable_bit = 1U << 0U;
    if ((task_pool_states_seen_.fetch_or(unavailable_bit, std::memory_order_relaxed) &
            unavailable_bit) == 0) {
        diagnostics::write("task_pool=unavailable; task_submission=deferred");
    }
}

void NativeOwner::wait_for_task_pool(std::unique_lock<std::mutex>& lock) {
    schedule_cv_.wait_for(lock, watchdog_interval, [this] {
        return stopping_.load(std::memory_order_acquire) || schedule_.wake_pending(false);
    });
}

void NativeOwner::report_task_pool_available() {
    constexpr std::uint32_t available_bit = 1U << 1U;
    if ((task_pool_states_seen_.fetch_or(available_bit, std::memory_order_relaxed) &
            available_bit) == 0) {
        diagnostics::write("task_pool=available; task_submissions=resumed");
    }
}

void NativeOwner::wait_for_worker_wake(std::unique_lock<std::mutex>& lock) {
    schedule_cv_.wait_for(lock, watchdog_interval, [this] {
        return stopping_.load(std::memory_order_acquire) ||
               schedule_.wake_pending(ticket_admitted_.load(std::memory_order_acquire));
    });
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
