#include "cpp/component/native_ui_capture/ui_capture_test.hpp"
#include "native/ui_capture.hpp"
#include "support.hpp"

#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/session.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#undef FAIL

namespace fallout4_adapter::native_owner_test {
using namespace std::chrono_literals;
using namespace fallout4_adapter;
using namespace fallout4_adapter::native;

TEST_CASE("fallout4.native_owner.phase_defer_stale_cancel_identity_exhaustion",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    constexpr std::uint64_t generation = 301;
    configure_owner_hooks(generation);

    const auto owner_storage = make_test_owner();
    auto& owner = *owner_storage;
    const auto now = std::chrono::steady_clock::now();
    const auto ticket = prepare_owner(owner, generation, now);
    check(ticket.has_value(), "phase-admission fixture opens its lifecycle ticket");
    if (!ticket) {
        return;
    }
    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch, .generation = generation});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, generation),
        "phase fixture publishes one request");
    check(NativeOwnerTestAccess::request_pending_hint(owner),
        "published request raises the fast pending hint");
    const auto deferred_id = NativeOwnerTestAccess::request_id(owner);
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, generation) &&
              NativeOwnerTestAccess::request_id(owner) == deferred_id,
        "repeat worker demand coalesces into the same pending request identity");
    phase_admission.store(OwnerPhaseAdmission::deferred, std::memory_order_release);
    NativeOwnerTestAccess::consume_main(owner);
    check(NativeOwnerTestAccess::request_pending(owner) &&
              NativeOwnerTestAccess::request_id(owner) == deferred_id,
        "busy gate or UI phase defers the same pending identity");
    check(NativeOwnerTestAccess::request_pending_hint(owner),
        "deferred request remains visible to later callbacks");
    check(observation_provider_calls.load(std::memory_order_relaxed) == 0 &&
              !NativeOwnerTestAccess::has_cached_player(owner),
        "deferred Main phase performs no observation or cache work");
    phase_admission.store(OwnerPhaseAdmission::ready, std::memory_order_release);
    NativeOwnerTestAccess::consume_main(owner);
    check(!NativeOwnerTestAccess::request_pending(owner) &&
              observation_provider_calls.load(std::memory_order_relaxed) == 1 &&
              NativeOwnerTestAccess::has_sample(owner),
        "a later admitted Main call reads once, publishes the sample, and completes the deferred "
        "request");
    check(NativeOwnerTestAccess::request_id(owner) == 0,
        "ready Main admission completes the matching request identity");
    check(!NativeOwnerTestAccess::request_pending_hint(owner),
        "completed observation clears the fast pending hint");
    NativeOwnerTestAccess::consume_main(owner);
    check(observation_provider_calls.load(std::memory_order_relaxed) == 1 &&
              NativeOwnerTestAccess::has_sample(owner),
        "a Main callback without a pending request does not reread or replace the published "
        "sample");

    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch, .generation = generation});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, generation),
        "old-generation request is published before transition");
    const auto stale_id = NativeOwnerTestAccess::request_id(owner);
    check(NativeOwnerTestAccess::request_pending_hint(owner),
        "stale request is pending before generation retirement");
    ui_capture_test::set_generation(generation + 1U);
    NativeOwnerTestAccess::consume_main(owner);
    check(!NativeOwnerTestAccess::request_pending(owner),
        "stale generation retires its pending request");
    check(!NativeOwnerTestAccess::request_pending_hint(owner),
        "retiring stale work clears the fast pending hint");
    NativeOwnerTestAccess::complete(owner, stale_id);
    check(NativeOwnerTestAccess::request_id(owner) == 0,
        "late completion of a retired identity is harmless");

    check(NativeOwnerTestAccess::set_worker_generation(owner, generation + 1U),
        "worker generation reconciles to new generation");
    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch,
            .generation = generation + 1U});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, generation + 1U),
        "new generation can publish a new request");
    check(NativeOwnerTestAccess::request_pending_hint(owner),
        "new generation publication raises the pending hint");
    NativeOwnerTestAccess::enter_inert(owner);
    check(!NativeOwnerTestAccess::request_pending(owner) && NativeOwnerTestAccess::stopping(owner),
        "stopped owner cancels pending work and remains inert");
    check(!NativeOwnerTestAccess::request_pending_hint(owner),
        "inert cancellation clears the pending hint");

    ui_capture_test::reset(generation + 2U);
    const auto exhausted_storage =
        make_test_owner(Config{}, nullptr, std::numeric_limits<std::uint64_t>::max());
    auto& exhausted = *exhausted_storage;
    check(NativeOwnerTestAccess::set_worker_generation(exhausted, generation + 2U),
        "worker generation reconciles to exhausted owner generation");
    NativeOwnerTestAccess::publish_task_provenance(exhausted,
        NativeOwnerTestAccess::TaskProvenance{.epoch = 0, .generation = generation + 2U});
    NativeOwnerTestAccess::set_last_request_id(exhausted,
        std::numeric_limits<std::uint64_t>::max());
    check(!NativeOwnerTestAccess::publish(exhausted, 0, generation + 2U) &&
              NativeOwnerTestAccess::stopping(exhausted),
        "request identity exhaustion fails closed outside the inbox lock");
}

