#include "test_support.hpp"
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/native/task_schedule.hpp>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::tests::schedule {

TEST_CASE("skyrim.schedule.scheduler_urgent_and_periodic_gates", "[component][skyrim][schedule]") {
    using namespace std::chrono_literals;
    using namespace skyrim_adapter;
    using namespace skyrim_adapter::native;

    const auto start = MonoTime{100s};
    const NativeTicket ticket{7};

    TaskSchedule schedule(start);
    schedule.notify_urgent();
    schedule.notify_urgent();
    auto reservation = schedule.reserve(start + 1ms, ticket);
    INFO("urgent notices coalesce and reserve before the normal poll");
    REQUIRE(reservation.has_value());
    CHECK(require_value(reservation).urgent);
    CHECK(require_value(reservation).ticket.epoch == ticket.epoch);

    INFO("reservation consumes only the pending urgency");
    CHECK(schedule.outstanding());
    CHECK(!schedule.urgent_pending());

    INFO("only one native task reservation can be outstanding");
    CHECK(!schedule.reserve(start + 2ms, ticket).has_value());

    schedule.notify_urgent();
    INFO("urgency received while a task is pending is retained");
    CHECK(schedule.urgent_pending());

    schedule.complete();
    reservation = schedule.reserve(start + 3ms, ticket);
    INFO("retained urgency dispatches after completion");
    REQUIRE(reservation.has_value());
    CHECK(require_value(reservation).urgent);

    schedule.complete();
    INFO("task completion alone does not create another task");
    CHECK(!schedule.reserve(start + 4ms, ticket).has_value());

    TaskSchedule periodic(start);
    INFO("watchdog wake does not schedule before the observation deadline");
    CHECK(!periodic.reserve(start + 20ms, ticket).has_value());

    reservation = periodic.reserve(start + 50ms, ticket);
    INFO("ordinary observation poll becomes eligible at its deadline");
    REQUIRE(reservation.has_value());
    CHECK(!require_value(reservation).urgent);

    periodic.complete();
    INFO("ordinary polling remains deadline bounded");
    CHECK(!periodic.reserve(start + 99ms, ticket).has_value());

    INFO("ordinary poll advances by one interval");
    CHECK(periodic.reserve(start + 100ms, ticket).has_value());

    TaskSchedule unavailable(start);
    unavailable.notify_urgent();
    INFO("urgent notice remains pending until an epoch ticket is available");
    CHECK(!unavailable.reserve(start, std::nullopt).has_value());
    CHECK(unavailable.urgent_pending());

    INFO("pending urgency reserves against the available ticket");
    CHECK(unavailable.reserve(start, NativeTicket{8}).has_value());
}

TEST_CASE("skyrim.schedule.scheduler_state_transition_regressions",
    "[component][skyrim][schedule]") {
    using namespace std::chrono_literals;
    using namespace skyrim_adapter;
    using namespace skyrim_adapter::native;

    const auto t0 = MonoTime{200s};
    const NativeTicket epoch1{10};
    const NativeTicket epoch2{11};

    // Single reservation: while outstanding is true, neither deadline nor urgency yields a new slot.
    {
        TaskSchedule schedule(t0);
        const auto first = schedule.reserve(t0 + 50ms, epoch1);
        INFO("initial reservation succeeds and sets outstanding");
        REQUIRE(first.has_value());
        CHECK(schedule.outstanding());

        INFO("subsequent reserve while outstanding returns nullopt");
        CHECK(!schedule.reserve(t0 + 60ms, epoch1).has_value());

        schedule.notify_urgent();
        INFO("reserve remains blocked while outstanding even after deadline and urgent notice");
        CHECK(!schedule.reserve(t0 + 100ms, epoch1).has_value());

        INFO("outstanding remains true");
        CHECK(schedule.outstanding());
    }

    // Epoch change while outstanding: passing a new ticket/epoch does not clear an active reservation.
    {
        TaskSchedule schedule(t0);
        const auto first = schedule.reserve(t0 + 50ms, epoch1);
        INFO("first reservation acquired for epoch 1");
        REQUIRE(first.has_value());
        CHECK(schedule.outstanding());

        const auto second = schedule.reserve(t0 + 50ms, epoch2);
        INFO("new epoch ticket cannot acquire slot while previous task is outstanding");
        CHECK(!second.has_value());

        INFO("outstanding remains true after new epoch ticket attempt");
        CHECK(schedule.outstanding());

        schedule.complete();
        auto third = schedule.reserve(t0 + 100ms, epoch2);
        INFO("new epoch reserves cleanly after completion");
        REQUIRE(third.has_value());
        CHECK(require_value(third).ticket.epoch == epoch2.epoch);
    }

    // Completion and subsequent reservation timing after complete().
    {
        TaskSchedule schedule(t0);
        const auto first = schedule.reserve(t0 + 50ms, epoch1);
        INFO("initial reservation at deadline");
        REQUIRE(first.has_value());

        schedule.complete();
        INFO("complete clears outstanding");
        CHECK(!schedule.outstanding());

        INFO("immediate reserve before next interval is rejected");
        CHECK(!schedule.reserve(t0 + 51ms, epoch1).has_value());

        INFO("reserve just before next interval is rejected");
        CHECK(!schedule.reserve(t0 + 99ms, epoch1).has_value());

        auto second = schedule.reserve(t0 + 100ms, epoch1);
        INFO("reserve exactly at next interval succeeds");
        REQUIRE(second.has_value());
        CHECK(!require_value(second).urgent);
    }

    // Redundant complete: calling complete() when idle is idempotent.
    {
        TaskSchedule schedule(t0);
        INFO("initially not outstanding");
        CHECK(!schedule.outstanding());

        schedule.complete();
        INFO("complete on idle schedule leaves outstanding false");
        CHECK(!schedule.outstanding());

        const auto res = schedule.reserve(t0 + 50ms, epoch1);
        INFO("reserve after idle complete succeeds at deadline");
        REQUIRE(res.has_value());

        schedule.complete();
        INFO("first complete clears outstanding");
        CHECK(!schedule.outstanding());

        schedule.complete();
        schedule.complete();
        INFO("multiple complete calls leave outstanding false");
        CHECK(!schedule.outstanding());

        const auto next_res = schedule.reserve(t0 + 100ms, epoch1);
        INFO("subsequent reserve after redundant completes succeeds");
        CHECK(next_res.has_value());
    }

    // Urgent requests: urgent_pending() semantics and coalescing when the slot is busy.
    {
        TaskSchedule schedule(t0);
        auto first = schedule.reserve(t0 + 50ms, epoch1);
        INFO("first reservation was periodic");
        REQUIRE(first.has_value());
        CHECK(!require_value(first).urgent);

        schedule.notify_urgent();
        schedule.notify_urgent();
        schedule.notify_urgent();
        INFO("multiple urgent notices coalesce into pending state");
        CHECK(schedule.urgent_pending());

        schedule.complete();
        auto second = schedule.reserve(t0 + 51ms, epoch1);
        INFO("coalesced urgency dispatched immediately after complete before periodic interval");
        REQUIRE(second.has_value());
        CHECK(require_value(second).urgent);

        INFO("urgency consumed by reservation");
        CHECK(!schedule.urgent_pending());

        schedule.complete();
        INFO("no urgency remains, sub-interval reserve rejected");
        CHECK(!schedule.reserve(t0 + 52ms, epoch1).has_value());
    }

    // Uncompleted reservation blocks subsequent reserves across advancing time.
    {
        TaskSchedule schedule(t0);
        const auto first = schedule.reserve(t0 + 50ms, epoch1);
        INFO("task reserved");
        REQUIRE(first.has_value());

        INFO("+100ms without complete blocked");
        CHECK(!schedule.reserve(t0 + 100ms, epoch1).has_value());

        INFO("+1s without complete blocked");
        CHECK(!schedule.reserve(t0 + 1s, epoch1).has_value());

        INFO("+60s without complete blocked");
        CHECK(!schedule.reserve(t0 + 60s, epoch1).has_value());

        INFO("+1h without complete blocked");
        CHECK(!schedule.reserve(t0 + 1h, epoch1).has_value());

        INFO("outstanding remains true indefinitely if complete is never called");
        CHECK(schedule.outstanding());
    }
}

TEST_CASE("skyrim.schedule.output_wake_does_not_reserve_or_resample",
    "[component][skyrim][schedule]") {
    using namespace std::chrono_literals;
    using namespace skyrim_adapter;
    using namespace skyrim_adapter::native;

    const auto start = MonoTime{300s};
    const NativeTicket ticket{20};
    TaskSchedule schedule(start);

    INFO("no data-loaded ticket or output leaves the worker idle");
    CHECK(!schedule.wake_pending(false));

    INFO("no ticket cannot reserve a native task");
    CHECK(!schedule.reserve(start + 1ms, std::nullopt).has_value());

    schedule.notify_output();
    INFO("output wakes the worker before native admission");
    CHECK(schedule.wake_pending(false));

    INFO("output wake is independent of ticket admission");
    CHECK(schedule.wake_pending(true));

    INFO("output-only wake does not reserve a native sampling task");
    CHECK(!schedule.reserve(start + 1ms, ticket).has_value());

    INFO("worker can consume an output-only wake");
    CHECK(schedule.consume_output_wake());

    INFO("consuming output wake returns the worker to idle");
    CHECK(!schedule.wake_pending(false));

    INFO("output delivery does not create an immediate resampling loop");
    CHECK(!schedule.reserve(start + 2ms, ticket).has_value());

    INFO("ordinary sampling still follows its deadline");
    CHECK(schedule.reserve(start + 50ms, ticket).has_value());
}

TEST_CASE("skyrim.schedule.output_wake_while_task_is_outstanding",
    "[component][skyrim][schedule]") {
    using namespace std::chrono_literals;
    using namespace skyrim_adapter;
    using namespace skyrim_adapter::native;

    const auto start = MonoTime{400s};
    const NativeTicket ticket{21};
    TaskSchedule schedule(start);

    INFO("native task can reserve before output wake");
    CHECK(schedule.reserve(start + 50ms, ticket).has_value());

    schedule.notify_output();
    INFO("output wake remains visible with a native task outstanding");
    CHECK(schedule.wake_pending(true));

    INFO("output wake cannot reserve a second native task");
    CHECK(!schedule.reserve(start + 51ms, ticket).has_value());

    INFO("worker can consume output wake while native task is outstanding");
    CHECK(schedule.consume_output_wake());

    INFO("consuming output wake leaves no redundant worker wake");
    CHECK(!schedule.wake_pending(true));

    schedule.complete();
    INFO("task completion independently wakes output processing");
    CHECK(schedule.wake_pending(false));
}

TEST_CASE("skyrim.schedule.completion_publishes_output_wake", "[component][skyrim][schedule]") {
    using namespace std::chrono_literals;
    using namespace skyrim_adapter;
    using namespace skyrim_adapter::native;

    const auto start = MonoTime{500s};
    const NativeTicket ticket{22};
    TaskSchedule schedule(start);

    INFO("native task reserves for completion wake test");
    CHECK(schedule.reserve(start + 50ms, ticket).has_value());

    schedule.complete();
    INFO("completion releases the one-task reservation");
    CHECK(!schedule.outstanding());

    INFO("completion marks output processing pending without a ticket");
    CHECK(schedule.wake_pending(false));

    INFO("worker consumes completion output wake");
    CHECK(schedule.consume_output_wake());

    INFO("completion wake is coalesced after consumption");
    CHECK(!schedule.wake_pending(false));

    INFO("completion does not request another native sample");
    CHECK(!schedule.reserve(start + 51ms, ticket).has_value());

    INFO("next sample still waits for the next deadline");
    CHECK(schedule.reserve(start + 100ms, ticket).has_value());
}

TEST_CASE("skyrim.schedule.native_urgency_still_reserves_one_task",
    "[component][skyrim][schedule]") {
    using namespace std::chrono_literals;
    using namespace skyrim_adapter;
    using namespace skyrim_adapter::native;

    const auto start = MonoTime{600s};
    const NativeTicket ticket{23};
    TaskSchedule schedule(start);
    schedule.notify_urgent();

    INFO("native urgency requests an output and sampling wake");
    CHECK(schedule.wake_pending(true));

    INFO("worker consumes urgency output wake before ticking");
    CHECK(schedule.consume_output_wake());

    auto first = schedule.reserve(start + 1ms, ticket);
    INFO("native urgency still reserves one immediate native task");
    REQUIRE(first.has_value());
    CHECK(require_value(first).urgent);

    schedule.notify_urgent();
    INFO("urgent notice cannot bypass the outstanding-task limit");
    CHECK(!schedule.reserve(start + 2ms, ticket).has_value());

    INFO("retained urgency remains visible while the task is outstanding");
    CHECK(schedule.wake_pending(true));

    schedule.complete();
    INFO("completion makes retained urgency dispatchable");
    CHECK(schedule.wake_pending(true));

    INFO("completion output wake is consumed separately");
    CHECK(schedule.consume_output_wake());

    auto second = schedule.reserve(start + 2ms, ticket);
    INFO("retained urgency dispatches after task completion");
    REQUIRE(second.has_value());
    CHECK(require_value(second).urgent);
}

} // namespace skyrim_adapter::tests::schedule
