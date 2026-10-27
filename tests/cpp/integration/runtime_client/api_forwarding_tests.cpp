#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include "inputnext_adapters/runtime_client.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>

namespace inputnext::test::runtime_client {

void test_client_target_and_capabilities(RuntimeClient& client,
    const std::filesystem::path& fixture,
    input_next_device_target_v1& target,
    input_next_effects_capabilities_v1& capabilities,
    input_next_effects_context_v1& context) {
    input_next_device_selector_v1 selector{};
    selector.struct_size = sizeof(selector);
    selector.target.struct_size = sizeof(selector.target);
    selector.kind = INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT;
    selector.xinput_slot = 1;
    REQUIRE(client.resolve_target(selector, target, capabilities) == INPUT_NEXT_STATUS_OK);
    REQUIRE(target.struct_size == sizeof(target));
    REQUIRE(target.runtime_id[0] == UINT64_C(0x100000000));
    REQUIRE(target.runtime_id[1] == UINT64_C(0x200000000));
    REQUIRE(target.device_key == UINT64_C(0x300000001));
    REQUIRE(target.connection_id == UINT64_C(0x400000001));
    REQUIRE(capabilities.struct_size == sizeof(capabilities));
    REQUIRE(capabilities.supported_channels == INPUT_NEXT_EFFECTS_ALL_CHANNELS);
    REQUIRE(capabilities.feature_flags ==
            (INPUT_NEXT_EFFECTS_CAP_BODY_LEASE | INPUT_NEXT_EFFECTS_CAP_HD_READY | 1U));
    REQUIRE(capabilities.audio_instance_id == UINT64_C(0x500000001));
    REQUIRE(capabilities.capabilities_revision == 7);
    const input_next_effects_capabilities_v1 expected_capabilities{
        .struct_size = sizeof(input_next_effects_capabilities_v1),
        .supported_channels = INPUT_NEXT_EFFECTS_ALL_CHANNELS,
        .feature_flags = INPUT_NEXT_EFFECTS_CAP_BODY_LEASE | INPUT_NEXT_EFFECTS_CAP_HD_READY | 1U,
        .completion_capabilities = 0,
        .audio_instance_id = UINT64_C(0x500000001),
        .capabilities_revision = 7,
    };
    REQUIRE(same_capabilities(capabilities, expected_capabilities));

    input_next_effects_open_v1 open{};
    open.struct_size = sizeof(open);
    open.target = target;
    REQUIRE(client.open_context(open, context) == INPUT_NEXT_STATUS_OK);
    REQUIRE(context.struct_size == sizeof(context));
    REQUIRE(context.id == UINT64_C(0x400000000));
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        const auto& state = observer.state();
        REQUIRE(state.open_calls == 1);
        REQUIRE(state.last_open.struct_size == sizeof(open));
        REQUIRE(same_target(state.last_open.target, target));
    }

    input_next_effects_context_info_v1 info{};
    REQUIRE(client.query_context(context, info) == INPUT_NEXT_STATUS_OK);
    REQUIRE(info.struct_size == sizeof(info));
    REQUIRE(info.state == INPUT_NEXT_EFFECTS_CONTEXT_OPEN);
    REQUIRE(same_context(info.context, context));

    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        const auto& state = observer.state();
        REQUIRE(state.query_calls == 1);
        REQUIRE(state.last_requested_abi == INPUT_NEXT_EFFECTS_ABI_VERSION);
        REQUIRE(state.resolve_calls == 1);
        REQUIRE(state.last_selector.xinput_slot == 1);
        REQUIRE(state.last_target_input_size == sizeof(input_next_device_target_v1));
        REQUIRE(state.last_capabilities_input_size == sizeof(input_next_effects_capabilities_v1));
        REQUIRE(state.query_context_calls == 1);
        REQUIRE(same_context(state.last_context, context));
    }

    const auto preserved_target = target;
    const auto preserved_capabilities = capabilities;
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        observer.state().resolve_status = INPUT_NEXT_STATUS_STALE_TARGET;
    }
    selector.xinput_slot = 3;
    REQUIRE(
        client.resolve_target(selector, target, capabilities) == INPUT_NEXT_STATUS_STALE_TARGET);
    REQUIRE(same_target(target, preserved_target));
    REQUIRE(same_capabilities(capabilities, preserved_capabilities));
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        REQUIRE(observer.state().resolve_calls == 2);
        REQUIRE(observer.state().last_slot_index == 3);
    }

    const auto preserved_info = info;
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        observer.state().query_context_status = INPUT_NEXT_STATUS_BUSY;
    }
    REQUIRE(client.query_context(context, info) == INPUT_NEXT_STATUS_BUSY);
    REQUIRE(info.struct_size == preserved_info.struct_size);
    REQUIRE(info.state == preserved_info.state);
    REQUIRE(same_context(info.context, preserved_info.context));
}

