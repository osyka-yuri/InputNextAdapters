#pragma once

#include <inputnext_adapters/runtime_client.hpp>

#include <filesystem>

namespace inputnext::adapters {
class OutputPort {
protected:
    OutputPort() = default;

public:
    virtual ~OutputPort() = default;

    OutputPort(const OutputPort&) = delete;
    OutputPort& operator=(const OutputPort&) = delete;
    OutputPort(OutputPort&&) = delete;
    OutputPort& operator=(OutputPort&&) = delete;

    virtual input_next_status resolve_target(const input_next_device_selector_v1& selector,
        input_next_device_target_v1& target,
        input_next_effects_capabilities_v1& capabilities) noexcept = 0;
    // Only OK creates a caller-owned context. Other statuses preserve the output and reserve
    // no context requiring cleanup.
    virtual input_next_status open_context(const input_next_effects_open_v1& request,
        input_next_effects_context_v1& context) noexcept = 0;
    virtual input_next_status query_context(const input_next_effects_context_v1& context,
        input_next_effects_context_info_v1& info) noexcept = 0;
    virtual input_next_status submit(const input_next_effects_request_v1& request,
        input_next_effects_receipt_v1& receipt) noexcept = 0;
    virtual input_next_status clear(const input_next_effects_clear_v1& request) noexcept = 0;

    // Implementations consume or copy request.pcm.samples before this call returns.
    virtual input_next_status submit_body(const input_next_effects_body_request_v1& request,
        input_next_effects_body_receipt_v1& receipt) noexcept = 0;
    virtual input_next_status clear_body(
        const input_next_effects_body_clear_v1& request) noexcept = 0;
    virtual input_next_status close_context(const input_next_effects_close_v1& request,
        input_next_effects_close_receipt_v1& receipt) noexcept = 0;
    virtual input_next_status ack_close(
        const input_next_effects_close_receipt_v1& receipt) noexcept = 0;
    virtual input_next_status query_progress(const input_next_effects_progress_query_v1& query,
        input_next_effects_progress_v1& progress) noexcept = 0;
};

class RuntimeOutput final : public OutputPort {
public:
    ConnectResult start(const std::filesystem::path& absolute_runtime_path);
    input_next_status resolve_target(const input_next_device_selector_v1& selector,
        input_next_device_target_v1& target,
        input_next_effects_capabilities_v1& capabilities) noexcept override;
    input_next_status open_context(const input_next_effects_open_v1& request,
        input_next_effects_context_v1& context) noexcept override;
    input_next_status query_context(const input_next_effects_context_v1& context,
        input_next_effects_context_info_v1& info) noexcept override;
    input_next_status submit(const input_next_effects_request_v1& request,
        input_next_effects_receipt_v1& receipt) noexcept override;
    input_next_status clear(const input_next_effects_clear_v1& request) noexcept override;
    input_next_status submit_body(const input_next_effects_body_request_v1& request,
        input_next_effects_body_receipt_v1& receipt) noexcept override;
    input_next_status clear_body(const input_next_effects_body_clear_v1& request) noexcept override;
    input_next_status close_context(const input_next_effects_close_v1& request,
        input_next_effects_close_receipt_v1& receipt) noexcept override;
    input_next_status ack_close(
        const input_next_effects_close_receipt_v1& receipt) noexcept override;
    input_next_status query_progress(const input_next_effects_progress_query_v1& query,
        input_next_effects_progress_v1& progress) noexcept override;

private:
    RuntimeClient client_;
};
} // namespace inputnext::adapters
