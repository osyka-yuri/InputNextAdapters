#include "fake_runtime.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>

#include <cstdint>

#ifndef FAKE_RUNTIME_KIND
#    error FAKE_RUNTIME_KIND must be set for the fake runtime fixture.
#endif

#ifndef TEST_FIXTURE_ID
#    error TEST_FIXTURE_ID must be set for the fake runtime fixture.
#endif

namespace {

fake_runtime_state make_initial_state() noexcept {
    fake_runtime_state state{};
    state.struct_size = static_cast<std::uint32_t>(sizeof(state));
    state.fixture_id = TEST_FIXTURE_ID;
    state.resolve_status = INPUT_NEXT_STATUS_OK;
    state.open_status = INPUT_NEXT_STATUS_OK;
    state.query_context_status = INPUT_NEXT_STATUS_OK;
    state.submit_status = INPUT_NEXT_STATUS_OK;
    state.clear_status = INPUT_NEXT_STATUS_OK;
    state.submit_body_status = INPUT_NEXT_STATUS_OK;
    state.clear_body_status = INPUT_NEXT_STATUS_OK;
    state.close_status = INPUT_NEXT_STATUS_REVOKED;
    state.ack_close_status = INPUT_NEXT_STATUS_OK;
    state.query_progress_status = INPUT_NEXT_STATUS_OK;
    state.body_effective_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    state.body_fallback_reason = INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE;
    state.next_context_id = UINT64_C(0x400000000);
    return state;
}

fake_runtime_state g_state = make_initial_state();

#if FAKE_RUNTIME_KIND != 1
[[maybe_unused]] input_next_status INPUT_NEXT_EFFECTS_CALL fake_resolve_target(
    const input_next_device_selector_v1* selector,
    input_next_device_target_v1* out_target,
    input_next_effects_capabilities_v1* out_capabilities) noexcept {
    ++g_state.resolve_calls;
    if (selector == nullptr || out_target == nullptr || out_capabilities == nullptr ||
        selector->target.struct_size != sizeof(selector->target)) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }
    g_state.last_selector = *selector;
    g_state.last_slot_index = selector->xinput_slot;
    g_state.last_target_input_size = out_target->struct_size;
    g_state.last_capabilities_input_size = out_capabilities->struct_size;

    input_next_device_target_v1 target{};
    target.struct_size = static_cast<std::uint32_t>(sizeof(target));
    target.runtime_id[0] = UINT64_C(0x100000000);
    target.runtime_id[1] = UINT64_C(0x200000000);
    target.device_key = UINT64_C(0x300000000) + selector->xinput_slot;
    target.connection_id = UINT64_C(0x400000000) + selector->xinput_slot;

    input_next_effects_capabilities_v1 capabilities{};
    capabilities.struct_size = static_cast<std::uint32_t>(sizeof(capabilities));
    capabilities.supported_channels = INPUT_NEXT_EFFECTS_ALL_CHANNELS;
    capabilities.feature_flags = INPUT_NEXT_EFFECTS_CAP_BODY_LEASE |
                                 INPUT_NEXT_EFFECTS_CAP_HD_READY |
                                 static_cast<std::uint32_t>(TEST_FIXTURE_ID);
    capabilities.audio_instance_id = UINT64_C(0x500000000) + selector->xinput_slot;
    capabilities.capabilities_revision = 7;

    g_state.last_target = target;
    g_state.last_capabilities = capabilities;
    *out_target = target;
    *out_capabilities = capabilities;
    return g_state.resolve_status;
}

