#pragma once

#include <input_next_effects.h>

#include <array>
#include <cstdint>

struct fake_runtime_state {
    std::uint32_t struct_size;
    std::uint32_t fixture_id;
    std::uint32_t query_calls;
    std::uint32_t last_requested_abi;
    input_next_status resolve_status;
    input_next_status open_status;
    input_next_status query_context_status;
    input_next_status submit_status;
    input_next_status clear_status;
    input_next_status submit_body_status;
    input_next_status clear_body_status;
    input_next_status close_status;
    input_next_status ack_close_status;
    input_next_status query_progress_status;
    std::uint32_t body_effective_route;
    std::uint32_t body_fallback_reason;
    std::uint32_t resolve_calls;
    std::uint32_t open_calls;
    std::uint32_t query_context_calls;
    std::uint32_t submit_calls;
    std::uint32_t clear_calls;
    std::uint32_t submit_body_calls;
    std::uint32_t clear_body_calls;
    std::uint32_t close_calls;
    std::uint32_t ack_close_calls;
    std::uint32_t query_progress_calls;
    std::uint32_t last_slot_index;
    std::uint32_t last_target_input_size;
    std::uint32_t last_capabilities_input_size;
    std::uint32_t last_body_receipt_input_size;
    std::uint32_t copied_body_sample_count;
    std::uint64_t next_context_id;
    input_next_device_selector_v1 last_selector;
    input_next_device_target_v1 last_target;
    input_next_effects_capabilities_v1 last_capabilities;
    input_next_effects_open_v1 last_open;
    input_next_effects_context_v1 last_context;
    input_next_effects_context_info_v1 last_context_info;
    input_next_effects_request_v1 last_submit;
    input_next_effects_receipt_v1 last_receipt;
    input_next_effects_clear_v1 last_clear;
    input_next_effects_body_request_v1 last_submit_body;
    input_next_effects_body_receipt_v1 last_body_receipt;
    input_next_effects_body_clear_v1 last_clear_body;
    input_next_effects_close_v1 last_close;
    input_next_effects_close_receipt_v1 last_close_receipt;
    input_next_effects_close_receipt_v1 last_acknowledgement;
    input_next_effects_progress_query_v1 last_progress_query;
    input_next_effects_progress_v1 last_progress;
    std::array<float, 16> copied_body_samples;
};

using fake_runtime_reset_fn = void(INPUT_NEXT_EFFECTS_CALL*)() INPUT_NEXT_EFFECTS_NOEXCEPT;
using fake_runtime_get_state_fn = fake_runtime_state*(
    INPUT_NEXT_EFFECTS_CALL*)() INPUT_NEXT_EFFECTS_NOEXCEPT;
