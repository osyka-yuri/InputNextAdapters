#include "native/owner.hpp"
#include "support.hpp"
#include "support/one_shot_pause.hpp"

#include <input_next_core.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#undef FAIL

namespace fallout4_adapter::native_owner_test {
using namespace std::chrono_literals;
using namespace fallout4_adapter;
using namespace fallout4_adapter::native;

TEST_CASE("fallout4.native_owner.worker_iteration_blocked_body_progress",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    constexpr std::uint64_t generation = 401;
    configure_owner_hooks(generation);
    const auto owner_storage = make_test_owner();
    auto& owner = *owner_storage;
    auto& output = NativeOwnerTestAccess::output(owner);
    output.body_leases_enabled = true;

    const auto setup_time = std::chrono::steady_clock::now();
    const auto ticket = prepare_owner(owner, generation, setup_time);
    check(ticket.has_value(), "worker-iteration fixture opens its lifecycle ticket");
    if (!ticket) {
        return;
    }
    supply_observation({
        .weapon_identity = 88,
        .player_handle = 944,
        .player_address = 0x52345678U,
        .player_form_id = 0xCAFE0001U,
        .set_triggers = true,
        .sprinting = true,
    });
    schedule_and_run(owner, *ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    schedule_and_run(owner, *ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    const auto cached = NativeOwnerTestAccess::callback_player(owner);
    check(cached.has_value(), "worker-iteration fixture has current callback provenance");
    if (!cached) {
        return;
    }
    check(queue_shot(owner, cached), "worker-iteration fixture queues a current weapon shot");
    NativeOwnerTestAccess::process_pending(owner);
    check(NativeOwnerTestAccess::session(owner).diagnostics().accepted_events != 0,
        "worker-iteration fixture admits the current shot to the actual Session before its owner "
        "iteration");

    inputnext::test::OneShotPause blocked_body;
    std::size_t trigger_clears_at_body_entry{};
    std::size_t body_clears_at_body_entry{};
    output.on_body_submit = [&] {
        trigger_clears_at_body_entry = output.trigger_clear_calls;
        body_clears_at_body_entry = output.body_clear_calls;
        blocked_body.pause_once();
    };
    output.close_status_script[0] = INPUT_NEXT_STATUS_TIMED_OUT_UNKNOWN;
    output.close_status_script[1] = INPUT_NEXT_STATUS_REVOKED;
    output.close_status_script_size = 2;
    output.ack_close_status_script[0] = INPUT_NEXT_STATUS_BUSY;
    output.ack_close_status_script[1] = INPUT_NEXT_STATUS_OK;
    output.ack_close_status_script_size = 2;
    auto next_sample = std::chrono::steady_clock::now();
    bool event_overflow_reported{};
    std::atomic<bool> worker_finished{};
    bool iteration_continued{};

    struct IterationThreads {
        std::thread& callback;
        std::thread& worker;
    };

    struct WorkerIterationGuard {
        NativeOwner& owner;
        inputnext::test::OneShotPause& pause;
        IterationThreads threads;
        NativeOwnerTestOutput& out;

        WorkerIterationGuard(NativeOwner& owner_ref,
            inputnext::test::OneShotPause& pause_ref,
            IterationThreads running_threads,
            NativeOwnerTestOutput& output_ref) noexcept
            : owner(owner_ref),
              pause(pause_ref),
              threads(running_threads),
              out(output_ref) {}

        WorkerIterationGuard(const WorkerIterationGuard&) = delete;
        WorkerIterationGuard& operator=(const WorkerIterationGuard&) = delete;
        WorkerIterationGuard(WorkerIterationGuard&&) = delete;
        WorkerIterationGuard& operator=(WorkerIterationGuard&&) = delete;

        ~WorkerIterationGuard() {
            NativeOwnerTestAccess::request_stop(owner);
            pause.release();
            if (threads.callback.joinable()) {
                threads.callback.join();
            }
            if (threads.worker.joinable()) {
                threads.worker.join();
            }
            out.on_body_submit = {};
        }
    };

    std::thread main_callback;
    std::thread worker([&] {
        iteration_continued = NativeOwnerTestAccess::worker_iteration(owner,
            std::chrono::steady_clock::now(),
            true,
            next_sample,
            event_overflow_reported);
        NativeOwnerTestAccess::finish_worker(owner);
        worker_finished.store(true, std::memory_order_release);
    });

    WorkerIterationGuard const cleanup_guard{owner,
        blocked_body,
        IterationThreads{.callback = main_callback, .worker = worker},
        output};

    const bool body_entered = blocked_body.wait_until_entered(3s);
    const auto request_id = NativeOwnerTestAccess::request_id(owner);
    std::mutex callback_mutex;
    std::condition_variable callback_changed;
    bool callback_finished{};
    main_callback = std::thread([&] {
        NativeOwnerTestAccess::consume_main(owner);
        {
            std::scoped_lock lock{callback_mutex};
            callback_finished = true;
        }
        callback_changed.notify_one();
    });
    bool callback_progressed{};
    {
        std::unique_lock lock{callback_mutex};
        callback_progressed =
            callback_changed.wait_for(lock, 2s, [&] { return callback_finished; });
    }
    const bool still_blocked = !worker_finished.load(std::memory_order_acquire);
    NativeOwnerTestAccess::request_stop(owner);
    blocked_body.release();
    if (main_callback.joinable()) {
        main_callback.join();
    }
    if (worker.joinable()) {
        worker.join();
    }
    output.on_body_submit = {};

    check(body_entered && request_id != 0,
        "actual worker iteration published a request before body output blocked");
    check(callback_progressed && NativeOwnerTestAccess::request_id(owner) == 0 &&
              NativeOwnerTestAccess::has_sample(owner),
        "main-thread callback claimed and published the request while the actual body call was "
        "blocked");
    check(still_blocked, "worker iteration remained inside the blocked body until released");
    check(NativeOwnerTestAccess::stopping(owner) && !iteration_continued &&
              worker_finished.load(std::memory_order_acquire),
        "shutdown during body output stopped delivery before the same worker completed its cleanup "
        "phase");
    check(output.last_body_lease != 0 && output.close_calls == 2 && output.ack_close_calls == 2 &&
              output.close_requests[0].context.id == output.current_context.id &&
              output.close_requests[1].context.id == output.current_context.id &&
              output.close_requests[0].close_request_id == output.current_context.id &&
              output.close_requests[1].close_request_id == output.current_context.id &&
              output.last_acked_close_receipt.context.id == output.current_context.id &&
              output.last_acked_close_receipt.target.device_key ==
                  output.current_target.device_key &&
              output.last_acked_close_receipt.target.connection_id ==
                  output.current_target.connection_id,
        "worker shutdown retries the same unknown close and then acknowledges its retained exact "
        "receipt");
    check(output.trigger_clear_calls == trigger_clears_at_body_entry &&
              output.body_clear_calls == body_clears_at_body_entry,
        "worker shutdown releases accepted output through context close, without ordinary clears");
}

} // namespace fallout4_adapter::native_owner_test