void test_client_channel_effects_and_clear(RuntimeClient& client,
    const std::filesystem::path& fixture,
    const input_next_effects_context_v1& context,
    input_next_effects_request_v1& request,
    input_next_effects_receipt_v1& receipt) {
    request = inputnext::adapters::make_request(context,
        1,
        INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RUMBLE | INPUT_NEXT_EFFECTS_LEDS,
        std::chrono::milliseconds{321});
    request.left_trigger = {
        .mode = INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK,
        .start = 0.125F,
        .end = 0.875F,
        .strength = 0.625F,
        .frequency = 127.0F,
    };
    request.right_trigger = {
        .mode = INPUT_NEXT_EFFECTS_TRIGGER_WEAPON,
        .start = 0.25F,
        .end = 0.75F,
        .strength = 0.5F,
        .frequency = 99.0F,
    };
    request.rumble = {.low_frequency = 0.2F, .high_frequency = 0.8F};
    request.leds = {
        .lightbar_rgb = 0x00123456U,
        .player_index = 3,
        .microphone_led = 1,
        .reserved = 0,
    };
    REQUIRE(client.submit(request, receipt) == INPUT_NEXT_STATUS_OK);
    const input_next_effects_receipt_v1 expected_receipt{
        .struct_size = sizeof(input_next_effects_receipt_v1),
        .accepted_mask = request.channel_mask,
        .context = context,
        .mutation_sequence = request.mutation_sequence,
        .publication_revision = 1,
        .channel_revisions = {1, 0, 1, 1},
    };
    REQUIRE(same_receipt(receipt, expected_receipt));
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        const auto& state = observer.state();
        REQUIRE(state.submit_calls == 1);
        REQUIRE(same_request(state.last_submit, request));
        REQUIRE(same_receipt(state.last_receipt, expected_receipt));
    }

    const auto preserved_receipt = receipt;
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        observer.state().submit_status = INPUT_NEXT_STATUS_STALE_TARGET;
    }
    REQUIRE(client.submit(request, receipt) == INPUT_NEXT_STATUS_STALE_TARGET);
    REQUIRE(same_receipt(receipt, preserved_receipt));
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        REQUIRE(observer.state().submit_calls == 2);
    }

    input_next_effects_clear_v1 clear{};
    clear.struct_size = sizeof(clear);
    clear.channel_mask = INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_LEDS;
    clear.context = context;
    clear.mutation_sequence = 2;
    clear.channel_revisions[0] = receipt.channel_revisions[0];
    clear.channel_revisions[3] = receipt.channel_revisions[3];
    REQUIRE(client.clear(clear) == INPUT_NEXT_STATUS_OK);
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        const auto& state = observer.state();
        REQUIRE(state.clear_calls == 1);
        REQUIRE(same_clear(state.last_clear, clear));
    }
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        observer.state().clear_status = INPUT_NEXT_STATUS_UNSUPPORTED_CHANNEL;
    }
    REQUIRE(client.clear(clear) == INPUT_NEXT_STATUS_UNSUPPORTED_CHANNEL);
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        REQUIRE(observer.state().clear_calls == 2);
    }
}

