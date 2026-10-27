#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <inputnext_adapters/body_program.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <ranges>
#include <span>

namespace {
using namespace std::chrono_literals;
using inputnext::adapters::BodyProgram;
using inputnext::adapters::BodyVoice;
using inputnext::adapters::MonoTime;

constexpr std::size_t channels_per_frame = 2;
constexpr std::size_t frames_per_millisecond = inputnext::adapters::body_pcm_sample_rate / 1000U;

BodyVoice voice(bool held = false) {
    BodyVoice result{};
    result.wave.frequency = 110.0F;
    result.wave.second_frequency = 55.0F;
    result.wave.second_mix = 0.12F;
    result.wave.level = 0.5F;
    result.wave.noise = 0.01F;
    result.wave.attack_ms = 1.0F;
    result.wave.release_ms = 10.0F;
    result.wave.decay_per_second = 2.0F;
    result.wave.modulation_hz = 1.25F;
    result.wave.first_pulse_ms = 20;
    result.wave.pulse_gap_ms = 15;
    result.wave.second_pulse_ms = 20;
    result.started_at = MonoTime{};
    result.duration_ms = 100;
    result.stable_id = 4;
    result.sequence = 5;
    result.source_id = 6;
    result.priority = 7;
    result.scale = 0.8F;
    result.hd_scale = 1.25F;
    result.duration_scale = 1.5F;
    result.position_gain = 0.5F;
    result.channel_mask = 0x1U;
    result.held = held;
    return result;
}

BodyProgram one_voice(BodyVoice voice_value) {
    BodyProgram result{};
    result.voices[0] = voice_value;
    return result;
}

constexpr std::size_t samples_for_frames(std::size_t frames) noexcept {
    return frames * channels_per_frame;
}

constexpr std::size_t samples_for_milliseconds(std::size_t milliseconds) noexcept {
    return samples_for_frames(milliseconds * frames_per_millisecond);
}

constexpr std::size_t sample_index(std::size_t frame, std::size_t channel = 0) noexcept {
    return (frame * channels_per_frame) + channel;
}

void render_test_window(const BodyProgram& program,
    MonoTime begins_at,
    std::span<float> destination) {
    const auto result = inputnext::adapters::build_body_pcm(program, begins_at, destination);
    INFO("test renderer fills its requested PCM window");
    REQUIRE(
        static_cast<std::size_t>(result.frame_count) == destination.size() / channels_per_frame);
}

TEST_CASE("effects.body_program.exact_millisecond_noise_boundaries",
    "[unit][effects][body_program]") {
    BodyVoice textured = voice();
    textured.wave.first_pulse_ms = 0;
    textured.wave.pulse_gap_ms = 0;
    textured.wave.second_pulse_ms = 0;
    const BodyProgram program = one_voice(textured);

    struct Boundary {
        std::chrono::milliseconds start;
        std::size_t split_frame;
    };

    constexpr std::array boundaries{
        Boundary{.start = 9ms, .split_frame = 81},
        Boundary{.start = 18ms, .split_frame = 162},
        Boundary{.start = 36ms, .split_frame = 321},
    };
    constexpr std::size_t overlap_frames = 32;
    std::array<float, samples_for_frames(321 + overlap_frames)> whole_window{};
    std::array<float, samples_for_frames(overlap_frames)> overlap_window{};

    for (const Boundary& boundary : boundaries) {
        const MonoTime start = MonoTime{} + boundary.start;
        const auto first_length = samples_for_frames(boundary.split_frame + overlap_frames);
        render_test_window(program, start, std::span<float>{whole_window}.first(first_length));
        const MonoTime overlap_start =
            start + std::chrono::duration_cast<MonoTime::duration>(std::chrono::duration<double>(
                        static_cast<double>(boundary.split_frame) /
                        static_cast<double>(inputnext::adapters::body_pcm_sample_rate)));
        render_test_window(program, overlap_start, overlap_window);
        INFO("noise follows floor of each actual age across exact-millisecond overlapping windows");
        CHECK(std::ranges::all_of(std::views::iota(std::size_t{0}, overlap_frames),
            [&](std::size_t frame) {
                const float whole = whole_window[sample_index(boundary.split_frame + frame)];
                const float overlap = overlap_window[sample_index(frame)];
                return std::abs(whole - overlap) < 1.0e-5F;
            }));
    }
}

TEST_CASE("effects.body_program.identity", "[unit][effects][body_program]") {
    const BodyVoice finite = voice();
    const BodyProgram original = one_voice(finite);

    INFO("a finite physical program has stable identity");
    CHECK(inputnext::adapters::same_body_program_identity(original, original));

    BodyVoice changed = finite;
    changed.duration_ms += 1;
    INFO("finite voice duration is part of identity");
    CHECK_FALSE(inputnext::adapters::same_body_program_identity(original, one_voice(changed)));

    BodyVoice held = voice(true);
    BodyProgram held_original = one_voice(held);
    held.duration_ms += 250;
    INFO("held voice horizon renewal keeps identity");
    CHECK(inputnext::adapters::same_body_program_identity(held_original, one_voice(held)));

    changed = finite;
    changed.wave.frequency += 1.0F;
    INFO("wave frequency is part of physical identity");
    CHECK_FALSE(inputnext::adapters::same_body_program_identity(original, one_voice(changed)));

    changed = finite;
    changed.channel_mask = 0x2U;
    INFO("stereo routing is part of physical identity");
    CHECK_FALSE(inputnext::adapters::same_body_program_identity(original, one_voice(changed)));

    changed = finite;
    changed.sequence += 1;
    INFO("sequence provenance is part of physical identity");
    CHECK_FALSE(inputnext::adapters::same_body_program_identity(original, one_voice(changed)));

    changed = finite;
    changed.source_id += 1;
    INFO("source provenance is part of physical identity");
    CHECK_FALSE(inputnext::adapters::same_body_program_identity(original, one_voice(changed)));

    changed = finite;
    changed.priority += 1;
    INFO("priority provenance is part of physical identity");
    CHECK_FALSE(inputnext::adapters::same_body_program_identity(original, one_voice(changed)));

    changed = finite;
    changed.stable_id += 1;
    INFO("stable noise seed is part of physical identity");
    CHECK_FALSE(inputnext::adapters::same_body_program_identity(original, one_voice(changed)));

    changed = finite;
    changed.wave.frequency = std::numeric_limits<float>::quiet_NaN();
    INFO("wave fields use exact value equality");
    CHECK_FALSE(
        inputnext::adapters::same_body_program_identity(one_voice(changed), one_voice(changed)));
}

TEST_CASE("effects.body_program.lifetime_queries", "[unit][effects][body_program]") {
    BodyVoice finite = voice();
    BodyProgram program = one_voice(finite);
    const MonoTime start{};

    INFO("future voice is not active");
    CHECK_FALSE(inputnext::adapters::body_program_active(program, start - 1ms));

    INFO("voice is active before its finite horizon");
    CHECK(inputnext::adapters::body_program_active(program, start + 99ms));

    INFO("voice expires at its finite horizon");
    CHECK_FALSE(inputnext::adapters::body_program_active(program, start + 100ms));

    INFO("finite transient is not reported as held");
    CHECK_FALSE(inputnext::adapters::body_program_has_held(program, start + 10ms));

    finite.held = true;
    program = one_voice(finite);

    INFO("active held voice is reported");
    CHECK(inputnext::adapters::body_program_has_held(program, start + 10ms));

    INFO("expired held voice is not reported");
    CHECK_FALSE(inputnext::adapters::body_program_has_held(program, start + 100ms));
}

TEST_CASE("effects.body_program.rendering_bounds_and_slices", "[unit][effects][body_program]") {
    BodyVoice voice_value = voice(true);
    BodyProgram program = one_voice(voice_value);
    std::array<float, 12> output{};
    output.fill(3.0F);
    const auto window = inputnext::adapters::build_body_pcm(program, MonoTime{}, output);

    INFO("renderer returns bounded frame count and active held state");
    CHECK(window.frame_count == 6);
    CHECK(window.contains_held);

    INFO("rendered voice samples remain finite");
    CHECK(std::ranges::all_of(output, [](float sample) { return std::isfinite(sample); }));

    std::array<float, 3> odd_destination{3.0F, 3.0F, 3.0F};
    const auto odd = inputnext::adapters::build_body_pcm(program, MonoTime{}, odd_destination);

    INFO("renderer leaves an unmatched channel sample in the caller tail untouched");
    CHECK(odd.frame_count == 1);
    CHECK(odd_destination[2] == 3.0F);

    std::array<float, 2> expired{};
    const auto silence = inputnext::adapters::build_body_pcm(program, MonoTime{} + 100ms, expired);

    INFO("expired programs render silence and clear only their output window");
    CHECK_FALSE(silence.contains_held);
    CHECK(expired[0] == 0.0F);
    CHECK(expired[1] == 0.0F);

    BodyVoice delayed_voice = voice(true);
    delayed_voice.started_at = MonoTime{} + 1ms;
    delayed_voice.duration_ms = 2;
    delayed_voice.duration_scale = 1.0F;
    delayed_voice.wave.second_frequency = 0.0F;
    delayed_voice.wave.noise = 0.0F;
    delayed_voice.wave.decay_per_second = 0.0F;
    delayed_voice.wave.modulation_hz = 0.0F;
    delayed_voice.wave.first_pulse_ms = 0;
    delayed_voice.wave.pulse_gap_ms = 0;
    delayed_voice.wave.second_pulse_ms = 0;
    delayed_voice.wave.attack_ms = 0.0F;
    delayed_voice.wave.release_ms = 0.0F;
    std::array<float, samples_for_milliseconds(4)> delayed_output{};
    const auto delayed =
        inputnext::adapters::build_body_pcm(one_voice(delayed_voice), MonoTime{}, delayed_output);

    INFO("a partially overlapping delayed voice renders only within its exact lifetime");
    CHECK(delayed.contains_held);
    CHECK(std::ranges::all_of(std::span{delayed_output}.first(samples_for_milliseconds(1)),
        [](float sample) { return sample == 0.0F; }));
    CHECK(delayed_output[sample_index(49U)] != 0.0F);
    CHECK(std::ranges::all_of(std::span{delayed_output}.subspan(samples_for_milliseconds(3)),
        [](float sample) { return sample == 0.0F; }));

    std::array<float, 0> empty{};
    INFO("empty destination is safe");
    CHECK(inputnext::adapters::build_body_pcm(program, MonoTime{}, empty).frame_count == 0);
}

TEST_CASE("effects.body_program.rendering_recurrences_and_absolute_age",
    "[unit][effects][body_program]") {
    BodyVoice textured = voice();
    textured.wave.first_pulse_ms = 0;
    textured.wave.pulse_gap_ms = 0;
    textured.wave.second_pulse_ms = 0;
    const BodyProgram textured_program = one_voice(textured);
    const MonoTime fractional_start =
        MonoTime{} + 10ms +
        std::chrono::duration_cast<MonoTime::duration>(std::chrono::duration<double>(
            0.5 / static_cast<double>(inputnext::adapters::body_pcm_sample_rate)));
    std::array<float, samples_for_frames(96)> first_window{};
    std::array<float, samples_for_frames(96)> repeated_window{};
    std::array<float, samples_for_frames(64)> overlapping_window{};
    render_test_window(textured_program, fractional_start, first_window);
    render_test_window(textured_program, fractional_start, repeated_window);
    const MonoTime overlap_start =
        fractional_start +
        std::chrono::duration_cast<MonoTime::duration>(std::chrono::duration<double>(
            32.0 / static_cast<double>(inputnext::adapters::body_pcm_sample_rate)));
    render_test_window(textured_program, overlap_start, overlapping_window);

    INFO("identical fractional-age input renders bit-identically");
    CHECK(first_window == repeated_window);

    INFO("overlapping windows preserve fractional carrier and rolling-noise phase");
    CHECK(std::ranges::all_of(std::views::iota(0U, 64U), [&](std::uint32_t frame) {
        return std::abs(first_window[sample_index(static_cast<std::size_t>(frame) + 32U)] -
                        overlapping_window[sample_index(static_cast<std::size_t>(frame))]) <
               1.0e-5F;
    }));

    BodyVoice single_carrier = textured;
    single_carrier.wave.second_frequency = 0.0F;
    single_carrier.wave.second_mix = 0.5F;
    single_carrier.wave.noise = 0.0F;
    single_carrier.wave.decay_per_second = 0.0F;
    single_carrier.wave.modulation_hz = 0.0F;
    single_carrier.wave.attack_ms = 0.0F;
    single_carrier.wave.release_ms = 0.0F;
    std::array<float, 2> single_carrier_sample{};
    render_test_window(one_voice(single_carrier), MonoTime{} + 10ms, single_carrier_sample);
    const double expected = std::sin(2.0 * std::numbers::pi * 110.0 * 0.010) * 0.25;

    INFO("noise-free single-carrier output retains its absolute sine phase");
    CHECK(std::abs(static_cast<double>(single_carrier_sample[0]) - expected) < 1.0e-6);

    BodyVoice pulsed = single_carrier;
    pulsed.wave.first_pulse_ms = 3;
    pulsed.wave.pulse_gap_ms = 3;
    pulsed.wave.second_pulse_ms = 3;
    pulsed.wave.second_frequency = 55.0F;
    pulsed.duration_scale = 1.0F;
    std::array<float, samples_for_milliseconds(9)> pulse_output{};
    render_test_window(one_voice(pulsed), MonoTime{}, pulse_output);

    INFO("oscillator and decay state cross the silent gap without producing samples");
    CHECK(std::ranges::all_of(
        std::span{pulse_output}.subspan(samples_for_milliseconds(4), samples_for_milliseconds(2)),
        [](float sample) { return sample == 0.0F; }));

    INFO("the second pulse resumes at its absolute carrier phase");
    CHECK(std::ranges::any_of(std::span{pulse_output}.subspan(samples_for_milliseconds(7)),
        [](float sample) { return sample != 0.0F; }));

    BodyVoice long_held = textured;
    long_held.held = true;
    long_held.duration_ms = 610'000;
    const MonoTime long_age = MonoTime{} + 600'000ms;
    const BodyProgram old_start_program = one_voice(long_held);
    long_held.started_at = MonoTime{} - 600'000ms;
    const BodyProgram shifted_start_program = one_voice(long_held);
    std::array<float, samples_for_frames(64)> old_start_window{};
    std::array<float, samples_for_frames(64)> shifted_start_window{};
    render_test_window(old_start_program, long_age, old_start_window);
    render_test_window(shifted_start_program, MonoTime{}, shifted_start_window);

    INFO("long-held phases initialize from absolute age independently of render-window history");
    CHECK(old_start_window == shifted_start_window);
}
} // namespace
