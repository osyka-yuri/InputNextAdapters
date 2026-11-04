#include "body_waveform/body_waveform_test_cases.hpp"
#include "body_waveform/body_waveform_test_support.hpp"
#include "support/test_runner.hpp"

#include <iostream>

namespace {
int run_tests() {
    using namespace skyrim_adapter::body_waveform_tests;

    test_empty_and_bounded_destinations();
    test_determinism_range_stereo_and_dc();
    test_voice_level_waveforms_are_ordered_and_window_safe();
    test_dual_spell_waveform_is_coherent_and_bounded();
    test_melee_swing_hand_gain_and_overlap();
    test_complete_elapsed_time_slices();
    test_sustained_bow_tension_recipes_and_renewal();
    test_five_position_peak_and_hand_isolation();
    test_overloaded_mix_limiting_scale_and_slices();
    test_heartbeat_preserves_authored_cadence();
    test_footstep_waveforms_are_finite_bounded_and_finite_duration();
    test_world_waveforms_are_finite_limited_and_silent_after_expiry();
    test_content_waveforms_are_finite_limited_and_silent_after_expiry();

    const int failures = failure_count();
    if (failures != 0) {
        std::cerr << failures << " body waveform assertion(s) failed\n";
        return 1;
    }
    std::cout << "Skyrim body waveform tests passed\n";
    return 0;
}
} // namespace

int main() {
    return inputnext::test::run_main(__FILE__, [] { return run_tests(); });
}