TEST_CASE("fallout4.native_owner.empty_main_callback_skips_inbox_lock",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    std::mutex test_inbox_mutex;
    const auto owner_storage = make_test_owner(Config{}, nullptr, 0, &test_inbox_mutex);
    auto& owner = *owner_storage;
    std::atomic<bool> lock_entered{};
    std::atomic<bool> release_lock{};
    std::atomic<bool> callback_returned{};

    struct LockGuard {
        std::atomic<bool>& release;
        std::thread& t1;
        std::thread& t2;

        LockGuard(std::atomic<bool>& rel, std::thread& thread1, std::thread& thread2) noexcept
            : release(rel),
              t1(thread1),
              t2(thread2) {}

        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;
        LockGuard(LockGuard&&) = delete;
        LockGuard& operator=(LockGuard&&) = delete;

        ~LockGuard() {
            release.store(true, std::memory_order_release);
            if (t1.joinable()) {
                t1.join();
            }
            if (t2.joinable()) {
                t2.join();
            }
        }
    };

    std::thread lock_holder([&] {
        std::scoped_lock lock(test_inbox_mutex);
        lock_entered.store(true, std::memory_order_release);
        while (!release_lock.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    });
    std::thread callback;
    LockGuard const guard{release_lock, lock_holder, callback};

    const auto lock_deadline = std::chrono::steady_clock::now() + 2s;
    while (!lock_entered.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < lock_deadline) {
        std::this_thread::yield();
    }
    if (!lock_entered.load(std::memory_order_acquire)) {
        check(false, "test thread acquired the inbox mutex within the bounded wait");
        return;
    }

    callback = std::thread([&] {
        NativeOwnerTestAccess::consume_main(owner);
        callback_returned.store(true, std::memory_order_release);
    });
    const auto callback_deadline = std::chrono::steady_clock::now() + 2s;
    while (!callback_returned.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < callback_deadline) {
        std::this_thread::yield();
    }
    const bool returned_while_locked = callback_returned.load(std::memory_order_acquire);
    release_lock.store(true, std::memory_order_release);
    if (lock_holder.joinable()) {
        lock_holder.join();
    }
    if (callback.joinable()) {
        callback.join();
    }

    check(returned_while_locked,
        "an empty Main callback returns while another thread holds the inbox mutex");
}

TEST_CASE("fallout4.native_owner.reentry_worker_publication_during_execution",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    constexpr std::uint64_t generation = 501;
    configure_owner_hooks(generation);

    const auto owner_storage = make_test_owner();
    auto& owner = *owner_storage;
    const auto ticket = prepare_owner(owner, generation, std::chrono::steady_clock::now());
    check(ticket.has_value(), "reentrant fixture opens its lifecycle ticket");
    if (!ticket) {
        return;
    }
    supply_observation({.weapon_identity = 71,
        .player_handle = 81,
        .player_address = 0x23456789U,
        .player_form_id = 0xAABBCCEEU});
    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch, .generation = generation});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, generation),
        "reentrant fixture publishes a request");
    const auto executing_id = NativeOwnerTestAccess::request_id(owner);
    ReentrantOwnerBinding const reentrant_binding{owner};

    BlockedMainObservation execution{owner};
    if (!BlockedMainObservation::wait_for_entry()) {
        check(false, "reentrant provider enters its blocked Main observation");
        return;
    }
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, generation) &&
              NativeOwnerTestAccess::request_id(owner) == executing_id &&
              !NativeOwnerTestAccess::request_pending(owner) &&
              !NativeOwnerTestAccess::request_pending_hint(owner),
        "worker demand during execution coalesces without replacing the executing identity");
    NativeOwnerTestAccess::process_pending(owner);
    check(NativeOwnerTestAccess::request_id(owner) == executing_id,
        "worker queue processing cannot complete Main-owned execution");
    execution.release_and_join();
    check(observation_provider_calls.load(std::memory_order_relaxed) == 1,
        "reentrant Main entry during an executing request consumes no second request");
    check(NativeOwnerTestAccess::request_id(owner) == 0 &&
              !NativeOwnerTestAccess::request_pending_hint(owner),
        "exact execution completion returns the request state to idle and clears the pending hint");

    NativeOwnerTestAccess::enter_inert(owner);
}

