#pragma once

namespace skyrim_adapter::body_waveform_tests {

void test_empty_and_bounded_destinations();
void test_determinism_range_stereo_and_dc();
void test_complete_elapsed_time_slices();

void test_dual_spell_waveform_is_coherent_and_bounded();
void test_melee_swing_hand_gain_and_overlap();
void test_five_position_peak_and_hand_isolation();
void test_overloaded_mix_limiting_scale_and_slices();

void test_voice_level_waveforms_are_ordered_and_window_safe();
void test_sustained_bow_tension_recipes_and_renewal();
void test_heartbeat_preserves_authored_cadence();

void test_footstep_waveforms_are_finite_bounded_and_finite_duration();
void test_world_waveforms_are_finite_limited_and_silent_after_expiry();
void test_content_waveforms_are_finite_limited_and_silent_after_expiry();

} // namespace skyrim_adapter::body_waveform_tests
