#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>

#include <input_next_effects.h>

namespace inputnext::adapters {

enum class ConnectError {
    none,
    already_connected,
    invalid_path,
    load_failed,
    query_missing,
    query_unavailable,
    incompatible_api,
};

struct ConnectResult {
    ConnectError error = ConnectError::none;
    std::uint32_t native_error = 0;

    explicit operator bool() const noexcept { return error == ConnectError::none; }
};

// Serialize lifetime changes against calls; the caller owns context close/ACK cleanup.
class RuntimeClient {
public:
    RuntimeClient() noexcept = default;
    ~RuntimeClient() noexcept;

    RuntimeClient(const RuntimeClient&) = delete;
    RuntimeClient& operator=(const RuntimeClient&) = delete;

    // Moving leaves the source disconnected; its public operations remain valid in that state.
    RuntimeClient(RuntimeClient&& other) noexcept;
    RuntimeClient& operator=(RuntimeClient&& other) noexcept;

    ConnectResult connect(const std::filesystem::path& path);
    void disconnect() noexcept;
    [[nodiscard]] bool connected() const noexcept;

    input_next_status resolve_target(const input_next_device_selector_v1& selector,
        input_next_device_target_v1& target,
        input_next_effects_capabilities_v1& capabilities) const noexcept;
    input_next_status open_context(const input_next_effects_open_v1& request,
        input_next_effects_context_v1& context) const noexcept;
    input_next_status query_context(const input_next_effects_context_v1& context,
        input_next_effects_context_info_v1& info) const noexcept;
    [[nodiscard]] input_next_status submit(const input_next_effects_request_v1& request,
        input_next_effects_receipt_v1& receipt) const noexcept;
    [[nodiscard]] input_next_status clear(
        const input_next_effects_clear_v1& request) const noexcept;
    input_next_status submit_body(const input_next_effects_body_request_v1& request,
        input_next_effects_body_receipt_v1& receipt) const noexcept;
    [[nodiscard]] input_next_status clear_body(
        const input_next_effects_body_clear_v1& request) const noexcept;
    input_next_status close_context(const input_next_effects_close_v1& request,
        input_next_effects_close_receipt_v1& receipt) const noexcept;
    [[nodiscard]] input_next_status ack_close(
        const input_next_effects_close_receipt_v1& receipt) const noexcept;
    input_next_status query_progress(const input_next_effects_progress_query_v1& query,
        input_next_effects_progress_v1& progress) const noexcept;

private:
    void* module_{};
    input_next_effects_api_v1 api_{};
};

[[nodiscard]] input_next_effects_request_v1 make_request(
    const input_next_effects_context_v1& context,
    std::uint64_t mutation_sequence,
    std::uint32_t channel_mask,
    std::chrono::milliseconds ttl) noexcept;

} // namespace inputnext::adapters
