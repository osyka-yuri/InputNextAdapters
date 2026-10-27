#include <input_next_core.h>
#include <input_next_effects.h>

#include <stddef.h>

#if INPUTNEXTADAPTERS_EXPECTED_BITS != 32 && INPUTNEXTADAPTERS_EXPECTED_BITS != 64
#    error INPUTNEXTADAPTERS_EXPECTED_BITS must be 32 or 64
#endif

_Static_assert(sizeof(void*) * 8U == INPUTNEXTADAPTERS_EXPECTED_BITS, "unexpected pointer width");
_Static_assert(sizeof(input_next_device_target_v1) == 40, "target ABI size changed");
_Static_assert(sizeof(input_next_input_domain_v1) == 48, "domain ABI size changed");
_Static_assert(sizeof(input_next_handle_v1) == 32, "handle ABI size changed");
_Static_assert(sizeof(input_next_reader_v1) == 32, "reader ABI size changed");
_Static_assert(sizeof(input_next_effects_context_v1) == 32, "effects context ABI size changed");
_Static_assert(sizeof(input_next_runtime_info_v1) == 48, "runtime info ABI size changed");
_Static_assert(sizeof(input_next_device_selector_v1) == 96, "selector ABI size changed");
_Static_assert(sizeof(input_next_device_info_v1) == 232, "device info ABI size changed");
_Static_assert(sizeof(input_next_gamepad_state_v1) == 28, "gamepad state ABI size changed");
_Static_assert(sizeof(input_next_input_snapshot_v1) == 112, "snapshot ABI size changed");
_Static_assert(sizeof(input_next_reader_request_v1) == 56, "reader request ABI size changed");
_Static_assert(sizeof(input_next_reader_result_v1) == 112, "reader result ABI size changed");
_Static_assert(sizeof(input_next_effects_trigger_v1) == 20, "trigger ABI size changed");
_Static_assert(sizeof(input_next_effects_rumble_v1) == 8, "rumble ABI size changed");
_Static_assert(sizeof(input_next_effects_led_v1) == 16, "LED ABI size changed");
_Static_assert(sizeof(input_next_effects_capabilities_v1) == 32, "capabilities ABI size changed");
_Static_assert(sizeof(input_next_effects_open_v1) == 48, "effects open ABI size changed");
_Static_assert(sizeof(input_next_effects_context_info_v1) == 40, "context info ABI size changed");
_Static_assert(sizeof(input_next_effects_request_v1) == 120, "effects request ABI size changed");
_Static_assert(sizeof(input_next_effects_receipt_v1) == 88, "effects receipt ABI size changed");
_Static_assert(sizeof(input_next_effects_clear_v1) == 80, "effects clear ABI size changed");
_Static_assert(sizeof(input_next_effects_pcm_v1) == 32, "PCM ABI size changed");
_Static_assert(sizeof(input_next_effects_body_request_v1) == 112, "body request ABI size changed");
_Static_assert(sizeof(input_next_effects_body_receipt_v1) == 120, "body receipt ABI size changed");
_Static_assert(sizeof(input_next_effects_body_clear_v1) == 64, "body clear ABI size changed");
_Static_assert(sizeof(input_next_effects_close_v1) == 48, "close ABI size changed");
_Static_assert(sizeof(input_next_effects_close_receipt_v1) == 96, "close receipt ABI size changed");
_Static_assert(sizeof(input_next_effects_progress_query_v1) == 56,
    "progress query ABI size changed");
_Static_assert(sizeof(input_next_effects_progress_v1) == 80, "progress ABI size changed");

#if INPUTNEXTADAPTERS_EXPECTED_BITS == 64
_Static_assert(sizeof(input_next_core_api_v1) == 64, "x64 core table ABI size changed");
_Static_assert(sizeof(input_next_effects_api_v1) == 88, "x64 effects table ABI size changed");
#else
_Static_assert(sizeof(input_next_core_api_v1) == 36, "x86 core table ABI size changed");
_Static_assert(sizeof(input_next_effects_api_v1) == 48, "x86 effects table ABI size changed");
#endif

_Static_assert(offsetof(input_next_effects_pcm_v1, samples) == 24, "samples ABI offset changed");
_Static_assert(offsetof(input_next_effects_request_v1, context) == 16, "context ABI offset changed");
_Static_assert(offsetof(input_next_effects_request_v1, mutation_sequence) == 48, "mutation_sequence ABI offset changed");
_Static_assert(offsetof(input_next_effects_body_request_v1, pcm) == 80, "pcm ABI offset changed");

int main(void) {
    return 0;
}
