#include <inputnext_adapters/output.hpp>
#include <inputnext_adapters/runtime_client.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <filesystem>

namespace inputnext::adapters {
ConnectResult RuntimeOutput::start(const std::filesystem::path& absolute_runtime_path) {
    return client_.connect(absolute_runtime_path);
}

input_next_status RuntimeOutput::resolve_target(const input_next_device_selector_v1& selector,
    input_next_device_target_v1& target,
    input_next_effects_capabilities_v1& capabilities) noexcept {
    return client_.resolve_target(selector, target, capabilities);
}

input_next_status RuntimeOutput::open_context(const input_next_effects_open_v1& request,
    input_next_effects_context_v1& context) noexcept {
    return client_.open_context(request, context);
}

input_next_status RuntimeOutput::query_context(const input_next_effects_context_v1& context,
    input_next_effects_context_info_v1& info) noexcept {
    return client_.query_context(context, info);
}

input_next_status RuntimeOutput::submit(const input_next_effects_request_v1& request,
    input_next_effects_receipt_v1& receipt) noexcept {
    return client_.submit(request, receipt);
}

input_next_status RuntimeOutput::clear(const input_next_effects_clear_v1& request) noexcept {
    return client_.clear(request);
}

input_next_status RuntimeOutput::submit_body(const input_next_effects_body_request_v1& request,
    input_next_effects_body_receipt_v1& receipt) noexcept {
    return client_.submit_body(request, receipt);
}

input_next_status RuntimeOutput::clear_body(
    const input_next_effects_body_clear_v1& request) noexcept {
    return client_.clear_body(request);
}

input_next_status RuntimeOutput::close_context(const input_next_effects_close_v1& request,
    input_next_effects_close_receipt_v1& receipt) noexcept {
    return client_.close_context(request, receipt);
}

input_next_status RuntimeOutput::ack_close(
    const input_next_effects_close_receipt_v1& receipt) noexcept {
    return client_.ack_close(receipt);
}

input_next_status RuntimeOutput::query_progress(const input_next_effects_progress_query_v1& query,
    input_next_effects_progress_v1& progress) noexcept {
    return client_.query_progress(query, progress);
}
} // namespace inputnext::adapters