[[maybe_unused]] input_next_status INPUT_NEXT_EFFECTS_CALL fake_open_context(
    const input_next_effects_open_v1* request,
    input_next_effects_context_v1* out_context) noexcept {
    ++g_state.open_calls;
    if (request == nullptr || out_context == nullptr) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }
    g_state.last_open = *request;
    if (g_state.open_status != INPUT_NEXT_STATUS_OK &&
        g_state.open_status != INPUT_NEXT_STATUS_PENDING &&
        g_state.open_status != INPUT_NEXT_STATUS_REVOKED) {
        return g_state.open_status;
    }
    input_next_effects_context_v1 context{};
    context.struct_size = static_cast<std::uint32_t>(sizeof(context));
    context.runtime_id[0] = request->target.runtime_id[0];
    context.runtime_id[1] = request->target.runtime_id[1];
    context.id = g_state.next_context_id++;
    g_state.last_context = context;
    *out_context = context;
    return g_state.open_status;
}

[[maybe_unused]] input_next_status INPUT_NEXT_EFFECTS_CALL fake_query_context(
    const input_next_effects_context_v1* context,
    input_next_effects_context_info_v1* out_info) noexcept {
    ++g_state.query_context_calls;
    if (context == nullptr || out_info == nullptr) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }
    g_state.last_context = *context;
    if (g_state.query_context_status != INPUT_NEXT_STATUS_OK &&
        g_state.query_context_status != INPUT_NEXT_STATUS_PENDING &&
        g_state.query_context_status != INPUT_NEXT_STATUS_REVOKED) {
        return g_state.query_context_status;
    }
    input_next_effects_context_info_v1 info{};
    info.struct_size = static_cast<std::uint32_t>(sizeof(info));
    if (g_state.query_context_status == INPUT_NEXT_STATUS_PENDING) {
        info.state = INPUT_NEXT_EFFECTS_CONTEXT_ATTACHING;
    } else if (g_state.query_context_status == INPUT_NEXT_STATUS_REVOKED) {
        info.state = INPUT_NEXT_EFFECTS_CONTEXT_REVOKED;
    } else {
        info.state = INPUT_NEXT_EFFECTS_CONTEXT_OPEN;
    }
    info.context = *context;
    g_state.last_context_info = info;
    *out_info = info;
    return g_state.query_context_status;
}

[[maybe_unused]] input_next_status INPUT_NEXT_EFFECTS_CALL fake_submit(
    const input_next_effects_request_v1* request,
    input_next_effects_receipt_v1* out_receipt) noexcept {
    ++g_state.submit_calls;
    if (request == nullptr || out_receipt == nullptr ||
        out_receipt->struct_size != sizeof(*out_receipt) ||
        out_receipt->context.struct_size != sizeof(out_receipt->context)) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }
    g_state.last_submit = *request;
    if (g_state.submit_status == INPUT_NEXT_STATUS_OK) {
        input_next_effects_receipt_v1 receipt{};
        receipt.struct_size = static_cast<std::uint32_t>(sizeof(receipt));
        receipt.accepted_mask = request->channel_mask;
        receipt.context = request->context;
        receipt.mutation_sequence = request->mutation_sequence;
        receipt.publication_revision = 1;
        for (std::uint32_t index = 0; index < 4; ++index) {
            if ((request->channel_mask & (UINT32_C(1) << index)) != 0) {
                receipt.channel_revisions[index] = 1;
            }
        }
        g_state.last_receipt = receipt;
        *out_receipt = receipt;
    }
    return g_state.submit_status;
}

[[maybe_unused]] input_next_status INPUT_NEXT_EFFECTS_CALL fake_clear(
    const input_next_effects_clear_v1* request) noexcept {
    ++g_state.clear_calls;
    if (request == nullptr) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }
    g_state.last_clear = *request;
    return g_state.clear_status;
}

