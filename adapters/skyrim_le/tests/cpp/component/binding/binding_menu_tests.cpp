#include "binding_test_support.hpp"
#include "native/translators.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace skyrim_le::binding_tests {
namespace {

// Menu snapshot boundaries, capacity, invariants, and fail-closed behavior.
void test_menu_snapshot_lifecycle() {
    using namespace skyrim_adapter::native::detail;

    MenuSnapshot snapshot{};
    apply_menu_event(snapshot, "InventoryMenu", true);
    check(snapshot.count == 1 && snapshot.open[0].length == 13, "menu event copies a bounded name");
    check(menu_snapshot_is_reliable(snapshot), "snapshot is reliable with 1 menu");
    apply_menu_event(snapshot, "InventoryMenu", false);
    check(snapshot.count == 0, "menu close removes copied name");
    check(menu_snapshot_is_reliable(snapshot), "snapshot is reliable with 0 menus");

    apply_menu_event(snapshot, "InventoryMenu", true);
    check(snapshot.count == 1, "first open increments count to 1");
    apply_menu_event(snapshot, "InventoryMenu", true);
    check(snapshot.count == 1, "re-opening same menu does not duplicate entry");
    check(menu_snapshot_is_reliable(snapshot), "re-opening remains reliable");

    apply_menu_event(snapshot, "MagicMenu", false);
    check(snapshot.count == 1, "closing absent menu leaves snapshot count unchanged");
    check(menu_snapshot_is_reliable(snapshot), "closing absent menu remains reliable");

    apply_menu_event(snapshot, "InventoryMenu", false);
    check(snapshot.count == 0, "closing open menu empties snapshot");
    apply_menu_event(snapshot, "InventoryMenu", false);
    check(snapshot.count == 0, "repeat close of already closed menu is a safe no-op");
    check(menu_snapshot_is_reliable(snapshot), "repeat close remains reliable");
}

void test_menu_snapshot_name_boundaries() {
    using namespace skyrim_adapter::native::detail;

    MenuSnapshot snapshot{};
    apply_menu_event(snapshot, nullptr, true);
    check(snapshot.unreliable && !menu_snapshot_is_reliable(snapshot),
        "nullptr open name fails closed");

    snapshot = {};
    apply_menu_event(snapshot, "", true);
    check(snapshot.unreliable && !menu_snapshot_is_reliable(snapshot),
        "empty open name fails closed");

    snapshot = {};
    apply_menu_event(snapshot, nullptr, false);
    check(snapshot.unreliable && !menu_snapshot_is_reliable(snapshot),
        "nullptr close name fails closed");

    snapshot = {};
    apply_menu_event(snapshot, "", false);
    check(snapshot.unreliable && !menu_snapshot_is_reliable(snapshot),
        "empty close name fails closed");

    // Maximum allowed menu name length (menu_name_capacity - 1 = 127 bytes).
    snapshot = {};
    std::array<char, menu_name_capacity> max_name{};
    max_name.fill('a');
    max_name.back() = '\0'; // 127 characters + NUL

    apply_menu_event(snapshot, max_name.data(), true);
    check(snapshot.count == 1, "menu name at maximum capacity (127 chars) is accepted");
    check(snapshot.open[0].length == 127, "name length recorded as 127");
    check(menu_snapshot_is_reliable(snapshot), "snapshot with maximum length name is reliable");
    apply_menu_event(snapshot, max_name.data(), false);
    check(snapshot.count == 0, "closing maximum length menu succeeds");

    // Exceeding allowed menu name length (>= 128 bytes).
    snapshot = {};
    std::array<char, menu_name_capacity + 1> name_128{};
    name_128.fill('b');
    name_128.back() = '\0'; // exactly 128 characters + NUL
    apply_menu_event(snapshot, name_128.data(), true);
    check(snapshot.unreliable && !menu_snapshot_is_reliable(snapshot),
        "128-char menu name fails closed");

    snapshot = {};
    std::array<char, 201> name_200{};
    name_200.fill('c');
    name_200.back() = '\0';
    apply_menu_event(snapshot, name_200.data(), true);
    check(snapshot.unreliable && !menu_snapshot_is_reliable(snapshot),
        "200-char menu name fails closed");
}

void test_menu_snapshot_capacity() {
    using namespace skyrim_adapter::native::detail;

    MenuSnapshot snapshot{};
    std::array<std::array<char, 16>, open_menu_capacity> names{};
    for (std::size_t i = 0; i < open_menu_capacity; ++i) {
        names[i][0] = 'M';
        names[i][1] = static_cast<char>('A' + (i / 10));
        names[i][2] = static_cast<char>('0' + (i % 10));
        names[i][3] = '\0';
        apply_menu_event(snapshot, names[i].data(), true);
    }

    check(snapshot.count == open_menu_capacity, "all 32 menus are added to snapshot");
    check(menu_snapshot_is_reliable(snapshot), "snapshot at full capacity remains reliable");

    apply_menu_event(snapshot, names[0].data(), true);
    check(snapshot.count == open_menu_capacity,
        "re-opening existing menu at capacity keeps count at 32");
    check(menu_snapshot_is_reliable(snapshot),
        "re-opening existing menu at capacity remains reliable");

    apply_menu_event(snapshot, "OverflowMenu", true);
    check(snapshot.unreliable, "adding beyond open_menu_capacity marks snapshot unreliable");
    check(!menu_snapshot_is_reliable(snapshot), "snapshot over capacity fails closed");
    check(snapshot.count == open_menu_capacity,
        "count remains bounded at capacity after overflow attempt");
}

void test_menu_snapshot_fail_closed() {
    using namespace skyrim_adapter::native::detail;

    MenuSnapshot snapshot{};
    snapshot.count = open_menu_capacity + 1;
    check(!menu_snapshot_is_reliable(snapshot),
        "snapshot count beyond fixed capacity fails closed");

    apply_menu_event(snapshot, "SafeMenu", true);
    check(snapshot.unreliable, "apply_menu_event on invalid count marks unreliable");
    check(!menu_snapshot_is_reliable(snapshot), "invalid count remains unreliable after open");

    snapshot = {};
    snapshot.count = 1000;
    apply_menu_event(snapshot, "SafeMenu", false);
    check(snapshot.unreliable, "apply_menu_event close on extreme count marks unreliable");

    // Once marked unreliable, the snapshot remains unreliable across events
    // until a session boundary reset, but close events continue retiring tracked menus.
    snapshot = {};
    apply_menu_event(snapshot, "RetiringMenu", true);
    check(snapshot.count == 1, "menu added to snapshot");
    apply_menu_event(snapshot, nullptr, true);
    check(snapshot.unreliable, "unreliable triggered by bad event");
    check(!menu_snapshot_is_reliable(snapshot), "snapshot is unreliable");

    apply_menu_event(snapshot, "RetiringMenu", false);
    check(snapshot.count == 0, "close event retires menu even when snapshot is unreliable");
    check(snapshot.unreliable,
        "snapshot remains unreliable after close until engine session boundary");
    check(!menu_snapshot_is_reliable(snapshot), "snapshot remains unreliable");

    apply_menu_event(snapshot, "ValidMenu", true);
    check(snapshot.unreliable, "snapshot remains unreliable after subsequent valid open");
    check(!menu_snapshot_is_reliable(snapshot), "snapshot remains unreliable");

    apply_menu_event(snapshot, "ValidMenu", false);
    check(snapshot.unreliable, "snapshot remains unreliable after subsequent valid close");
    check(!menu_snapshot_is_reliable(snapshot), "snapshot remains unreliable after close");

    // Capacity overflow invariant: an untracked menu opening beyond capacity marks the snapshot
    // permanently unreliable, preventing premature unsuppression when tracked menus close.
    snapshot = {};
    std::array<std::array<char, 16>, open_menu_capacity> batch_names{};
    for (std::size_t i = 0; i < open_menu_capacity; ++i) {
        batch_names[i][0] = 'K';
        batch_names[i][1] = static_cast<char>('A' + (i / 10));
        batch_names[i][2] = static_cast<char>('0' + (i % 10));
        batch_names[i][3] = '\0';
        apply_menu_event(snapshot, batch_names[i].data(), true);
    }
    check(snapshot.count == open_menu_capacity, "filled snapshot to capacity (32)");
    check(menu_snapshot_is_reliable(snapshot), "snapshot at capacity is reliable");

    apply_menu_event(snapshot, "ModUnknownBlockingMenu", true);
    check(snapshot.unreliable, "33rd menu triggers unreliable state");
    check(!menu_snapshot_is_reliable(snapshot), "snapshot fails closed on overflow");

    for (std::size_t i = 0; i < open_menu_capacity; ++i) {
        apply_menu_event(snapshot, batch_names[i].data(), false);
    }
    check(snapshot.count == 0, "all 32 tracked menus are now closed");
    check(snapshot.unreliable, "snapshot remains unreliable even when count reaches 0");
    check(!menu_snapshot_is_reliable(snapshot),
        "fail-closed: untracked 33rd menu prevents premature unsuppression");

    // Closing an untracked menu does not corrupt state.
    apply_menu_event(snapshot, "NeverOpenedMenu", false);
    check(snapshot.count == 0, "closing untracked menu leaves count at 0");
    check(snapshot.unreliable, "unreliable state preserved");

    // Session boundary reset (PreLoadGame / NewGame) clears unreliable state.
    snapshot = {};
    check(menu_snapshot_is_reliable(snapshot),
        "session boundary reset cleanly restores reliability");
}

// Menu classification flags, instances, and reasons.
void test_menu_classification() {
    using namespace skyrim_adapter::native::detail;

    check(classify_menu(true, true, le_menu_pause_game | le_menu_show_cursor).reason ==
              MenuBlockReason::pause_and_cursor,
        "open pause and cursor menu classifies as pause_and_cursor");
    check(classify_menu(true, true, le_menu_pause_game | le_menu_show_cursor).blocks_gameplay,
        "open pause and cursor menu blocks gameplay");

    check(classify_menu(true, true, le_menu_pause_game).reason == MenuBlockReason::pause,
        "open pause menu blocks gameplay with pause reason");
    check(classify_menu(true, true, le_menu_show_cursor).reason == MenuBlockReason::cursor,
        "open cursor menu blocks gameplay with cursor reason");

    check(classify_menu(true, true, le_menu_pause_game | le_menu_do_not_delete_on_close)
              .blocks_gameplay,
        "pause menu blocks with persistence flag");
    check(classify_menu(true, true, le_menu_show_cursor | le_menu_do_not_delete_on_close)
              .blocks_gameplay,
        "cursor menu blocks with persistence flag");

    check(classify_menu(true, false, 0).reason == MenuBlockReason::missing_instance,
        "open menu without an instance remains blocking");
    check(classify_menu(true, false, 0).blocks_gameplay,
        "open menu without an instance blocks gameplay");

    // Semantic distinction: menu_blocks_gameplay() == false does not imply IsMenuOpen() == false;
    // passive UI/HUD elements can be legitimately open without blocking gameplay inputs.
    const auto open_hud = classify_menu(true, true, 0x18902);
    check(!open_hud.blocks_gameplay, "normal HUD flags do not block gameplay");
    check(open_hud.reason == MenuBlockReason::clear, "open HUD menu reason is clear");

    check(!classify_menu(true, true, le_menu_do_not_delete_on_close).blocks_gameplay,
        "persistence-only menu flag does not block gameplay");
    check(!classify_menu(true, true, 0x20).blocks_gameplay,
        "neutral menu flags do not block gameplay");
    check(!classify_menu(true, true, 0).blocks_gameplay, "zero flag menu does not block gameplay");

    check(classify_menu(false, false, le_menu_pause_game | le_menu_show_cursor).reason ==
              MenuBlockReason::clear,
        "closed stale snapshot is clear without an instance regardless of flags");
    check(!classify_menu(false, false, le_menu_pause_game | le_menu_show_cursor).blocks_gameplay,
        "closed menu without instance does not block gameplay");
    check(classify_menu(false, true, le_menu_pause_game).reason == MenuBlockReason::clear,
        "closed menu with a stale instance is clear");
    check(!classify_menu(false, true, le_menu_pause_game).blocks_gameplay,
        "closed menu with instance does not block gameplay");
}

void test_menu_immediate_suppression_and_lifecycle_transitions() {
    using namespace skyrim_adapter::native::detail;

    // Immediate suppression on capacity overflow (unknown 33rd menu).
    {
        MenuSnapshot snapshot{};

        // Fill up to 32 with unknown non-blocking menus
        std::array<std::array<char, 16>, open_menu_capacity> batch_names{};
        for (std::size_t i = 0; i < open_menu_capacity; ++i) {
            batch_names[i][0] = 'N';
            batch_names[i][1] = static_cast<char>('A' + (i / 10));
            batch_names[i][2] = static_cast<char>('0' + (i % 10));
            batch_names[i][3] = '\0';
            apply_menu_event(snapshot, batch_names[i].data(), true);
            check(!menu_event_requires_immediate_suppression(snapshot, batch_names[i].data(), true),
                "32 non-blocking menus do not require immediate suppression");
        }

        // 33rd unknown menu overflows capacity, marking snapshot unreliable and requiring immediate suppression.
        apply_menu_event(snapshot, "Unknown33rdMenu", true);
        check(snapshot.unreliable, "33rd menu marks snapshot unreliable");
        check(menu_event_requires_immediate_suppression(snapshot, "Unknown33rdMenu", true),
            "overflow by unknown 33rd menu directly triggers immediate suppression in production "
            "code");

        // Even close events while unreliable must still evaluate as requiring suppression
        check(menu_event_requires_immediate_suppression(snapshot, batch_names[0].data(), false),
            "subsequent events while unreliable consistently require suppression");
    }

    // Immediate suppression on string boundary violations (length overflow or null pointer).
    {
        MenuSnapshot snapshot{};
        std::array<char, 130> long_name{};
        long_name.fill('x');
        long_name.back() = '\0';

        apply_menu_event(snapshot, long_name.data(), true);
        check(snapshot.unreliable, "129-char name marks snapshot unreliable");
        check(menu_event_requires_immediate_suppression(snapshot, long_name.data(), true),
            "production predicate requires immediate suppression on name boundary error");

        snapshot = {};
        apply_menu_event(snapshot, nullptr, true);
        check(snapshot.unreliable, "nullptr name marks snapshot unreliable");
        check(menu_event_requires_immediate_suppression(snapshot, "", true),
            "production predicate requires immediate suppression on nullptr error");
    }

    // Immediate suppression behavior for known blocking menus.
    {
        MenuSnapshot snapshot{};
        apply_menu_event(snapshot, "InventoryMenu", true);
        check(menu_event_requires_immediate_suppression(snapshot, "InventoryMenu", true),
            "opening known blocking menu requires immediate suppression");
        check(!menu_event_requires_immediate_suppression(snapshot, "InventoryMenu", false),
            "closing known blocking menu does not require immediate suppression (deferred to "
            "resync)");
    }

    // Simplified state-transition model illustrating expected lifecycle rejection logic
    // (pre-load epoch advance and menu revision invalidation).
    // Note: this is a local model, not a direct test of NativeOwner::on_message or runtime synchronization.
    // Multithreaded coordinator interleaving is tested in
    // adapters/skyrim/tests/cpp/component/session/coordinator_interleaving_tests.cpp.
    {
        std::uint64_t coordinator_epoch = 1;
        bool coordinator_load_ready = true;
        std::uint64_t menu_revision = 1;

        // In-flight task samples snapshot and revision under epoch 1.
        const std::uint64_t task_ticket_epoch = coordinator_epoch;
        const std::uint64_t task_read_revision = menu_revision;

        // Simulate pre_load transition: epoch advances, load readiness cleared, menu revision bumped.
        coordinator_load_ready = false;
        ++coordinator_epoch;
        ++menu_revision;

        // Task attempting to reconcile menu state is rejected due to revision mismatch.
        const bool update_allowed = (task_read_revision == menu_revision);
        check(!update_allowed, "in-flight task cannot update menu state across revision boundary");

        // Task attempting to publish is rejected due to epoch mismatch and cleared load readiness.
        const bool publish_allowed =
            (task_ticket_epoch == coordinator_epoch && coordinator_load_ready);
        check(!publish_allowed, "in-flight task cannot publish stale capture after pre_load");
    }
}

} // namespace

void run_menu_tests() {
    test_menu_snapshot_lifecycle();
    test_menu_snapshot_name_boundaries();
    test_menu_snapshot_capacity();
    test_menu_snapshot_fail_closed();
    test_menu_classification();
    test_menu_immediate_suppression_and_lifecycle_transitions();
}
} // namespace skyrim_le::binding_tests
