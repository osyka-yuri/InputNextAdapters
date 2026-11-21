#include "input_next_core.h"
#include <input_next_effects.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace {

input_next_status INPUT_NEXT_EFFECTS_CALL unavailable_resolve_target(
    const input_next_device_selector_v1* /*unused*/,
    input_next_device_target_v1* /*unused*/,
    input_next_effects_capabilities_v1* /*unused*/) noexcept {
    return INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status INPUT_NEXT_EFFECTS_CALL unavailable_open_context(
    const input_next_effects_open_v1* /*unused*/,
    input_next_effects_context_v1* /*unused*/) noexcept {
    return INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status INPUT_NEXT_EFFECTS_CALL unavailable_query_context(
    const input_next_effects_context_v1* /*unused*/,
    input_next_effects_context_info_v1* /*unused*/) noexcept {
    return INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status INPUT_NEXT_EFFECTS_CALL unavailable_submit(
    const input_next_effects_request_v1* /*unused*/,
    input_next_effects_receipt_v1* /*unused*/) noexcept {
    return INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status INPUT_NEXT_EFFECTS_CALL unavailable_clear(
    const input_next_effects_clear_v1* /*unused*/) noexcept {
    return INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status INPUT_NEXT_EFFECTS_CALL unavailable_submit_body(
    const input_next_effects_body_request_v1* /*unused*/,
    input_next_effects_body_receipt_v1* /*unused*/) noexcept {
    return INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status INPUT_NEXT_EFFECTS_CALL unavailable_clear_body(
    const input_next_effects_body_clear_v1* /*unused*/) noexcept {
    return INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status INPUT_NEXT_EFFECTS_CALL unavailable_close_context(
    const input_next_effects_close_v1* /*unused*/,
    input_next_effects_close_receipt_v1* /*unused*/) noexcept {
    return INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status INPUT_NEXT_EFFECTS_CALL unavailable_ack_close(
    const input_next_effects_close_receipt_v1* /*unused*/) noexcept {
    return INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status INPUT_NEXT_EFFECTS_CALL unavailable_query_progress(
    const input_next_effects_progress_query_v1* /*unused*/,
    input_next_effects_progress_v1* /*unused*/) noexcept {
    return INPUT_NEXT_STATUS_UNAVAILABLE;
}

const input_next_effects_api_v1 good_api{
    .struct_size = static_cast<std::uint32_t>(sizeof(input_next_effects_api_v1)),
    .abi_version = INPUT_NEXT_EFFECTS_ABI_VERSION,
    .resolve_target = unavailable_resolve_target,
    .open_context = unavailable_open_context,
    .query_context = unavailable_query_context,
    .submit = unavailable_submit,
    .clear = unavailable_clear,
    .submit_body = unavailable_submit_body,
    .clear_body = unavailable_clear_body,
    .close_context = unavailable_close_context,
    .ack_close = unavailable_ack_close,
    .query_progress = unavailable_query_progress,
};

const input_next_effects_api_v1 bad_size_api{
    .struct_size = static_cast<std::uint32_t>(sizeof(input_next_effects_api_v1) - 1U),
    .abi_version = INPUT_NEXT_EFFECTS_ABI_VERSION,
    .resolve_target = unavailable_resolve_target,
    .open_context = unavailable_open_context,
    .query_context = unavailable_query_context,
    .submit = unavailable_submit,
    .clear = unavailable_clear,
    .submit_body = unavailable_submit_body,
    .clear_body = unavailable_clear_body,
    .close_context = unavailable_close_context,
    .ack_close = unavailable_ack_close,
    .query_progress = unavailable_query_progress,
};

constexpr input_next_device_target_v1 worker_target{
    .struct_size = sizeof(input_next_device_target_v1),
    .reserved = 0,
    .runtime_id = {1, 0},
    .device_key = 2,
    .connection_id = 3,
};
constexpr input_next_effects_context_v1 worker_context{
    .struct_size = sizeof(input_next_effects_context_v1),
    .reserved = 0,
    .runtime_id = {1, 0},
    .id = 4,
};
std::atomic<std::uint32_t> worker_opens{};
std::atomic<std::uint32_t> worker_closes{};
input_next_status worker_close_status{};

input_next_status INPUT_NEXT_EFFECTS_CALL worker_resolve_target(
    const input_next_device_selector_v1* /*unused*/,
    input_next_device_target_v1* target,
    input_next_effects_capabilities_v1* capabilities) noexcept {
    *target = worker_target;
    *capabilities = {};
    capabilities->struct_size = sizeof(*capabilities);
    return INPUT_NEXT_STATUS_OK;
}

input_next_status INPUT_NEXT_EFFECTS_CALL worker_open_context(
    const input_next_effects_open_v1* /*unused*/,
    input_next_effects_context_v1* context) noexcept {
    *context = worker_context;
    ++worker_opens;
    return INPUT_NEXT_STATUS_OK;
}

input_next_status INPUT_NEXT_EFFECTS_CALL worker_query_context(
    const input_next_effects_context_v1* /*unused*/,
    input_next_effects_context_info_v1* info) noexcept {
    *info = {.struct_size = sizeof(*info),
        .state = INPUT_NEXT_EFFECTS_CONTEXT_OPEN,
        .context = worker_context};
    return INPUT_NEXT_STATUS_OK;
}

input_next_status INPUT_NEXT_EFFECTS_CALL worker_close_context(
    const input_next_effects_close_v1* close,
    input_next_effects_close_receipt_v1* /*unused*/) noexcept {
    if (close->context.id != worker_context.id || close->close_request_id != worker_context.id) {
        return INPUT_NEXT_STATUS_STALE_CONTEXT;
    }
    ++worker_closes;
    return worker_close_status;
}

constexpr input_next_effects_api_v1 worker_api{
    .struct_size = static_cast<std::uint32_t>(sizeof(input_next_effects_api_v1)),
    .abi_version = INPUT_NEXT_EFFECTS_ABI_VERSION,
    .resolve_target = worker_resolve_target,
    .open_context = worker_open_context,
    .query_context = worker_query_context,
    .submit = unavailable_submit,
    .clear = unavailable_clear,
    .submit_body = unavailable_submit_body,
    .clear_body = unavailable_clear_body,
    .close_context = worker_close_context,
    .ack_close = unavailable_ack_close,
    .query_progress = unavailable_query_progress,
};

} // namespace

extern "C" std::uint32_t __cdecl InputNext_TestWorkerCalls(bool close) noexcept {
    return close ? worker_closes.load() : worker_opens.load();
}

extern "C" const input_next_effects_api_v1* INPUT_NEXT_EFFECTS_CALL
input_next_runtime_query_effects_api(std::uint32_t requested_abi) noexcept {
    if (requested_abi != INPUT_NEXT_EFFECTS_ABI_VERSION) {
        return nullptr;
    }

    std::array<char, 32> mode{};
    const auto length = GetEnvironmentVariableA("INPUTNEXT_OBLIVION_TEST_RUNTIME_MODE",
        mode.data(),
        static_cast<DWORD>(mode.size()));
    if (length >= static_cast<DWORD>(mode.size())) {
        return nullptr;
    }
    if (length != 0 && std::strcmp(mode.data(), "unavailable") == 0) {
        return nullptr;
    }
    if (std::strcmp(mode.data(), "close_busy") == 0) {
        worker_close_status = INPUT_NEXT_STATUS_BUSY;
        return &worker_api;
    }
    if (std::strcmp(mode.data(), "close_invalid") == 0) {
        worker_close_status = INPUT_NEXT_STATUS_INVALID_ARGUMENT;
        return &worker_api;
    }
    return length != 0 && std::strcmp(mode.data(), "bad_size") == 0 ? &bad_size_api : &good_api;
}