[[maybe_unused]] input_next_status INPUT_NEXT_EFFECTS_CALL fake_submit_body(
    const input_next_effects_body_request_v1* request,
    input_next_effects_body_receipt_v1* out_receipt) noexcept {
    ++g_state.submit_body_calls;
    g_state.copied_body_sample_count = 0;
    for (auto& sample : g_state.copied_body_samples) {
        sample = 0.0F;
    }
    if (request == nullptr || out_receipt == nullptr ||
        out_receipt->struct_size != sizeof(*out_receipt) ||
        out_receipt->context.struct_size != sizeof(out_receipt->context) ||
        out_receipt->target.struct_size != sizeof(out_receipt->target)) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }

    g_state.last_submit_body = *request;
    g_state.last_body_receipt_input_size = out_receipt->struct_size;
    if (request->pcm.samples != nullptr && request->pcm.channels != 0 &&
        request->pcm.frame_count <= g_state.copied_body_samples.size() / request->pcm.channels) {
        const auto sample_count = request->pcm.frame_count * request->pcm.channels;
        g_state.copied_body_sample_count = sample_count;
        for (std::uint32_t index = 0; index < sample_count; ++index) {
            g_state.copied_body_samples[index] = request->pcm.samples[index];
        }
    }

    if (g_state.submit_body_status == INPUT_NEXT_STATUS_OK) {
        input_next_effects_body_receipt_v1 receipt{};
        receipt.struct_size = static_cast<std::uint32_t>(sizeof(receipt));
        receipt.effective_route = g_state.body_effective_route;
        receipt.context = request->context;
        // The active context owns the target resolved when it was opened. A later failed
        // resolve may update last_target, but it cannot retarget this context's receipt.
        receipt.target = g_state.last_open.target;
        receipt.lease_id = request->lease_id == 0 ? UINT64_C(0x600000000) : request->lease_id;
        receipt.audio_instance_id = request->audio_instance_id;
        receipt.sequence = request->sequence;
        receipt.publication_revision = 1;
        receipt.fallback_reason = g_state.body_fallback_reason;
        g_state.last_body_receipt = receipt;
        *out_receipt = receipt;
    }
    return g_state.submit_body_status;
}

[[maybe_unused]] input_next_status INPUT_NEXT_EFFECTS_CALL fake_clear_body(
    const input_next_effects_body_clear_v1* request) noexcept {
    ++g_state.clear_body_calls;
    if (request == nullptr) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }
    g_state.last_clear_body = *request;
    return g_state.clear_body_status;
}

[[maybe_unused]] input_next_status INPUT_NEXT_EFFECTS_CALL fake_close_context(
    const input_next_effects_close_v1* request,
    input_next_effects_close_receipt_v1* out_receipt) noexcept {
    ++g_state.close_calls;
    if (request == nullptr || out_receipt == nullptr ||
        out_receipt->struct_size != sizeof(*out_receipt) ||
        out_receipt->context.struct_size != sizeof(out_receipt->context) ||
        out_receipt->target.struct_size != sizeof(out_receipt->target)) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }
    g_state.last_close = *request;
    if (g_state.close_status == INPUT_NEXT_STATUS_REVOKED) {
        input_next_effects_close_receipt_v1 receipt{};
        receipt.struct_size = static_cast<std::uint32_t>(sizeof(receipt));
        receipt.disposition = INPUT_NEXT_EFFECTS_CLOSE_NORMAL;
        receipt.context = request->context;
        receipt.target = g_state.last_open.target;
        receipt.close_request_id = request->close_request_id;
        receipt.publication_revision = 2;
        g_state.last_close_receipt = receipt;
        *out_receipt = receipt;
    }
    return g_state.close_status;
}

[[maybe_unused]] input_next_status INPUT_NEXT_EFFECTS_CALL fake_ack_close(
    const input_next_effects_close_receipt_v1* receipt) noexcept {
    ++g_state.ack_close_calls;
    if (receipt == nullptr) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }
    g_state.last_acknowledgement = *receipt;
    return g_state.ack_close_status;
}

