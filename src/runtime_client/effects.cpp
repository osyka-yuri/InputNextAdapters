#include "inputnext_adapters/runtime_client.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>

#include <cstdint>

namespace inputnext::adapters {

input_next_status RuntimeClient::resolve_target(const input_next_device_selector_v1& selector,
    input_next_device_target_v1& target,
    input_next_effects_capabilities_v1& capabilities) const noexcept {
    if (!connected()) {
        return INPUT_NEXT_STATUS_UNAVAILABLE;
    }

    auto candidate_target = input_next_device_target_v1{};
    candidate_target.struct_size = static_cast<std::uint32_t>(sizeof(candidate_target));
    auto candidate_capabilities = input_next_effects_capabilities_v1{};
    candidate_capabilities.struct_size = static_cast<std::uint32_t>(sizeof(candidate_capabilities));
    const auto status = api_.resolve_target(&selector, &candidate_target, &candidate_capabilities);
    if (status == INPUT_NEXT_STATUS_OK) {
        target = candidate_target;
        capabilities = candidate_capabilities;
    }
    return status;
}

input_next_status RuntimeClient::open_context(const input_next_effects_open_v1& request,
    input_next_effects_context_v1& context) const noexcept {
    if (!connected()) {
        return INPUT_NEXT_STATUS_UNAVAILABLE;
    }
    auto candidate = input_next_effects_context_v1{};
    candidate.struct_size = static_cast<std::uint32_t>(sizeof(candidate));
    const auto status = api_.open_context(&request, &candidate);
    if (status == INPUT_NEXT_STATUS_OK) {
        context = candidate;
    }
    return status;
}

input_next_status RuntimeClient::query_context(const input_next_effects_context_v1& context,
    input_next_effects_context_info_v1& info) const noexcept {
    if (!connected()) {
        return INPUT_NEXT_STATUS_UNAVAILABLE;
    }
    auto candidate = input_next_effects_context_info_v1{};
    candidate.struct_size = static_cast<std::uint32_t>(sizeof(candidate));
    candidate.context.struct_size = static_cast<std::uint32_t>(sizeof(candidate.context));
    const auto status = api_.query_context(&context, &candidate);
    if (status == INPUT_NEXT_STATUS_OK || status == INPUT_NEXT_STATUS_REVOKED) {
        info = candidate;
    }
    return status;
}

input_next_status RuntimeClient::submit(const input_next_effects_request_v1& request,
    input_next_effects_receipt_v1& receipt) const noexcept {
    if (!connected()) {
        return INPUT_NEXT_STATUS_UNAVAILABLE;
    }
    auto candidate = input_next_effects_receipt_v1{};
    candidate.struct_size = static_cast<std::uint32_t>(sizeof(candidate));
    candidate.context.struct_size = static_cast<std::uint32_t>(sizeof(candidate.context));
    const auto status = api_.submit(&request, &candidate);
    if (status == INPUT_NEXT_STATUS_OK) {
        receipt = candidate;
    }
    return status;
}

input_next_status RuntimeClient::clear(const input_next_effects_clear_v1& request) const noexcept {
    return connected() ? api_.clear(&request) : INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status RuntimeClient::submit_body(const input_next_effects_body_request_v1& request,
    input_next_effects_body_receipt_v1& receipt) const noexcept {
    if (!connected()) {
        return INPUT_NEXT_STATUS_UNAVAILABLE;
    }
    auto candidate = input_next_effects_body_receipt_v1{};
    candidate.struct_size = static_cast<std::uint32_t>(sizeof(candidate));
    candidate.context.struct_size = static_cast<std::uint32_t>(sizeof(candidate.context));
    candidate.target.struct_size = static_cast<std::uint32_t>(sizeof(candidate.target));
    const auto status = api_.submit_body(&request, &candidate);
    if (status == INPUT_NEXT_STATUS_OK) {
        receipt = candidate;
    }
    return status;
}

input_next_status RuntimeClient::clear_body(
    const input_next_effects_body_clear_v1& request) const noexcept {
    return connected() ? api_.clear_body(&request) : INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status RuntimeClient::close_context(const input_next_effects_close_v1& request,
    input_next_effects_close_receipt_v1& receipt) const noexcept {
    if (!connected()) {
        return INPUT_NEXT_STATUS_UNAVAILABLE;
    }
    auto candidate = input_next_effects_close_receipt_v1{};
    candidate.struct_size = static_cast<std::uint32_t>(sizeof(candidate));
    candidate.context.struct_size = static_cast<std::uint32_t>(sizeof(candidate.context));
    candidate.target.struct_size = static_cast<std::uint32_t>(sizeof(candidate.target));
    const auto status = api_.close_context(&request, &candidate);
    if (status == INPUT_NEXT_STATUS_REVOKED) {
        receipt = candidate;
    }
    return status;
}

input_next_status RuntimeClient::ack_close(
    const input_next_effects_close_receipt_v1& receipt) const noexcept {
    return connected() ? api_.ack_close(&receipt) : INPUT_NEXT_STATUS_UNAVAILABLE;
}

input_next_status RuntimeClient::query_progress(const input_next_effects_progress_query_v1& query,
    input_next_effects_progress_v1& progress) const noexcept {
    if (!connected()) {
        return INPUT_NEXT_STATUS_UNAVAILABLE;
    }
    auto candidate = input_next_effects_progress_v1{};
    candidate.struct_size = static_cast<std::uint32_t>(sizeof(candidate));
    candidate.target.struct_size = static_cast<std::uint32_t>(sizeof(candidate.target));
    const auto status = api_.query_progress(&query, &candidate);
    if (status == INPUT_NEXT_STATUS_OK) {
        progress = candidate;
    }
    return status;
}

} // namespace inputnext::adapters
