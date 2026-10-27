#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <utility>

namespace inputnext::test::runtime_client {

void test_concurrent_clients_and_moves(RuntimeClient& first,
    const std::filesystem::path& first_fixture,
    const std::filesystem::path& second_fixture,
    const input_next_device_target_v1& target,
    const input_next_effects_request_v1& request,
    const input_next_effects_receipt_v1& receipt) {
    REQUIRE(receipt.accepted_mask == request.channel_mask);
    RuntimeClient second;
    REQUIRE(second.connect(second_fixture));
    input_next_device_selector_v1 selector{};
    selector.struct_size = sizeof(selector);
    selector.target.struct_size = sizeof(selector.target);
    selector.kind = INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT;
    selector.xinput_slot = 1;
    input_next_device_target_v1 second_target{};
    input_next_effects_capabilities_v1 second_capabilities{};
    REQUIRE(second.resolve_target(selector, second_target, second_capabilities) ==
            INPUT_NEXT_STATUS_OK);
    REQUIRE(same_target(target, second_target));
    REQUIRE(second_capabilities.feature_flags ==
            (INPUT_NEXT_EFFECTS_CAP_BODY_LEASE | INPUT_NEXT_EFFECTS_CAP_HD_READY | 2U));
    REQUIRE(second_capabilities.audio_instance_id == UINT64_C(0x500000001));
    input_next_effects_open_v1 open{};
    open.struct_size = sizeof(open);
    open.target = second_target;
    input_next_effects_context_v1 context{};
    REQUIRE(second.open_context(open, context) == INPUT_NEXT_STATUS_OK);
    auto second_request = request;
    second_request.context = context;
    second_request.mutation_sequence = 1;
    input_next_effects_receipt_v1 second_receipt{};
    REQUIRE(second.submit(second_request, second_receipt) == INPUT_NEXT_STATUS_OK);
    input_next_effects_clear_v1 clear{};
    clear.struct_size = sizeof(clear);
    clear.channel_mask = INPUT_NEXT_EFFECTS_LEDS;
    clear.context = context;
    clear.mutation_sequence = 2;
    clear.channel_revisions[3] = second_receipt.channel_revisions[3];
    REQUIRE(second.clear(clear) == INPUT_NEXT_STATUS_OK);
    {
        FixtureObserver first_observer(first_fixture);
        FixtureObserver second_observer(second_fixture);
        REQUIRE(first_observer);
        REQUIRE(second_observer);
        REQUIRE(first_observer.state().fixture_id == 1);
        REQUIRE(second_observer.state().fixture_id == 2);
        REQUIRE(first_observer.state().resolve_calls == 2);
        REQUIRE(first_observer.state().submit_calls == 2);
        REQUIRE(first_observer.state().clear_calls == 2);
        REQUIRE(first_observer.state().submit_body_calls == 3);
        REQUIRE(first_observer.state().clear_body_calls == 2);
        REQUIRE(second_observer.state().resolve_calls == 1);
        REQUIRE(second_observer.state().last_slot_index == 1);
        REQUIRE(second_observer.state().submit_calls == 1);
        REQUIRE(same_request(second_observer.state().last_submit, second_request));
        REQUIRE(second_observer.state().clear_calls == 1);
        REQUIRE(same_context(second_observer.state().last_clear.context, context));
    }

    RuntimeClient moved(std::move(first));
    // RuntimeClient guarantees that a moved-from object is disconnected and remains observable.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    REQUIRE(!first.connected());
    REQUIRE(moved.connected());
    REQUIRE(module_is_loaded(first_fixture));
    REQUIRE(module_is_loaded(second_fixture));

    // Moved-from RuntimeClient returns UNAVAILABLE for all API methods without modifying arguments.
    {
        input_next_device_target_v1 moved_target{
            .struct_size = sizeof(moved_target),
            .reserved = 0,
            .runtime_id = {11, 13},
            .device_key = 17,
            .connection_id = 19,
        };
        input_next_effects_capabilities_v1 moved_caps{
            .struct_size = sizeof(moved_caps),
            .supported_channels = 0,
            .feature_flags = 0,
            .completion_capabilities = 0,
            .audio_instance_id = 0,
            .capabilities_revision = 0,
        };
        input_next_device_selector_v1 const moved_selector{};
        REQUIRE(first.resolve_target(moved_selector, moved_target, moved_caps) ==
                INPUT_NEXT_STATUS_UNAVAILABLE);
        REQUIRE(moved_target.device_key == 17);

        input_next_effects_request_v1 const moved_req{};
        input_next_effects_receipt_v1 moved_effect_receipt{};
        REQUIRE(first.submit(moved_req, moved_effect_receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);

        input_next_effects_clear_v1 const moved_clear{};
        REQUIRE(first.clear(moved_clear) == INPUT_NEXT_STATUS_UNAVAILABLE);

        input_next_effects_body_request_v1 const moved_body_req{};
        input_next_effects_body_receipt_v1 moved_receipt{
            .struct_size = sizeof(moved_receipt),
            .effective_route = 99,
            .context = {},
            .target = {},
            .lease_id = 0,
            .audio_instance_id = 0,
            .sequence = 0,
            .publication_revision = 0,
            .fallback_reason = 0,
            .reserved = 0,
        };
        REQUIRE(first.submit_body(moved_body_req, moved_receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
        REQUIRE(moved_receipt.effective_route == 99);

        input_next_effects_body_clear_v1 const moved_body_clear{};
        REQUIRE(first.clear_body(moved_body_clear) == INPUT_NEXT_STATUS_UNAVAILABLE);
        input_next_effects_close_v1 const close{};
        input_next_effects_close_receipt_v1 close_receipt{};
        REQUIRE(first.close_context(close, close_receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
        REQUIRE(first.ack_close(close_receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
        input_next_effects_progress_query_v1 const progress_query{};
        input_next_effects_progress_v1 progress{};
        REQUIRE(first.query_progress(progress_query, progress) == INPUT_NEXT_STATUS_UNAVAILABLE);
    }

    second = std::move(moved);
    // RuntimeClient guarantees that a moved-from object remains observable as disconnected.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    REQUIRE(!moved.connected());
    REQUIRE(second.connected());
    REQUIRE(module_is_loaded(first_fixture));
    REQUIRE(!module_is_loaded(second_fixture));
    second.disconnect();
    REQUIRE(!second.connected());
    REQUIRE(!module_is_loaded(first_fixture));
}

TEST_CASE("runtime_client.client_move_operations", "[integration][runtime_client]") {
    const auto& fixtures = get_fixtures();
    REQUIRE(!fixtures[0].empty());
    REQUIRE(!fixtures[1].empty());
    REQUIRE(reset_fixture(fixtures[0]));
    REQUIRE(reset_fixture(fixtures[1]));
    std::exception_ptr failure;
    try {
        RuntimeClient first;
        REQUIRE(first.connect(fixtures[0]));
        REQUIRE(first.connected());
        REQUIRE(module_is_loaded(fixtures[0]));
        const auto second_path_result = first.connect(fixtures[1]);
        REQUIRE(second_path_result.error == ConnectError::already_connected);
        REQUIRE(first.connected());
        REQUIRE(!module_is_loaded(fixtures[1]));
        input_next_device_target_v1 target{};
        input_next_effects_capabilities_v1 capabilities{};
        input_next_effects_context_v1 context{};
        input_next_effects_request_v1 request{};
        input_next_effects_receipt_v1 receipt{};
        test_client_target_and_capabilities(first, fixtures[0], target, capabilities, context);
        test_client_channel_effects_and_clear(first, fixtures[0], context, request, receipt);
        test_client_body_effects(first, fixtures[0], target, context, capabilities);
        test_concurrent_clients_and_moves(first,
            fixtures[0],
            fixtures[1],
            target,
            request,
            receipt);
    } catch (...) {
        failure = std::current_exception();
    }
    CHECK(!module_is_loaded(fixtures[0]));
    CHECK(!module_is_loaded(fixtures[1]));
    if (failure) {
        std::rethrow_exception(failure);
    }
}

} // namespace inputnext::test::runtime_client