[[maybe_unused]] input_next_status INPUT_NEXT_EFFECTS_CALL fake_query_progress(
    const input_next_effects_progress_query_v1* query,
    input_next_effects_progress_v1* out_progress) noexcept {
    ++g_state.query_progress_calls;
    if (query == nullptr || out_progress == nullptr ||
        out_progress->struct_size != sizeof(*out_progress) ||
        out_progress->target.struct_size != sizeof(out_progress->target)) {
        return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
    }
    g_state.last_progress_query = *query;
    if (g_state.query_progress_status == INPUT_NEXT_STATUS_OK) {
        input_next_effects_progress_v1 progress{};
        progress.struct_size = static_cast<std::uint32_t>(sizeof(progress));
        progress.stage = INPUT_NEXT_EFFECTS_PROGRESS_CURRENT_COMPLETED;
        progress.completion_class = INPUT_NEXT_EFFECTS_COMPLETION_SETTER_RETURNED_CURRENT;
        progress.completion_status = INPUT_NEXT_STATUS_OK;
        progress.target = query->target;
        progress.requested_publication_revision = query->publication_revision;
        progress.current_publication_revision = query->publication_revision;
        progress.attempt_serial = 1;
        g_state.last_progress = progress;
        *out_progress = progress;
    }
    return g_state.query_progress_status;
}

constexpr std::uint32_t api_size =
#    if FAKE_RUNTIME_KIND == 3
    static_cast<std::uint32_t>(sizeof(input_next_effects_api_v1) - 1U);
#    else
    static_cast<std::uint32_t>(sizeof(input_next_effects_api_v1));
#    endif

constexpr std::uint32_t api_version =
#    if FAKE_RUNTIME_KIND == 4
    INPUT_NEXT_EFFECTS_ABI_VERSION + 1U;
#    else
    INPUT_NEXT_EFFECTS_ABI_VERSION;
#    endif

[[maybe_unused]] const input_next_effects_api_v1 g_api{
    .struct_size = api_size,
    .abi_version = api_version,
#    if FAKE_RUNTIME_KIND == 5
    .resolve_target = nullptr,
#    else
    .resolve_target = fake_resolve_target,
#    endif
#    if FAKE_RUNTIME_KIND == 6
    .open_context = nullptr,
#    else
    .open_context = fake_open_context,
#    endif
#    if FAKE_RUNTIME_KIND == 7
    .query_context = nullptr,
#    else
    .query_context = fake_query_context,
#    endif
#    if FAKE_RUNTIME_KIND == 8
    .submit = nullptr,
#    else
    .submit = fake_submit,
#    endif
#    if FAKE_RUNTIME_KIND == 9
    .clear = nullptr,
#    else
    .clear = fake_clear,
#    endif
#    if FAKE_RUNTIME_KIND == 10
    .submit_body = nullptr,
#    else
    .submit_body = fake_submit_body,
#    endif
#    if FAKE_RUNTIME_KIND == 11
    .clear_body = nullptr,
#    else
    .clear_body = fake_clear_body,
#    endif
#    if FAKE_RUNTIME_KIND == 12
    .close_context = nullptr,
#    else
    .close_context = fake_close_context,
#    endif
#    if FAKE_RUNTIME_KIND == 13
    .ack_close = nullptr,
#    else
    .ack_close = fake_ack_close,
#    endif
#    if FAKE_RUNTIME_KIND == 14
    .query_progress = nullptr,
#    else
    .query_progress = fake_query_progress,
#    endif
};
#endif

} // namespace

extern "C" __declspec(dllexport) void INPUT_NEXT_EFFECTS_CALL inputnext_test_reset() noexcept {
    g_state = make_initial_state();
}

extern "C" __declspec(dllexport) fake_runtime_state* INPUT_NEXT_EFFECTS_CALL
inputnext_test_get_state() noexcept {
    return &g_state;
}

#if FAKE_RUNTIME_KIND != 1
extern "C" const input_next_effects_api_v1* INPUT_NEXT_EFFECTS_CALL
input_next_runtime_query_effects_api(std::uint32_t requested_abi) noexcept {
    ++g_state.query_calls;
    g_state.last_requested_abi = requested_abi;
#    if FAKE_RUNTIME_KIND == 2
    (void)requested_abi;
    return nullptr;
#    else
    return requested_abi == INPUT_NEXT_EFFECTS_ABI_VERSION ? &g_api : nullptr;
#    endif
}
#endif