TEST_CASE("fallout4.native_owner.executing_invalidation_preserves_identity_rejects_old_data",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    constexpr std::uint64_t old_generation = 601;
    constexpr std::uint64_t new_generation = old_generation + 1U;
    configure_owner_hooks(old_generation);

    const auto owner_storage = make_test_owner();
    auto& owner = *owner_storage;
    const auto ticket = prepare_owner(owner, old_generation, std::chrono::steady_clock::now());
    check(ticket.has_value(), "executing invalidation fixture opens a lifecycle ticket");
    if (!ticket) {
        return;
    }
    supply_observation({.weapon_identity = 91,
        .player_handle = 92,
        .player_address = 0x34567890U,
        .player_form_id = 0xAABBCCDDU});

    schedule_and_run(owner, *ticket, old_generation);
    NativeOwnerTestAccess::process_pending(owner);
    const auto old_cache = NativeOwnerTestAccess::callback_player(owner);
    check(old_cache.has_value(),
        "invalidation fixture publishes prior valid cache through production Session");
    if (old_cache) {
        PolicyEvent event{};
        event.kind = EventKind::shot;
        event.arrived_at = std::chrono::steady_clock::now();
        event.weapon_identity = old_cache->weapon_identity;
        check(NativeOwnerTestAccess::queue_event(owner,
                  old_cache->epoch,
                  old_cache->ui_generation,
                  event),
            "invalidation fixture queues old-provenance native event");
    }

    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch,
            .generation = old_generation});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, old_generation),
        "executing generation case publishes one old-generation request");
    const auto executing_id = NativeOwnerTestAccess::request_id(owner);
    const auto accepted_events_before =
        NativeOwnerTestAccess::session(owner).diagnostics().accepted_events;
    BlockedMainObservation execution{owner};
    if (!BlockedMainObservation::wait_for_entry()) {
        check(false, "generation-invalidated provider enters its blocked Main observation");
        return;
    }
    ui_capture_test::set_generation(new_generation);
    check(NativeOwnerTestAccess::reconcile_generation(owner, std::chrono::steady_clock::now()),
        "actual worker reconciliation invalidates an executing request on generation change");
    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch,
            .generation = new_generation});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, new_generation) &&
              NativeOwnerTestAccess::request_id(owner) == executing_id,
        "new-generation demand coalesces without replacing the executing identity");
    check(!NativeOwnerTestAccess::callback_player(owner) &&
              !NativeOwnerTestAccess::has_cached_player(owner) &&
              !NativeOwnerTestAccess::has_sample(owner),
        "generation invalidation withdraws old cache and sample while provider work remains "
        "blocked");
    if (old_cache) {
        PolicyEvent stale{};
        stale.kind = EventKind::shot;
        stale.arrived_at = std::chrono::steady_clock::now();
        stale.weapon_identity = old_cache->weapon_identity;
        check(!NativeOwnerTestAccess::queue_event(owner,
                  old_cache->epoch,
                  old_cache->ui_generation,
                  stale),
            "actual event queue rejects stale event provenance during blocked execution");
    }
    execution.release_and_join();
    check(NativeOwnerTestAccess::request_id(owner) == 0,
        "generation-invalidated execution retains its identity until its matching completion");
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    check(NativeOwnerTestAccess::session(owner).diagnostics().accepted_events ==
              accepted_events_before,
        "stale cache/sample/event data never reaches Session admission after executing "
        "invalidation");

    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch,
            .generation = new_generation});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, new_generation),
        "fresh demand can publish after invalidated execution completes");
    NativeOwnerTestAccess::consume_main(owner);
    NativeOwnerTestAccess::process_pending(owner);
    check(NativeOwnerTestAccess::callback_player(owner).has_value() &&
              observation_provider_calls.load(std::memory_order_relaxed) >= 3,
        "fresh current-generation demand resumes provider and cache admission only after "
        "completion");
    NativeOwnerTestAccess::enter_inert(owner);
}

