#include "native/observation.hpp"
#include "native/ui_capture.hpp"
#include "native/ui_capture_abi.hpp"
#include "support.hpp"
#include "ui_capture_test.hpp"

#include <catch2/catch_test_macros.hpp>
#undef FAIL

#include <array>
#include <bit>
#include <cstddef>

namespace fallout4_adapter::tests::ui_capture {

TEST_CASE("fallout4.native_ui_capture.ui_capture_static_projection",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state{100};

    ui_capture_test::reset(100);
    ProductionFenceFixture fixture{};
    fixture.generation = 100;
    fixture.initialize_state();
    UiStaticFields projected{};
    check(ui_capture_test::project_static_fields(fixture.image, fixture_static_offsets, projected),
        "production static-field projection reads the owned raw fixture");
    check(ui_capture_test::static_fields_eligible(fixture.image, projected),
        "production static eligibility accepts exact slot, state zero, shutdown zero, and running "
        "one");
    check(ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "current quiescent production tuple is eligible");
    check(!ui_capture_test::eligible(fixture.image, fixture_static_offsets, 101),
        "stale expected generation is rejected");

    fixture.initialize_state({.state = 2});
    check(!ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "startup stale completion state two is rejected");
    fixture.initialize_state({.state = 1});
    check(!ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "state one is rejected while the startup worker has not reached the completed state");
    fixture.initialize_state({.state = 0, .shutdown = 1, .running = 0});
    check(!ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "shutdown and stopped worker state are rejected");
    fixture.initialize_state({.state = 0, .shutdown = 0, .running = 1});
    ui_capture_test::set_in_flight(1);
    check(!ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "an active helper blocks scalar eligibility");
    ui_capture_test::set_in_flight(0);
    check(ui_capture_test::eligible(fixture.image, fixture_static_offsets, 100),
        "reinitialized static state reopens only the current generation");
}

TEST_CASE("fallout4.native_ui_capture.ui_capture_native_lockwords",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state{100};
    ui_capture_test::reset(100);
    ProductionFenceFixture fixture{};
    fixture.generation = 100;
    fixture.initialize_state();
    check(
        ui_capture_test::try_process_lock(fixture.locks.process_words.data(), GetCurrentThreadId()),
        "production process-lock CAS acquires an idle owned lockword");
    check(std::bit_cast<DWORD>(fixture.locks.process_words[0]) == GetCurrentThreadId(),
        "process-lock owner word is published after acquisition");
    check(!ui_capture_test::try_process_lock(fixture.locks.process_words.data(),
              GetCurrentThreadId()),
        "recursive or already-owned process lock declines without spinning");
    ui_capture_test::release_process_lock(fixture.locks.process_words.data());
    check(fixture.locks.process_words[0] == 0 && fixture.locks.process_words[1] == 0,
        "production process-lock release clears owner and lock words");

    check(ui_capture_test::try_read_count(&fixture.locks.menu_read_count),
        "production read-subset CAS acquires an idle count");
    check(fixture.locks.menu_read_count == 1,
        "read-subset acquisition performs one zero-to-one CAS");
    ScopedThread joining_reader{[] {},
        [&] {
            static_cast<void>(InterlockedIncrement(
                reinterpret_cast<volatile LONG*>(&fixture.locks.menu_read_count)));
        }};
    joining_reader.join();
    check(fixture.locks.menu_read_count == 2,
        "an owned joining reader advances the shared count from one to two");
    ui_capture_test::release_read_count(&fixture.locks.menu_read_count);
    check(fixture.locks.menu_read_count == 1,
        "production release drops only its reader from two back to one");
    ui_capture_test::release_read_count(&fixture.locks.menu_read_count);
    check(fixture.locks.menu_read_count == 0,
        "the final production reader release decrements one to zero");
    fixture.locks.menu_read_count = -1;
    check(!ui_capture_test::try_read_count(&fixture.locks.menu_read_count),
        "an owned writer count is declined");
    fixture.locks.menu_read_count = 2;
    check(!ui_capture_test::try_read_count(&fixture.locks.menu_read_count),
        "an existing reader count is declined");
    fixture.locks.menu_read_count = 0;
}

TEST_CASE("fallout4.native_ui_capture.ui_capture_production_fences",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state{100};
    ui_capture_test::reset(100);
    ProductionFenceFixture fixture{};
    fixture.generation = 100;
    fixture.initialize_state();
    check(with_ui_capture_fences(fixture,
              [&] {
                  ++fixture.copy_count;
                  return true;
              }),
        "production fence orchestration runs against the actual process, private gate, and native "
        "lock operations");
    check(fixture.copy_count == 1 && fixture.release_count == 4,
        "production fence tuple copies once and releases all four locks");
    check(fixture.locks.process_words[1] == 0 && fixture.locks.menu_read_count == 0 &&
              fixture.locks.context_read_count == 0,
        "all owned lockword fixtures are free after the production tuple");

    fixture.locks.context_read_count = 1;
    fixture.attempt_count = 0;
    fixture.release_count = 0;
    fixture.copy_count = 0;
    check(!with_ui_capture_fences(fixture,
              [&] {
                  ++fixture.copy_count;
                  return true;
              }),
        "busy context read count declines the production tuple after a single CAS");
    check(fixture.attempt_count == 4 && fixture.copy_count == 0,
        "busy context path performs no retry or scalar copy");
    check(fixture.locks.process_words[1] == 0 && fixture.locks.menu_read_count == 0,
        "earlier acquired locks release in reverse on context contention");
    fixture.locks.context_read_count = 0;
}

TEST_CASE("fallout4.native_ui_capture.ui_capture_fence_order",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    constexpr std::array expected_order{
        UiCaptureFence::process_messages,
        UiCaptureFence::transition_gate,
        UiCaptureFence::menu_stack,
        UiCaptureFence::input_context,
    };
    constexpr std::array expected_release{
        UiCaptureFence::input_context,
        UiCaptureFence::menu_stack,
        UiCaptureFence::transition_gate,
        UiCaptureFence::process_messages,
    };

    FenceProbe complete{};
    check(with_ui_capture_fences(complete,
              [&] {
                  ++complete.copy_count;
                  return true;
              }),
        "all four available fences permit one scalar copy");
    check(complete.attempt_count == 4 && complete.eligibility_count == 1 &&
              complete.copy_count == 1,
        "successful fence tuple tries each fence once and copies once");
    check(complete.attempts == expected_order, "fences acquire in the mapped compatibility order");
    check(complete.release_count == 4 && complete.releases == expected_release,
        "fences release in reverse order");

    for (std::size_t fail_at = 0; fail_at < expected_order.size(); ++fail_at) {
        FenceProbe failed{};
        failed.fail_at = fail_at;
        check(!with_ui_capture_fences(failed,
                  [&] {
                      ++failed.copy_count;
                      return true;
                  }),
            "one contended fence declines the tuple");
        check(failed.attempt_count == fail_at + 1U,
            "contended fence is attempted once with no retry");
        check(failed.eligibility_count == 0 && failed.copy_count == 0,
            "failed acquisition skips eligibility and copy");
        check(failed.release_count == fail_at,
            "only acquired fences are released after contention");
        for (std::size_t index = 0; index < fail_at; ++index) {
            check(failed.releases[index] == expected_release[4U - fail_at + index],
                "partial fence release unwinds in reverse order");
        }
    }

    FenceProbe ineligible{};
    ineligible.state_eligible = false;
    check(!with_ui_capture_fences(ineligible,
              [&] {
                  ++ineligible.copy_count;
                  return true;
              }),
        "ineligible UI state declines after all fences");
    check(ineligible.eligibility_count == 1 && ineligible.copy_count == 0,
        "ineligible tuple releases fences without copying");
}

TEST_CASE("fallout4.native_ui_capture.ui_owner_phase_admission",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    OwnerPhaseProbe ready{};
    ready.facts = {
        .generation = 70,
        .in_flight = 0,
        .closed = false,
        .static_fields_eligible = true,
    };
    check(admit_owner_phase(ready, 70) == OwnerPhaseAdmission::ready,
        "production owner phase admission accepts the exact idle state-zero tuple");
    check(ready.attempts == 1 && ready.reads == 1 && ready.releases == 1,
        "owner phase admission attempts the gate once and releases it");

    OwnerPhaseProbe busy_gate{};
    busy_gate.gate_available = false;
    check(admit_owner_phase(busy_gate, 70) == OwnerPhaseAdmission::deferred &&
              busy_gate.attempts == 1 && busy_gate.reads == 0 && busy_gate.releases == 0,
        "private-gate contention defers without reading native phase fields");

    OwnerPhaseProbe transition{};
    transition.facts = {
        .generation = 71,
        .in_flight = 1,
        .closed = false,
        .static_fields_eligible = true,
    };
    check(admit_owner_phase(transition, 70) == OwnerPhaseAdmission::retired,
        "a changed generation retires the old request during a transition");

    OwnerPhaseProbe state_two{};
    state_two.facts = {
        .generation = 70,
        .in_flight = 0,
        .closed = false,
        .static_fields_eligible = false,
    };
    check(admit_owner_phase(state_two, 70) == OwnerPhaseAdmission::deferred,
        "unready UI processing state defers a request without waiting");

    OwnerPhaseProbe closed{};
    closed.facts = {
        .generation = 0,
        .in_flight = 0,
        .closed = true,
        .static_fields_eligible = false,
    };
    check(admit_owner_phase(closed, 70) == OwnerPhaseAdmission::retired,
        "permanently closed capture retires a pending request");
    OwnerPhaseProbe zero{};
    check(admit_owner_phase(zero, 0) == OwnerPhaseAdmission::retired && zero.attempts == 0,
        "zero generation retires without touching the phase gate");
}

TEST_CASE("fallout4.native_ui_capture.ui_scalar_count_bounds",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;

    check(ui_scalar_counts_valid(64, 64, 256), "maximum context/menu/mapping counts are accepted");
    check(!ui_scalar_counts_valid(0, 0, 0), "empty context stack is rejected");
    check(!ui_scalar_counts_valid(65, 0, 0), "oversized context stack is rejected");
    check(!ui_scalar_counts_valid(1, 65, 0), "oversized menu stack is rejected");
    check(!ui_scalar_counts_valid(1, 0, 257), "oversized mapping list is rejected");
}

TEST_CASE("fallout4.native_ui_capture.ui_mapping_selection",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    constexpr auto attack_name = std::to_array("PrimaryAttack");
    constexpr auto aim_name = std::to_array("SecondaryAttack");
    constexpr auto same_attack_text = std::to_array("PrimaryAttack");
    constexpr auto other_name = std::to_array("Reload");
    const std::array mappings{
        UiMappingValue{.event_name = other_name.data(), .input_key = 0x01},
        UiMappingValue{.event_name = attack_name.data(), .input_key = 0x0A},
        UiMappingValue{.event_name = same_attack_text.data(), .input_key = 0x09},
        UiMappingValue{.event_name = aim_name.data(), .input_key = -1},
    };
    const auto selected = select_ui_mapped_keys(mappings.size(),
        attack_name.data(),
        aim_name.data(),
        [&mappings](std::size_t index) { return mappings[index]; });
    check(selected.valid, "bounded mapping selection accepts a valid list");
    check(selected.attack == 0x0AU, "mapping selection preserves first matching interned action");
    check(selected.aim == invalid_mapped_key,
        "negative mapped key remains invalid rather than guessed");
    check(!select_ui_mapped_keys(257,
              attack_name.data(),
              aim_name.data(),
              [](std::size_t) { return UiMappingValue{}; })
              .valid,
        "mapping selector rejects oversized lists before reading an entry");
}

TEST_CASE("fallout4.native_ui_capture.ui_host_admission",
    "[component][fallout4][native_ui_capture]") {
    ScopedCaptureState const capture_state;
    check(supported_ui_capture_host(REL::Version{1, 11, 240, 0}, REL::Version{0, 7, 9, 0}, 1, 1),
        "production host-admission predicate accepts only the pinned runtime and F4SE interfaces");
    check(!supported_ui_capture_host(REL::Version{1, 11, 221, 0}, REL::Version{0, 7, 9, 0}, 1, 1),
        "wrong runtime is rejected");
    check(!supported_ui_capture_host(REL::Version{1, 11, 240, 0}, REL::Version{0, 7, 8, 0}, 1, 1),
        "wrong F4SE version is rejected");
    check(!supported_ui_capture_host(REL::Version{1, 11, 240, 0}, REL::Version{0, 7, 9, 0}, 2, 1),
        "wrong messaging interface is rejected");
    check(!supported_ui_capture_host(REL::Version{1, 11, 240, 0}, REL::Version{0, 7, 9, 0}, 1, 2),
        "wrong trampoline interface is rejected");
}

} // namespace fallout4_adapter::tests::ui_capture
