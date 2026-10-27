#include "inputnext_adapters/runtime_client.hpp"
#include <catch2/catch_test_macros.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <Windows.h>

#include <tuple>

namespace {
using inputnext::adapters::RuntimeClient;

bool same_target(const input_next_device_target_v1& left,
    const input_next_device_target_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.reserved,
            value.runtime_id[0],
            value.runtime_id[1],
            value.device_key,
            value.connection_id);
    };
    return fields(left) == fields(right);
}

bool same_context(const input_next_effects_context_v1& left,
    const input_next_effects_context_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.reserved,
            value.runtime_id[0],
            value.runtime_id[1],
            value.id);
    };
    return fields(left) == fields(right);
}

bool same_body_receipt(const input_next_effects_body_receipt_v1& left,
    const input_next_effects_body_receipt_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.effective_route,
            value.lease_id,
            value.audio_instance_id,
            value.sequence,
            value.publication_revision,
            value.fallback_reason,
            value.reserved);
    };
    return fields(left) == fields(right) && same_context(left.context, right.context) &&
           same_target(left.target, right.target);
}
} // namespace

TEST_CASE("runtime_client.inert_client", "[unit][runtime_client]") {
    RuntimeClient inert;
    REQUIRE(!inert.connected());

    input_next_device_target_v1 target{};
    target.struct_size = 17;
    input_next_effects_capabilities_v1 capabilities{};
    capabilities.struct_size = 19;
    input_next_device_selector_v1 selector{};
    selector.struct_size = sizeof(selector);
    selector.kind = INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT;
    input_next_effects_context_v1 context{};
    context.struct_size = 23;

    REQUIRE(inert.resolve_target(selector, target, capabilities) == INPUT_NEXT_STATUS_UNAVAILABLE);
    REQUIRE(target.struct_size == 17);
    REQUIRE(capabilities.struct_size == 19);

    input_next_effects_open_v1 const open{};
    input_next_effects_context_v1 opened_context{};
    opened_context.struct_size = 23;
    REQUIRE(inert.open_context(open, opened_context) == INPUT_NEXT_STATUS_UNAVAILABLE);
    REQUIRE(opened_context.struct_size == 23);

    input_next_effects_context_info_v1 context_info{};
    context_info.struct_size = 29;
    REQUIRE(inert.query_context(context, context_info) == INPUT_NEXT_STATUS_UNAVAILABLE);
    REQUIRE(context_info.struct_size == 29);

    input_next_effects_request_v1 const request{};
    input_next_effects_receipt_v1 effect_receipt{};
    effect_receipt.struct_size = 31;
    REQUIRE(inert.submit(request, effect_receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
    REQUIRE(effect_receipt.struct_size == 31);

    input_next_effects_clear_v1 const clear{};
    REQUIRE(inert.clear(clear) == INPUT_NEXT_STATUS_UNAVAILABLE);

    input_next_effects_body_request_v1 const body_request{};
    input_next_effects_body_receipt_v1 body_receipt{
        .struct_size = 17,
        .effective_route = 3,
        .context = {.struct_size = 19, .reserved = 0, .runtime_id = {1, 2}, .id = 3},
        .target = {.struct_size = 20,
            .reserved = 0,
            .runtime_id = {4, 5},
            .device_key = 6,
            .connection_id = 7},
        .lease_id = 31,
        .audio_instance_id = 37,
        .sequence = 41,
        .publication_revision = 43,
        .fallback_reason = 43,
        .reserved = 47,
    };
    const auto preserved_body_receipt = body_receipt;
    REQUIRE(inert.submit_body(body_request, body_receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
    REQUIRE(same_body_receipt(body_receipt, preserved_body_receipt));

    input_next_effects_body_clear_v1 const body_clear{};
    REQUIRE(inert.clear_body(body_clear) == INPUT_NEXT_STATUS_UNAVAILABLE);

    input_next_effects_close_v1 const close{};
    input_next_effects_close_receipt_v1 close_receipt{};
    close_receipt.struct_size = 53;
    REQUIRE(inert.close_context(close, close_receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
    REQUIRE(close_receipt.struct_size == 53);
    REQUIRE(inert.ack_close(close_receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);

    input_next_effects_progress_query_v1 const progress_query{};
    input_next_effects_progress_v1 progress{};
    progress.struct_size = 59;
    REQUIRE(inert.query_progress(progress_query, progress) == INPUT_NEXT_STATUS_UNAVAILABLE);
    REQUIRE(progress.struct_size == 59);
}
