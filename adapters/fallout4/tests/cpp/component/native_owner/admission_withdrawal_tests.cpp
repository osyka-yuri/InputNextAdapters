#include "cpp/component/native_ui_capture/ui_capture_test.hpp"
#include "native/ui_capture.hpp"
#include "support.hpp"

#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/session.hpp>

#include <RE/I/INPUT_DEVICE.h>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <chrono>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#undef FAIL

namespace fallout4_adapter::native_owner_test {
using namespace fallout4_adapter;
using namespace fallout4_adapter::native;

TEST_CASE("fallout4.native_owner.owner_publication_admission",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    constexpr std::uint64_t generation = 100;
    configure_owner_hooks(generation);
    ui_capture_test::set_remap_original(&owner_remap_original);

    const auto owner_storage = make_test_owner();
    auto& owner = *owner_storage;
    const auto ticket = prepare_owner(owner, generation, std::chrono::steady_clock::now());
    check(ticket.has_value(),
        "same-generation lifecycle controls open the existing session ticket");
    if (!ticket) {
        return;
    }

    supply_observation({.weapon_identity = 17,
        .player_handle = 44,
        .player_address = 0x12345678U,
        .player_form_id = 0xAABBCCDDU,
        .set_triggers = true,
        .sprinting = true});
    schedule_and_run(owner, *ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    check(NativeOwnerTestAccess::has_cached_player(owner),
        "actual Main observation publishes a same-generation scalar cache");
    check(!NativeOwnerTestAccess::has_sample(owner),
        "actual inbox drain consumes the first task sample");

    schedule_and_run(owner, *ticket, generation);
    const auto cached = NativeOwnerTestAccess::callback_player(owner);
    check(cached.has_value(),
        "actual callback-player admission accepts matching lifecycle and UI generations");
    if (cached) {
        check(cached->sprinting,
            "the admitted Main snapshot preserves sprint state in the callback cache");
        PolicyEvent event{};
        event.kind = EventKind::shot;
        event.arrived_at = std::chrono::steady_clock::now();
        event.weapon_identity = cached->weapon_identity;
        check(
            NativeOwnerTestAccess::queue_event(owner, cached->epoch, cached->ui_generation, event),
            "actual owner callback event queue admits the matching cache tuple");
    }
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    const auto& live_output = NativeOwnerTestAccess::output(owner);
    check(live_output.target_calls >= 2,
        "actual sample publication reconciles the existing output binding");
    check(NativeOwnerTestAccess::session(owner).diagnostics().accepted_events != 0,
        "actual callback event drains into the existing Session admission path");
}

TEST_CASE("fallout4.native_owner.owner_focus_withdrawal_recovery",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    constexpr std::uint64_t generation = 101;
    configure_owner_hooks(generation);

    const auto owner_storage = make_test_owner();
    auto& owner = *owner_storage;
    const auto ticket = prepare_owner(owner, generation, std::chrono::steady_clock::now());
    check(ticket.has_value(), "focus fixture opens its lifecycle ticket");
    if (!ticket) {
        return;
    }
    supply_observation({.weapon_identity = 27,
        .player_handle = 54,
        .player_address = 0x22345678U,
        .player_form_id = 0xAABBCCDEU,
        .set_triggers = true});

    schedule_and_run(owner, *ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    schedule_and_run(owner, *ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    check(NativeOwnerTestAccess::has_cached_player(owner),
        "focus fixture publishes a valid callback cache");
    check(queue_shot(owner, NativeOwnerTestAccess::callback_player(owner)),
        "focus fixture first admits an event to establish live output");
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();

    schedule_and_run(owner, *ticket, generation);
    check(NativeOwnerTestAccess::has_sample(owner),
        "actual Main observation retains a pending sample before focus loss");
    check(queue_shot(owner, NativeOwnerTestAccess::callback_player(owner)),
        "focus fixture queues an event for pending work");
    const auto accepted_before_focus =
        NativeOwnerTestAccess::session(owner).diagnostics().accepted_events;
    const auto clears_before_focus = NativeOwnerTestAccess::output(owner).trigger_clear_calls;
    check(NativeOwnerTestAccess::focus(owner, std::chrono::steady_clock::now(), false),
        "actual worker focus path detects focus loss");
    check(!NativeOwnerTestAccess::callback_player(owner),
        "focus loss closes callback cache admission before drain");
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    check(!NativeOwnerTestAccess::has_sample(owner),
        "focus-invalidated pending sample is drained without publication");
    check(NativeOwnerTestAccess::session(owner).diagnostics().accepted_events ==
              accepted_before_focus,
        "focus invalidation drops the queued event without increasing Session accepted-events");
    check(NativeOwnerTestAccess::output(owner).trigger_clear_calls > clears_before_focus,
        "actual Session withdrawal clears an admitted trigger after focus loss");

    check(NativeOwnerTestAccess::focus(owner, std::chrono::steady_clock::now(), true),
        "focus recovery reopens output observation demand");
    const auto recovery_ticket = NativeOwnerTestAccess::session(owner).ticket();
    check(recovery_ticket.has_value(), "focus recovery yields a fresh owner task epoch");
    if (!recovery_ticket) {
        return;
    }
    schedule_and_run(owner, *recovery_ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    check(NativeOwnerTestAccess::callback_player(owner).has_value(),
        "a fresh focused observation restores callback admission");
    check(NativeOwnerTestAccess::output(owner).target_calls > 0,
        "fresh focused observation recovers the existing output binding");
}

TEST_CASE("fallout4.native_owner.owner_lifecycle_invalidation_recovery",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    constexpr std::uint64_t generation = 111;
    configure_owner_hooks(generation);

    const auto owner_storage = make_test_owner();
    auto& owner = *owner_storage;
    const auto initial_ticket = prepare_owner(owner, generation, std::chrono::steady_clock::now());
    check(initial_ticket.has_value(), "lifecycle fixture opens its initial ticket");
    if (!initial_ticket) {
        return;
    }
    supply_observation({.weapon_identity = 37,
        .player_handle = 64,
        .player_address = 0x32345678U,
        .player_form_id = 0xAABBCCDFU});
    schedule_and_run(owner, *initial_ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    schedule_and_run(owner, *initial_ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    check(NativeOwnerTestAccess::has_cached_player(owner),
        "lifecycle fixture starts with a published callback cache");
    check(queue_shot(owner, NativeOwnerTestAccess::callback_player(owner)),
        "lifecycle fixture admits an event before preload");
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();

    const auto now = std::chrono::steady_clock::now();
    NativeOwnerTestAccess::queue_control(owner, Control::pre_load, now);
    NativeOwnerTestAccess::process_pending(owner);
    check(!NativeOwnerTestAccess::session(owner).ticket(),
        "actual lifecycle drain closes tickets during preload");
    NativeOwnerTestAccess::queue_control(owner, Control::load_succeeded, now + 1ms);
    NativeOwnerTestAccess::process_pending(owner);
    check(NativeOwnerTestAccess::session(owner).ticket().has_value(),
        "actual lifecycle drain reopens tickets after load");

    const auto lifecycle_ticket = NativeOwnerTestAccess::session(owner).ticket();
    check(lifecycle_ticket.has_value(), "load recovery yields a fresh owner task epoch");
    if (!lifecycle_ticket) {
        return;
    }
    schedule_and_run(owner, *lifecycle_ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    schedule_and_run(owner, *lifecycle_ticket, generation);
    check(NativeOwnerTestAccess::has_sample(owner),
        "lifecycle fixture retains a sample before preload invalidation");
    check(queue_shot(owner, NativeOwnerTestAccess::callback_player(owner)),
        "lifecycle fixture queues an event against the current published cache");
    const auto accepted_before_preload =
        NativeOwnerTestAccess::session(owner).diagnostics().accepted_events;
    NativeOwnerTestAccess::queue_control(owner, Control::pre_load, now + 3ms);
    NativeOwnerTestAccess::process_pending(owner);
    check(!NativeOwnerTestAccess::session(owner).ticket(),
        "preload closes the ticket before retired event admission");
    check(NativeOwnerTestAccess::session(owner).diagnostics().accepted_events ==
              accepted_before_preload,
        "lifecycle invalidation drops pending sample and event without Session admission");
    check(!NativeOwnerTestAccess::has_sample(owner), "preload drain consumes its retired sample");

    NativeOwnerTestAccess::queue_control(owner, Control::load_succeeded, now + 4ms);
    NativeOwnerTestAccess::process_pending(owner);
    const auto rebound_ticket = NativeOwnerTestAccess::session(owner).ticket();
    check(rebound_ticket.has_value(),
        "load completion reopens the lifecycle after pending-work invalidation");
    if (!rebound_ticket) {
        return;
    }
    schedule_and_run(owner, *rebound_ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    schedule_and_run(owner, *rebound_ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    check(NativeOwnerTestAccess::has_cached_player(owner),
        "same-generation tasks republish the owner cache after load");
}

TEST_CASE("fallout4.native_owner.owner_generation_retirement_recovery",
    "[component][fallout4][native_owner]") {
    Environment const environment;
    constexpr std::uint64_t first_generation = 121;
    constexpr std::uint64_t rebound_generation = first_generation + 1U;
    configure_owner_hooks(first_generation);
    ui_capture_test::set_remap_original(&owner_remap_original);

    const auto owner_storage = make_test_owner();
    auto& owner = *owner_storage;
    const auto ticket = prepare_owner(owner, first_generation, std::chrono::steady_clock::now());
    check(ticket.has_value(), "generation fixture opens its lifecycle ticket");
    if (!ticket) {
        return;
    }
    supply_observation({.weapon_identity = 47,
        .player_handle = 74,
        .player_address = 0x42345678U,
        .player_form_id = 0xAABBCCE0U,
        .set_triggers = true});

    schedule_and_run(owner, *ticket, first_generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    schedule_and_run(owner, *ticket, first_generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    const auto prior_target_calls = NativeOwnerTestAccess::output(owner).target_calls;
    schedule_and_run(owner, *ticket, first_generation);
    check(NativeOwnerTestAccess::has_sample(owner),
        "generation fixture retains a published sample before rebind");
    const auto retained_cache = NativeOwnerTestAccess::callback_player(owner);
    check(retained_cache.has_value(),
        "generation fixture starts with a matching published callback cache");
    check(queue_shot(owner, retained_cache),
        "generation fixture retains an event against the published cache");
    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch,
            .generation = first_generation});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, first_generation),
        "owner retains one delayed old-generation Main observation request");
    const auto old_request_id = NativeOwnerTestAccess::request_id(owner);

    const auto accepted_before_rebind =
        NativeOwnerTestAccess::session(owner).diagnostics().accepted_events;
    const auto& remap_event = empty_remap_event_argument();
    check(ui_capture_test::invoke_remap(fake_control_map_address(),
              remap_event,
              RE::INPUT_DEVICE::kGamepad,
              0x2A),
        "public RemapButton wrapper forwards a mapping change while invalidating old owner "
        "provenance");
    check(NativeOwnerTestAccess::has_cached_player(owner),
        "published cache remains present until the real owner reconciliation boundary");
    check(!NativeOwnerTestAccess::callback_player(owner),
        "actual callback admission rejects the old cached tuple before reconciliation");
    if (retained_cache) {
        PolicyEvent stale_event{};
        stale_event.kind = EventKind::shot;
        stale_event.arrived_at = std::chrono::steady_clock::now();
        stale_event.weapon_identity = retained_cache->weapon_identity;
        check(!NativeOwnerTestAccess::queue_event(owner,
                  retained_cache->epoch,
                  retained_cache->ui_generation,
                  stale_event),
            "actual event queue rejects the old epoch and UI generation before reconciliation");
        check(!NativeOwnerTestAccess::queue_event(owner,
                  retained_cache->epoch,
                  rebound_generation,
                  stale_event),
            "actual event queue rejects a falsely upgraded generation while the cached tuple "
            "remains old");
    }
    NativeOwnerTestAccess::process_pending(owner);
    check(!NativeOwnerTestAccess::has_sample(owner),
        "generation-mismatched sample is drained without publication");
    check(NativeOwnerTestAccess::session(owner).diagnostics().accepted_events ==
              accepted_before_rebind,
        "retired queued event drains without increasing Session accepted-events");
    NativeOwnerTestAccess::consume_main(owner);
    check(!NativeOwnerTestAccess::request_pending(owner),
        "stale old-generation Main request is retired before native work");
    NativeOwnerTestAccess::complete(owner, old_request_id);
    check(!NativeOwnerTestAccess::has_sample(owner),
        "delayed old-generation Main request cannot publish after UI rebind");
    check(!NativeOwnerTestAccess::pre_tick(owner, std::chrono::steady_clock::now()),
        "actual pre-tick gate reconciles only after stale callback and event checks");
    check(!NativeOwnerTestAccess::has_cached_player(owner),
        "reconciliation clears the retired cache");
    const auto clears_before_rebind = NativeOwnerTestAccess::output(owner).trigger_clear_calls;
    NativeOwnerTestAccess::session(owner).tick();
    check(NativeOwnerTestAccess::output(owner).trigger_clear_calls > clears_before_rebind,
        "pre-tick generation invalidation withdraws existing output through the native owner "
        "Session");

    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket->epoch,
            .generation = rebound_generation});
    check(NativeOwnerTestAccess::publish(owner, ticket->epoch, rebound_generation),
        "a new-generation worker request is admitted after reconciliation");
    const auto new_request_id = NativeOwnerTestAccess::request_id(owner);
    NativeOwnerTestAccess::complete(owner, old_request_id);
    check(NativeOwnerTestAccess::request_id(owner) == new_request_id,
        "a late old completion cannot erase a newer pending request");
    NativeOwnerTestAccess::consume_main(owner);
    NativeOwnerTestAccess::process_pending(owner);

    const auto current_ticket = NativeOwnerTestAccess::session(owner).ticket();
    check(current_ticket.has_value(), "UI context rebind keeps the loaded owner recoverable");
    if (!current_ticket) {
        return;
    }
    schedule_and_run(owner, *current_ticket, rebound_generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    schedule_and_run(owner, *current_ticket, rebound_generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    check(NativeOwnerTestAccess::output(owner).target_calls >= prior_target_calls,
        "fresh current-generation samples recover the existing output binding after pre-tick "
        "withdrawal");
    check(NativeOwnerTestAccess::callback_player(owner).has_value(),
        "fresh UI generation restores callback admission");
}

TEST_CASE("fallout4.native_owner.owner_inert_shutdown", "[component][fallout4][native_owner]") {
    Environment const environment;
    constexpr std::uint64_t generation = 131;
    configure_owner_hooks(generation);

    const auto owner_storage = make_test_owner();
    auto& owner = *owner_storage;
    auto& output = NativeOwnerTestAccess::output(owner);
    output.query_context_status = INPUT_NEXT_STATUS_PENDING;
    output.query_context_state = INPUT_NEXT_EFFECTS_CONTEXT_ATTACHING;
    const auto ticket = prepare_owner(owner, generation, std::chrono::steady_clock::now());
    check(ticket.has_value(), "shutdown fixture opens its lifecycle ticket");
    if (!ticket) {
        return;
    }
    supply_observation({.weapon_identity = 57,
        .player_handle = 84,
        .player_address = 0x52345678U,
        .player_form_id = 0xAABBCCE1U});
    schedule_and_run(owner, *ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    schedule_and_run(owner, *ticket, generation);
    NativeOwnerTestAccess::process_pending(owner);
    NativeOwnerTestAccess::session(owner).tick();
    check(NativeOwnerTestAccess::callback_player(owner).has_value(),
        "shutdown fixture has active callback admission");

    NativeOwnerTestAccess::enter_inert(owner);
    check(NativeOwnerTestAccess::stopping(owner),
        "actual owner shutdown retains a stopped process-life object");
    check(ui_transition_generation() == 0,
        "owner shutdown permanently closes the native UI generation");
    check(!NativeOwnerTestAccess::callback_player(owner),
        "shutdown rejects later callback admission");
    const auto context = output.current_context;
    NativeOwnerTestAccess::finish_worker(owner);
    check(output.close_calls == 1 && output.ack_close_calls == 1 &&
              output.last_close_request.context.id == context.id &&
              output.last_close_request.close_request_id == context.id &&
              output.last_acked_close_receipt.context.id == context.id &&
              output.last_acked_close_receipt.close_request_id == context.id,
        "the existing owner shutdown phase drains one pending attaching context with its exact "
        "close receipt");
    check(output.trigger_submit_calls == 0 && output.trigger_clear_calls == 0 &&
              output.body_submit_calls == 0 && output.body_clear_calls == 0,
        "pending context shutdown performs no ordinary output mutation or replay");
}

} // namespace fallout4_adapter::native_owner_test