void test_client_body_effects(RuntimeClient& client,
    const std::filesystem::path& fixture,
    const input_next_device_target_v1& target,
    const input_next_effects_context_v1& context,
    const input_next_effects_capabilities_v1& capabilities) {
    const std::array<float, 8> body_samples{-0.75F, 0.5F, -0.25F, 0.125F, 0.0F, 0.25F, 0.5F, 0.75F};
    input_next_effects_body_request_v1 body_request{};
    body_request.struct_size = sizeof(body_request);
    body_request.ttl_ms = 200;
    body_request.route_preference = INPUT_NEXT_EFFECTS_BODY_ROUTE_PREFER_HD;
    body_request.pcm_command = INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE;
    body_request.context = context;
    body_request.lease_id = 0;
    body_request.audio_instance_id = capabilities.audio_instance_id;
    body_request.sequence = 1;
    body_request.fallback_rumble = {.low_frequency = 0.15F, .high_frequency = 0.35F};
    body_request.pcm = {
        .struct_size = sizeof(input_next_effects_pcm_v1),
        .frame_count = 4,
        .channels = 2,
        .sample_rate = 48'000,
        .gain = 0.8F,
        .reserved = 0,
        .samples = body_samples.data(),
    };

    input_next_effects_body_receipt_v1 receipt{};
    receipt.struct_size = sizeof(receipt);
    REQUIRE(client.submit_body(body_request, receipt) == INPUT_NEXT_STATUS_OK);
    input_next_effects_body_receipt_v1 expected_receipt{};
    expected_receipt.struct_size = sizeof(expected_receipt);
    expected_receipt.effective_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    expected_receipt.context = context;
    expected_receipt.target = target;
    expected_receipt.lease_id = UINT64_C(0x600000000);
    expected_receipt.audio_instance_id = body_request.audio_instance_id;
    expected_receipt.sequence = body_request.sequence;
    expected_receipt.publication_revision = 1;
    expected_receipt.fallback_reason = INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE;
    REQUIRE(same_body_receipt(receipt, expected_receipt));
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        const auto& state = observer.state();
        REQUIRE(state.submit_body_calls == 1);
        REQUIRE(state.last_body_receipt_input_size == sizeof(input_next_effects_body_receipt_v1));
        REQUIRE(same_body_request(state.last_submit_body, body_request));
        REQUIRE(state.copied_body_sample_count == body_samples.size());
        REQUIRE(std::equal(body_samples.begin(),
            body_samples.end(),
            state.copied_body_samples.begin()));
    }

    body_request.lease_id = UINT64_C(0x1234);
    body_request.sequence = 2;
    body_request.pcm_command = INPUT_NEXT_EFFECTS_BODY_PCM_KEEP;
    body_request.pcm = {};
    auto preserved_receipt = input_next_effects_body_receipt_v1{};
    preserved_receipt.struct_size = 51;
    preserved_receipt.effective_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC;
    preserved_receipt.context = context;
    preserved_receipt.target = target;
    preserved_receipt.lease_id = 67;
    preserved_receipt.audio_instance_id = 71;
    preserved_receipt.sequence = 73;
    preserved_receipt.fallback_reason = INPUT_NEXT_EFFECTS_BODY_FALLBACK_NOT_READY;
    preserved_receipt.reserved = 79;
    receipt = preserved_receipt;
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        observer.state().submit_body_status = INPUT_NEXT_STATUS_STALE_LEASE;
        observer.state().body_effective_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC;
        observer.state().body_fallback_reason = INPUT_NEXT_EFFECTS_BODY_FALLBACK_OUTPUT_FAILED;
    }
    REQUIRE(client.submit_body(body_request, receipt) == INPUT_NEXT_STATUS_STALE_LEASE);
    REQUIRE(same_body_receipt(receipt, preserved_receipt));

    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        observer.state().submit_body_status = INPUT_NEXT_STATUS_OK;
    }
    REQUIRE(client.submit_body(body_request, receipt) == INPUT_NEXT_STATUS_OK);
    expected_receipt.effective_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC;
    expected_receipt.lease_id = body_request.lease_id;
    expected_receipt.sequence = body_request.sequence;
    expected_receipt.fallback_reason = INPUT_NEXT_EFFECTS_BODY_FALLBACK_OUTPUT_FAILED;
    REQUIRE(same_body_receipt(receipt, expected_receipt));

    input_next_effects_body_clear_v1 body_clear{};
    body_clear.struct_size = sizeof(body_clear);
    body_clear.context = context;
    body_clear.lease_id = body_request.lease_id;
    body_clear.audio_instance_id = body_request.audio_instance_id;
    body_clear.sequence = 3;
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        observer.state().clear_body_status = INPUT_NEXT_STATUS_STALE_LEASE;
    }
    REQUIRE(client.clear_body(body_clear) == INPUT_NEXT_STATUS_STALE_LEASE);
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        REQUIRE(observer.state().clear_body_calls == 1);
        REQUIRE(same_body_clear(observer.state().last_clear_body, body_clear));
    }
    {
        FixtureObserver observer(fixture);
        REQUIRE(observer);
        observer.state().clear_body_status = INPUT_NEXT_STATUS_OK;
    }
    REQUIRE(client.clear_body(body_clear) == INPUT_NEXT_STATUS_OK);

    input_next_effects_close_v1 close{};
    close.struct_size = sizeof(close);
    close.context = context;
    close.close_request_id = context.id;
    input_next_effects_close_receipt_v1 close_receipt{};
    close_receipt.struct_size = sizeof(close_receipt);
    REQUIRE(client.close_context(close, close_receipt) == INPUT_NEXT_STATUS_REVOKED);
    input_next_effects_close_receipt_v1 expected_close_receipt{};
    expected_close_receipt.struct_size = sizeof(expected_close_receipt);
    expected_close_receipt.disposition = INPUT_NEXT_EFFECTS_CLOSE_NORMAL;
    expected_close_receipt.context = context;
    expected_close_receipt.target = target;
    expected_close_receipt.close_request_id = context.id;
    expected_close_receipt.publication_revision = 2;
    REQUIRE(same_close_receipt(close_receipt, expected_close_receipt));
    REQUIRE(client.ack_close(close_receipt) == INPUT_NEXT_STATUS_OK);

    input_next_effects_progress_query_v1 progress_query{};
    progress_query.struct_size = sizeof(progress_query);
    progress_query.target = target;
    progress_query.publication_revision = 2;
    input_next_effects_progress_v1 progress{};
    progress.struct_size = sizeof(progress);
    REQUIRE(client.query_progress(progress_query, progress) == INPUT_NEXT_STATUS_OK);
    REQUIRE(progress.struct_size == sizeof(progress));
    REQUIRE(progress.stage == INPUT_NEXT_EFFECTS_PROGRESS_CURRENT_COMPLETED);
    REQUIRE(progress.completion_status == INPUT_NEXT_STATUS_OK);
    REQUIRE(same_target(progress.target, target));
    REQUIRE(progress.requested_publication_revision == progress_query.publication_revision);
}