TEST_CASE("fallout4.native_owner.executing_lifecycle_control_invalidation",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    constexpr std::uint64_t generation = 651;
    configure_owner_hooks(generation);

    const auto owner_storage = make_test_owner();
    auto& owner = *owner_storage;
    const auto ticket = prepare_owner(owner, generation, std::chrono::steady_clock::now());
    check(ticket.has_value(), "executing lifecycle fixture opens a ticket");
    if (!ticket) {
        return;
    }
    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch, .generation = generation});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, generation),
        "executing lifecycle fixture publishes one request");
    const auto executing_id = NativeOwnerTestAccess::request_id(owner);
    BlockedMainObservation execution{owner};
    if (!BlockedMainObservation::wait_for_entry()) {
        check(false, "lifecycle-invalidated provider enters its blocked Main observation");
        return;
    }
    check(NativeOwnerTestAccess::request_executing(owner),
        "Main request has entered the executing state");
    NativeOwnerTestAccess::queue_control(owner,
        Control::pre_load,
        std::chrono::steady_clock::now());
    NativeOwnerTestAccess::process_pending(owner);
    check(!NativeOwnerTestAccess::session(owner).ticket(),
        "queued lifecycle control closes the live Session ticket");
    check(NativeOwnerTestAccess::request_executing(owner) &&
              NativeOwnerTestAccess::request_id(owner) == executing_id &&
              NativeOwnerTestAccess::publish(owner, ticket->epoch, generation) &&
              NativeOwnerTestAccess::request_id(owner) == executing_id,
        "lifecycle invalidation does not cancel or replace the blocked execution identity");
    execution.release_and_join();
    check(NativeOwnerTestAccess::request_id(owner) == 0 &&
              !NativeOwnerTestAccess::has_sample(owner) &&
              !NativeOwnerTestAccess::has_cached_player(owner),
        "matching completion retires lifecycle-invalidated work without publishing stale state");
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    check(!NativeOwnerTestAccess::session(owner).ticket(),
        "blocked lifecycle result never reopens the closed Session ticket");
    NativeOwnerTestAccess::enter_inert(owner);
}

TEST_CASE("fallout4.native_owner.executing_focus_invalidation_fresh_resume",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    constexpr std::uint64_t generation = 671;
    configure_owner_hooks(generation);

    const auto owner_storage = make_test_owner();
    auto& owner = *owner_storage;
    const auto ticket = prepare_owner(owner, generation, std::chrono::steady_clock::now());
    check(ticket.has_value(), "executing focus fixture opens a ticket");
    if (!ticket) {
        return;
    }
    supply_observation({.weapon_identity = 101,
        .player_handle = 102,
        .player_address = 0x45678901U,
        .player_form_id = 0xAABBCCE2U});
    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch, .generation = generation});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, generation),
        "executing focus fixture publishes one request");
    const auto executing_id = NativeOwnerTestAccess::request_id(owner);
    BlockedMainObservation execution{owner};
    if (!BlockedMainObservation::wait_for_entry()) {
        check(false, "focus-invalidated provider enters its blocked Main observation");
        return;
    }
    check(NativeOwnerTestAccess::focus(owner, std::chrono::steady_clock::now(), false),
        "actual focus path invalidates work while provider is blocked");
    check(NativeOwnerTestAccess::request_executing(owner) &&
              NativeOwnerTestAccess::request_id(owner) == executing_id &&
              NativeOwnerTestAccess::publish(owner, ticket->epoch, generation) &&
              NativeOwnerTestAccess::request_id(owner) == executing_id,
        "focus invalidation keeps the executing identity stable under repeated demand");
    execution.release_and_join();
    check(NativeOwnerTestAccess::request_id(owner) == 0 &&
              !NativeOwnerTestAccess::has_sample(owner) &&
              !NativeOwnerTestAccess::has_cached_player(owner),
        "focus-invalidated completion publishes no stale sample or callback cache");
    check(NativeOwnerTestAccess::focus(owner, std::chrono::steady_clock::now(), true),
        "fresh focus path reopens output observation demand");
    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch, .generation = generation});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, generation),
        "fresh focus demand publishes only after old execution completion");
    NativeOwnerTestAccess::consume_main(owner);
    NativeOwnerTestAccess::process_pending(owner);
    check(NativeOwnerTestAccess::callback_player(owner).has_value() &&
              observation_provider_calls.load(std::memory_order_relaxed) == 2,
        "freshly focused provider call resumes after the blocked execution completes");
    NativeOwnerTestAccess::enter_inert(owner);
}

} // namespace fallout4_adapter::native_owner_test
