#include <input_next_core.h>
#include <input_next_effects.h>

#include <cstddef>
#include <type_traits>

static_assert(INPUTNEXTADAPTERS_EXPECTED_BITS == 32 || INPUTNEXTADAPTERS_EXPECTED_BITS == 64);
static_assert(sizeof(void*) * 8U == INPUTNEXTADAPTERS_EXPECTED_BITS);
static_assert(sizeof(input_next_device_target_v1) == 40);
static_assert(sizeof(input_next_input_domain_v1) == 48);
static_assert(sizeof(input_next_handle_v1) == 32);
static_assert(sizeof(input_next_reader_v1) == 32);
static_assert(sizeof(input_next_effects_context_v1) == 32);
static_assert(sizeof(input_next_runtime_info_v1) == 48);
static_assert(sizeof(input_next_device_selector_v1) == 96);
static_assert(sizeof(input_next_device_info_v1) == 232);
static_assert(sizeof(input_next_gamepad_state_v1) == 28);
static_assert(sizeof(input_next_input_snapshot_v1) == 112);
static_assert(sizeof(input_next_reader_request_v1) == 56);
static_assert(sizeof(input_next_reader_result_v1) == 112);
static_assert(sizeof(input_next_effects_trigger_v1) == 20);
static_assert(sizeof(input_next_effects_rumble_v1) == 8);
static_assert(sizeof(input_next_effects_led_v1) == 16);
static_assert(sizeof(input_next_effects_capabilities_v1) == 32);
static_assert(sizeof(input_next_effects_open_v1) == 48);
static_assert(sizeof(input_next_effects_context_info_v1) == 40);
static_assert(sizeof(input_next_effects_request_v1) == 120);
static_assert(sizeof(input_next_effects_receipt_v1) == 88);
static_assert(sizeof(input_next_effects_clear_v1) == 80);
static_assert(sizeof(input_next_effects_pcm_v1) == 32);
static_assert(sizeof(input_next_effects_body_request_v1) == 112);
static_assert(sizeof(input_next_effects_body_receipt_v1) == 120);
static_assert(sizeof(input_next_effects_body_clear_v1) == 64);
static_assert(sizeof(input_next_effects_close_v1) == 48);
static_assert(sizeof(input_next_effects_close_receipt_v1) == 96);
static_assert(sizeof(input_next_effects_progress_query_v1) == 56);
static_assert(sizeof(input_next_effects_progress_v1) == 80);

#if INPUTNEXTADAPTERS_EXPECTED_BITS == 64
static_assert(sizeof(input_next_core_api_v1) == 64);
static_assert(sizeof(input_next_effects_api_v1) == 88);
#elif INPUTNEXTADAPTERS_EXPECTED_BITS == 32
static_assert(sizeof(input_next_core_api_v1) == 36);
static_assert(sizeof(input_next_effects_api_v1) == 48);
#else
#    error INPUTNEXTADAPTERS_EXPECTED_BITS must be 32 or 64
#endif

static_assert(offsetof(input_next_effects_pcm_v1, samples) == 24);
static_assert(offsetof(input_next_effects_request_v1, context) == 16);
static_assert(offsetof(input_next_effects_request_v1, mutation_sequence) == 48);
static_assert(offsetof(input_next_effects_body_request_v1, pcm) == 80);
static_assert(std::is_same_v<decltype(input_next_core_api_v1::get_runtime_info),
    input_next_status(INPUT_NEXT_CORE_CALL*)(input_next_runtime_info_v1*) noexcept>);
static_assert(std::is_same_v<decltype(input_next_effects_api_v1::open_context),
    input_next_status(INPUT_NEXT_EFFECTS_CALL*)(const input_next_effects_open_v1*, input_next_effects_context_v1*) noexcept>);

int main() {
    return 0;
}
