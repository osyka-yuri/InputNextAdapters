#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include "support/one_shot_pause.hpp"
#include <algorithm>
#include <atomic>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/native/task_schedule.hpp>
#include <skyrim_adapter/native/worker_iteration.hpp>
#include <span>
#include <thread>

namespace skyrim_adapter::tests::schedule {
namespace {
using namespace std::chrono_literals;
using namespace skyrim_adapter::native;

Action voice_action(MonoTime observed_at) {
    Action action{};
    action.event = session_test::raw_event(EventKind::voice_fire, 0x14, observed_at);
    return action;
}

void make_urgent(TaskSchedule& schedule, std::mutex& mutex) {
    std::scoped_lock lock{mutex};
    schedule.notify_urgent();
}
} // namespace

TEST_CASE("skyrim.schedule.le_missing_pool_safety_tick", "[component][skyrim][schedule]") {
    using namespace session_test;
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    const auto lease = fixture.output.active_body_lease;
    REQUIRE(!fixture.output.body_submissions.empty());
    const auto body_request = fixture.output.body_submissions.back().request;
    INFO("missing-pool fixture did not establish an accepted body lease");
    REQUIRE(lease != 0);

    fixture.coordinator.control(Control::focus_lost, fixture.time);
    TaskSchedule schedule(fixture.time);
    std::mutex schedule_mutex;
    make_urgent(schedule, schedule_mutex);
    std::atomic<bool> const stopping{false};
    std::atomic<bool> const inert{false};
    std::uint32_t dispatch_count{};
    const auto iteration = native::run_worker_iteration(fixture.coordinator,
        schedule,
        schedule_mutex,
        stopping,
        inert,
        fixture.time,
        fixture.time,
        false,
        [&](const TaskSchedule::Reservation&) {
            ++dispatch_count;
            return native::WorkerDispatchResult::offered;
        });

    INFO("missing task pool did not select its wait");
    CHECK(iteration.wait_reason == native::WorkerWaitReason::task_pool);

    INFO("missing task pool consumed or dispatched the due observation reservation");
    CHECK(dispatch_count == 0);
    CHECK(!schedule.outstanding());
    CHECK(schedule.urgent_pending());

    INFO("missing task pool skipped the serialized safety tick and exact body withdrawal");
    CHECK(!fixture.coordinator.diagnostics().body_owner_present);
    CHECK((std::ranges::any_of(fixture.output.body_clears, [&](const auto& clear) {
        return clear.lease_id == lease;
    }) || has_acked_close(fixture.output, body_request.context, body_request.target)));
}

TEST_CASE("skyrim.schedule.stale_reservation_safety_tick", "[component][skyrim][schedule]") {
    using namespace session_test;
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    const auto old_ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto lease = fixture.output.active_body_lease;
    REQUIRE(!fixture.output.body_submissions.empty());
    const auto body_request = fixture.output.body_submissions.back().request;
    INFO("stale-reservation fixture did not establish an accepted body lease");
    REQUIRE(lease != 0);

    TaskSchedule schedule(fixture.time);
    std::mutex schedule_mutex;
    make_urgent(schedule, schedule_mutex);
    std::atomic<bool> const stopping{false};
    std::atomic<bool> const inert{false};
    std::uint32_t dispatch_count{};
    const auto iteration = native::run_worker_iteration(
        fixture.coordinator,
        schedule,
        schedule_mutex,
        stopping,
        inert,
        fixture.time,
        fixture.time,
        true,
        [&](const TaskSchedule::Reservation&) {
            ++dispatch_count;
            return native::WorkerDispatchResult::offered;
        },
        [&](const TaskSchedule::Reservation&) {
            fixture.coordinator.control(Control::focus_lost, fixture.time);
        });

    INFO("ticket invalidation after reservation was not detected");
    CHECK(iteration.stale_reservation);

    INFO("stale reservation was dispatched or failed to rearm due observation work");
    CHECK(dispatch_count == 0);
    CHECK(!schedule.outstanding());
    CHECK(schedule.urgent_pending());

    INFO("post-tick ticket refresh did not report the new current epoch");
    const auto current_ticket = require_ticket(iteration.ticket_after_tick);
    CHECK(current_ticket.epoch != old_ticket.epoch);

    INFO("stale reservation skipped its safety tick or exact owner withdrawal");
    CHECK(!fixture.coordinator.diagnostics().body_owner_present);
    CHECK((std::ranges::any_of(fixture.output.body_clears, [&](const auto& clear) {
        return clear.lease_id == lease;
    }) || has_acked_close(fixture.output, body_request.context, body_request.target)));
}

TEST_CASE("skyrim.schedule.dispatch_failure_safety_tick", "[component][skyrim][schedule]") {
    using namespace session_test;
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    const auto lease = fixture.output.active_body_lease;
    REQUIRE(!fixture.output.body_submissions.empty());
    const auto body_request = fixture.output.body_submissions.back().request;
    INFO("dispatch-failure fixture did not establish an accepted body lease");
    REQUIRE(lease != 0);

    TaskSchedule schedule(fixture.time);
    std::mutex schedule_mutex;
    make_urgent(schedule, schedule_mutex);
    std::atomic<bool> stopping{false};
    std::atomic<bool> inert{false};
    std::uint32_t dispatch_count{};
    const auto iteration = native::run_worker_iteration(fixture.coordinator,
        schedule,
        schedule_mutex,
        stopping,
        inert,
        fixture.time,
        fixture.time,
        true,
        [&](const TaskSchedule::Reservation&) {
            ++dispatch_count;
            inert.store(true, std::memory_order_release);
            stopping.store(true, std::memory_order_release);
            fixture.coordinator.fault();
            {
                std::scoped_lock lock{schedule_mutex};
                schedule.complete();
            }
            return native::WorkerDispatchResult::failed;
        });

    INFO("native dispatch failure did not reach the inert shutdown branch");
    CHECK(iteration.dispatch_failed);
    CHECK(stopping.load(std::memory_order_acquire));

    INFO("failed native dispatch skipped tick diagnostics or retained a live reservation");
    CHECK(dispatch_count == 1);
    CHECK(!iteration.ticket_after_tick.has_value());
    CHECK(!schedule.outstanding());
    CHECK(fixture.coordinator.diagnostics().faulted);

    INFO("failed native dispatch skipped the final exact-owner withdrawal tick");
    CHECK(!fixture.coordinator.diagnostics().body_owner_present);
    CHECK((std::ranges::any_of(fixture.output.body_clears, [&](const auto& clear) {
        return clear.lease_id == lease;
    }) || has_acked_close(fixture.output, body_request.context, body_request.target)));
}

TEST_CASE("skyrim.schedule.blocked_body_ingress_progress", "[component][skyrim][schedule]") {
    using namespace session_test;
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    REQUIRE(!fixture.output.body_submissions.empty());
    const auto body_request = fixture.output.body_submissions.back().request;
    fixture.time += 1ms;
    const auto action = voice_action(fixture.time);
    fixture.observe(casting(), std::span{&action, 1});

    TaskSchedule schedule(fixture.time);
    std::mutex schedule_mutex;
    make_urgent(schedule, schedule_mutex);
    std::atomic<bool> stopping{false};
    std::atomic<bool> inert{false};
    std::uint32_t dispatch_count{};
    bool outstanding_at_dispatch{};
    inputnext::test::OneShotPause blocked;
    fixture.output.on_body_submit = [&] {
        blocked.pause_once();
    };

    native::WorkerIterationResult iteration{};
    std::thread worker;
    std::thread callback;

    // RAII guard ensuring both threads are properly released and joined before exiting test
    struct ThreadCleaner {
        inputnext::test::OneShotPause& pause;
        std::thread& t1;
        std::thread& t2;

        ThreadCleaner(inputnext::test::OneShotPause& pause_state,
            std::thread& thread1,
            std::thread& thread2) noexcept
            : pause(pause_state),
              t1(thread1),
              t2(thread2) {}

        ThreadCleaner(const ThreadCleaner&) = delete;
        ThreadCleaner& operator=(const ThreadCleaner&) = delete;
        ThreadCleaner(ThreadCleaner&&) = delete;
        ThreadCleaner& operator=(ThreadCleaner&&) = delete;

        ~ThreadCleaner() {
            pause.release();
            if (t1.joinable()) {
                t1.join();
            }
            if (t2.joinable()) {
                t2.join();
            }
        }
    } const cleaner{blocked, worker, callback};

    worker = std::thread{[&] {
        iteration = native::run_worker_iteration(fixture.coordinator,
            schedule,
            schedule_mutex,
            stopping,
            inert,
            fixture.time,
            fixture.time,
            true,
            [&](const TaskSchedule::Reservation& reservation) {
                ++dispatch_count;
                outstanding_at_dispatch = schedule.outstanding() && reservation.urgent;
                return native::WorkerDispatchResult::offered;
            });
    }};
    const bool body_blocked = blocked.wait_until_entered(2s);

    std::mutex callback_mutex;
    std::condition_variable callback_changed;
    bool callback_completed{};
    bool capture_published{};
    callback = std::thread{[&] {
        const auto ticket = fixture.coordinator.native_ticket();
        if (ticket) {
            const auto capture = fixture.coordinator.begin_read(*ticket, fixture.time);
            if (capture) {
                fixture.coordinator.publish(*capture, idle(), {}, fixture.time);
                capture_published = true;
            }
        }
        fixture.coordinator.control(Control::focus_lost, fixture.time);
        stopping.store(true, std::memory_order_release);
        {
            std::scoped_lock lock{callback_mutex};
            callback_completed = true;
        }
        callback_changed.notify_one();
    }};
    bool callback_progressed{};
    {
        std::unique_lock lock{callback_mutex};
        callback_progressed =
            callback_changed.wait_for(lock, 1s, [&] { return callback_completed; });
    }

    blocked.release();
    if (callback.joinable()) {
        callback.join();
    }
    if (worker.joinable()) {
        worker.join();
    }
    fixture.output.on_body_submit = {};
    if (stopping.load(std::memory_order_acquire)) {
        fixture.coordinator.tick(fixture.time);
    }

    INFO("production worker iteration did not enter its serialized body output");
    CHECK(body_blocked);

    INFO("main-thread begin_read/publish callback waited for the blocked coordinator body call");
    CHECK(callback_progressed);
    CHECK(capture_published);

    INFO("production iteration did not reserve one task, refresh its ticket, and observe shutdown");
    CHECK(dispatch_count == 1);
    CHECK(outstanding_at_dispatch);
    CHECK(iteration.ticket_after_tick.has_value());
    CHECK(stopping.load(std::memory_order_acquire));

    INFO("shutdown while body output was blocked did not finish with body-owner withdrawal");
    CHECK(!fixture.coordinator.diagnostics().body_owner_present);
    CHECK(has_acked_close(fixture.output, body_request.context, body_request.target));
}

TEST_CASE("skyrim.schedule.pre_callback_worker_safety", "[component][skyrim][schedule]") {
    using namespace session_test;

    // Worker iterations prior to DataLoaded do not dispatch tasks or acquire tickets.
    FakeOutput output;
    const auto coordinator_owner = std::make_unique<Coordinator>(output);
    auto& coordinator = *coordinator_owner;
    MonoTime time{1s};
    TaskSchedule schedule{time};
    std::mutex schedule_mutex;
    std::atomic<bool> stopping{false};
    std::atomic<bool> const inert{false};
    std::uint32_t dispatch_count{};

    const auto dispatch = [&](const TaskSchedule::Reservation&) {
        ++dispatch_count;
        return WorkerDispatchResult::offered;
    };

    INFO("fresh coordinator unexpectedly reported an available native ticket");
    CHECK(!coordinator.native_ticket().has_value());

    for (int step = 0; step < 5; ++step) {
        time += 20ms;
        const auto iteration = run_worker_iteration(coordinator,
            schedule,
            schedule_mutex,
            stopping,
            inert,
            time,
            time,
            true,
            dispatch);

        INFO("pre-admission worker iteration dispatched a native task");
        CHECK(dispatch_count == 0);

        INFO("pre-admission worker iteration produced an active ticket");
        CHECK(!iteration.ticket_after_tick.has_value());
        CHECK(iteration.wait_reason == WorkerWaitReason::worker_wake);
        CHECK(!iteration.stale_reservation);
        CHECK(!iteration.dispatch_failed);
    }

    INFO("worker ran before DataLoaded but native ticket became available");
    CHECK(!coordinator.native_ticket().has_value());

    INFO("worker ran before DataLoaded but resolved device target or opened effects context");
    CHECK(output.target_queries == 0);
    CHECK(output.open_calls == 0);
    CHECK(output.contexts.empty());

    INFO("worker ran before DataLoaded but retained an effects context or body owner");
    CHECK(!coordinator.diagnostics().body_owner_present);

    // Non-admitting lifecycle transitions without Control::data_loaded.
    make_urgent(schedule, schedule_mutex);
    time += 20ms;
    coordinator.control(Control::focus_lost, time);
    time += 20ms;
    coordinator.control(Control::focus_gained, time);
    time += 20ms;
    coordinator.control(Control::load_succeeded, time);
    time += 20ms;
    coordinator.control(Control::new_game, time);
    time += 20ms;
    coordinator.control(Control::menu_suppressed, time);
    time += 20ms;
    coordinator.control(Control::menu_resumed, time);
    time += 20ms;
    coordinator.control(Control::pre_load, time);
    time += 20ms;
    coordinator.control(Control::load_failed, time);

    for (int step = 0; step < 3; ++step) {
        time += 20ms;
        const auto iteration = run_worker_iteration(coordinator,
            schedule,
            schedule_mutex,
            stopping,
            inert,
            time,
            time,
            true,
            dispatch);

        INFO("non-admitting control transitions dispatched a native task");
        CHECK(dispatch_count == 0);

        INFO("non-admitting control transitions produced an active ticket");
        CHECK(!iteration.ticket_after_tick.has_value());
    }

    INFO("non-admitting transitions bypassed the DataLoaded requirement");
    CHECK(!coordinator.native_ticket().has_value());
    CHECK(output.target_queries == 0);
    CHECK(output.open_calls == 0);
    CHECK(output.contexts.empty());
    CHECK(!coordinator.diagnostics().body_owner_present);

    // Teardown during unadmitted startup completes immediately without runtime close calls.
    stopping.store(true, std::memory_order_release);

    time += 20ms;
    const auto stop_iteration = run_worker_iteration(coordinator,
        schedule,
        schedule_mutex,
        stopping,
        inert,
        time,
        time,
        true,
        dispatch);

    INFO("stopping worker iteration dispatched a native task or produced a ticket");
    CHECK(dispatch_count == 0);
    CHECK(!stop_iteration.ticket_after_tick.has_value());

    const bool drained = coordinator.close_and_drain();

    INFO("close_and_drain did not complete immediately for unadmitted coordinator");
    CHECK(drained);

    INFO("close_and_drain issued runtime close or ack calls when no context was opened");
    CHECK(output.close_calls == 0);
    CHECK(output.ack_close_calls == 0);
    CHECK(output.close_requests.empty());
    CHECK(output.close_receipts.empty());
    CHECK(output.acknowledged_close_receipts.empty());

    INFO("context or ticket was retained after failed startup teardown");
    CHECK(output.contexts.empty());
    CHECK(!coordinator.diagnostics().body_owner_present);
    CHECK(!coordinator.native_ticket().has_value());
}

} // namespace skyrim_adapter::tests::schedule