TEST_CASE("runtime_client.target_forwarding", "[integration][runtime_client]") {
    const auto& fixtures = get_fixtures();
    REQUIRE(!fixtures[0].empty());
    input_next_device_target_v1 target{};
    input_next_effects_capabilities_v1 capabilities{};
    input_next_effects_context_v1 context{};
    with_connected_client(fixtures[0], [&](RuntimeClient& client) {
        test_client_target_and_capabilities(client, fixtures[0], target, capabilities, context);
    });
}

TEST_CASE("runtime_client.trigger_and_clear_forwarding", "[integration][runtime_client]") {
    const auto& fixtures = get_fixtures();
    REQUIRE(!fixtures[0].empty());
    input_next_device_target_v1 target{};
    input_next_effects_capabilities_v1 capabilities{};
    input_next_effects_context_v1 context{};
    input_next_effects_request_v1 request{};
    input_next_effects_receipt_v1 receipt{};
    with_connected_client(fixtures[0], [&](RuntimeClient& client) {
        test_client_target_and_capabilities(client, fixtures[0], target, capabilities, context);
        test_client_channel_effects_and_clear(client, fixtures[0], context, request, receipt);
    });
}

TEST_CASE("runtime_client.body_forwarding", "[integration][runtime_client]") {
    const auto& fixtures = get_fixtures();
    REQUIRE(!fixtures[0].empty());
    input_next_device_target_v1 target{};
    input_next_effects_capabilities_v1 capabilities{};
    input_next_effects_context_v1 context{};
    with_connected_client(fixtures[0], [&](RuntimeClient& client) {
        test_client_target_and_capabilities(client, fixtures[0], target, capabilities, context);
        test_client_body_effects(client, fixtures[0], target, context, capabilities);
    });
}

} // namespace inputnext::test::runtime_client
