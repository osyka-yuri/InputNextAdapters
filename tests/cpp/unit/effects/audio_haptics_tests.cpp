#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/audio_haptics.hpp>
#include <inputnext_adapters/body_program.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <numbers>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace std::chrono_literals;
using namespace inputnext::adapters;

void append_u16(std::vector<std::byte>& bytes, std::uint16_t value) {
    const auto widened = static_cast<std::uint32_t>(value);
    bytes.push_back(static_cast<std::byte>(widened & 0xFFU));
    bytes.push_back(static_cast<std::byte>((widened >> 8U) & 0xFFU));
}

void append_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void append_f32(std::vector<std::byte>& bytes, float value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

std::uint32_t crc32(std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xFFFF'FFFFU;
    for (const std::byte value : bytes) {
        crc ^= std::to_integer<std::uint8_t>(value);
        for (unsigned int bit = 0; bit < 8U; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB8'8320U & mask);
        }
    }
    return crc ^ 0xFFFF'FFFFU;
}

AudioHapticProfile profile() {
    AudioHapticProfile result;
    result.frequency = 100.0F;
    result.second_frequency = 200.0F;
    result.envelope.enabled = true;
    for (std::size_t index = 2; index < audio_haptic_knot_count - 2U; ++index) {
        result.envelope.primary[index] = 0.4F;
        result.envelope.secondary[index] = 0.3F;
    }
    return result;
}

std::vector<std::byte> bank_bytes(const AudioHapticProfile& value = profile(),
    std::string_view section = "Body.Melee.Unarmed.Hit") {
    std::vector<std::byte> payload;
    append_u16(payload, static_cast<std::uint16_t>(section.size()));
    for (const char character : section) {
        payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    append_f32(payload, value.frequency);
    append_f32(payload, value.second_frequency);
    for (const float gain : value.envelope.primary) {
        append_f32(payload, gain);
    }
    for (const float gain : value.envelope.secondary) {
        append_f32(payload, gain);
    }

    std::vector<std::byte> bytes{std::byte{'I'}, std::byte{'N'}, std::byte{'H'}, std::byte{'B'}};
    append_u32(bytes, 1U);
    append_u32(bytes, static_cast<std::uint32_t>(AudioHapticGame::skyrim_se));
    append_u32(bytes, 1U);
    append_u32(bytes, static_cast<std::uint32_t>(payload.size()));
    append_u32(bytes, crc32(payload));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

void rewrite_crc(std::vector<std::byte>& bytes) {
    const auto checksum = crc32(std::span<const std::byte>{bytes}.subspan(24U));
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        bytes[20U + (shift / 8U)] = static_cast<std::byte>((checksum >> shift) & 0xFFU);
    }
}

BodyVoice audio_voice(float duration_scale = 1.0F) {
    const AudioHapticProfile audio = profile();
    BodyVoice voice;
    voice.wave.frequency = audio.frequency;
    voice.wave.second_frequency = audio.second_frequency;
    voice.wave.second_mix = 0.8F;
    voice.wave.level = 0.8F;
    voice.wave.noise = 0.7F;
    voice.wave.attack_ms = 8.0F;
    voice.wave.release_ms = 6.0F;
    voice.wave.decay_per_second = 5.0F;
    voice.wave.modulation_hz = 4.0F;
    voice.wave.first_pulse_ms = 5U;
    voice.wave.pulse_gap_ms = 3U;
    voice.wave.second_pulse_ms = 4U;
    voice.wave.audio_envelope = audio.envelope;
    voice.started_at = MonoTime{};
    voice.duration_ms = 20U;
    voice.scale = 1.0F;
    voice.hd_scale = 1.0F;
    voice.duration_scale = duration_scale;
    voice.position_gain = 1.0F;
    voice.channel_mask = 0x3U;
    return voice;
}

BodyProgram one_voice(const BodyVoice& voice) {
    BodyProgram program;
    program.voices[0] = voice;
    return program;
}

std::size_t stereo_index(std::size_t frame, std::size_t channel = 0U) {
    return (frame * 2U) + channel;
}

AudioHapticBankResult parse_shared_fixture(std::string_view name) {
    const auto path = std::filesystem::path{INPUTNEXT_AUDIO_HAPTIC_FIXTURE_DIR} / name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("failed to open shared audio-haptics bank fixture");
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    return parse_audio_haptic_bank(std::as_bytes(std::span<const char>{bytes}));
}

} // namespace

TEST_CASE("effects.audio_haptics.bank_format", "[unit][effects][audio_haptics]") {
    const auto bytes = bank_bytes();
    const AudioHapticBankResult parsed = parse_audio_haptic_bank(bytes);

    INFO("valid v1 banks retain their game tag, key, carriers, and enabled value envelope");
    REQUIRE(parsed.status == AudioHapticBankStatus::loaded);
    REQUIRE(parsed.bank.game == AudioHapticGame::skyrim_se);
    REQUIRE(parsed.bank.entries.size() == 1U);
    CHECK(parsed.bank.entries[0].section == "Body.Melee.Unarmed.Hit");
    CHECK(parsed.bank.entries[0].profile == profile());

    auto corrupted = bytes;
    corrupted.back() ^= std::byte{0x01};
    INFO("payload CRC failure rejects the entire bank");
    CHECK(parse_audio_haptic_bank(corrupted).status == AudioHapticBankStatus::invalid);

    INFO("truncation and trailing data reject the exact-length file");
    CHECK(parse_audio_haptic_bank(std::span<const std::byte>{bytes}.first(bytes.size() - 1U))
              .status == AudioHapticBankStatus::invalid);
    auto trailing = bytes;
    trailing.push_back(std::byte{0});
    CHECK(parse_audio_haptic_bank(trailing).status == AudioHapticBankStatus::invalid);

    INFO("unsupported versions, game tags, record caps, and payload caps are rejected");
    auto bad_version = bytes;
    bad_version[4] = std::byte{2};
    CHECK(parse_audio_haptic_bank(bad_version).status == AudioHapticBankStatus::invalid);
    auto bad_game = bytes;
    bad_game[8] = std::byte{4};
    CHECK(parse_audio_haptic_bank(bad_game).status == AudioHapticBankStatus::invalid);
    auto too_many_records = bytes;
    too_many_records[12] = std::byte{129};
    CHECK(parse_audio_haptic_bank(too_many_records).status == AudioHapticBankStatus::invalid);
    auto too_large = bytes;
    too_large[16] = std::byte{1};
    too_large[17] = std::byte{0};
    too_large[18] = std::byte{1};
    CHECK(parse_audio_haptic_bank(too_large).status == AudioHapticBankStatus::invalid);

    INFO("NaN gains and duplicate keys reject the complete bank");
    AudioHapticProfile nan_profile = profile();
    nan_profile.envelope.primary[8] = std::numeric_limits<float>::quiet_NaN();
    CHECK(
        parse_audio_haptic_bank(bank_bytes(nan_profile)).status == AudioHapticBankStatus::invalid);
    AudioHapticProfile nan_frequency = profile();
    nan_frequency.frequency = std::numeric_limits<float>::quiet_NaN();
    CHECK(parse_audio_haptic_bank(bank_bytes(nan_frequency)).status ==
          AudioHapticBankStatus::invalid);
    CHECK(parse_audio_haptic_bank(bank_bytes(profile(), std::string(97U, 'A'))).status ==
          AudioHapticBankStatus::invalid);
    auto duplicate = bytes;
    const std::size_t record_size = duplicate.size() - 24U;
    duplicate[12] = std::byte{2};
    const std::vector<std::byte> record(duplicate.begin() + 24,
        duplicate.begin() + static_cast<std::ptrdiff_t>(24U + record_size));
    duplicate.insert(duplicate.end(), record.begin(), record.end());
    const auto doubled_payload = static_cast<std::uint32_t>(record_size * 2U);
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        duplicate[16U + (shift / 8U)] = static_cast<std::byte>((doubled_payload >> shift) & 0xFFU);
    }
    rewrite_crc(duplicate);
    CHECK(parse_audio_haptic_bank(duplicate).status == AudioHapticBankStatus::invalid);
}

TEST_CASE("effects.audio_haptics.shared_v1_binary_fixtures_have_identical_ordering_rules",
    "[unit][effects][audio_haptics]") {
    const auto sorted = parse_shared_fixture("sorted.audiohaptics");
    REQUIRE(sorted.status == AudioHapticBankStatus::loaded);
    REQUIRE(sorted.bank.game == AudioHapticGame::skyrim_se);
    REQUIRE(sorted.bank.entries.size() == 2U);
    CHECK(sorted.bank.entries[0].section == "Body.A");
    CHECK(sorted.bank.entries[0].profile.frequency == 78.0F);
    CHECK(sorted.bank.entries[1].section == "Body.B");
    CHECK(sorted.bank.entries[1].profile.frequency == 188.0F);
    CHECK(parse_shared_fixture("descending.audiohaptics").status == AudioHapticBankStatus::invalid);
    CHECK(parse_shared_fixture("duplicate.audiohaptics").status == AudioHapticBankStatus::invalid);
}

TEST_CASE("effects.audio_haptics.profile_application", "[unit][effects][audio_haptics]") {
    WaveRecipe recipe{
        .frequency = 65.0F,
        .second_frequency = 55.0F,
        .second_mix = 0.42F,
        .level = 0.71F,
        .noise = 0.18F,
        .attack_ms = 7.0F,
        .release_ms = 8.0F,
        .decay_per_second = 1.7F,
        .modulation_hz = 3.5F,
        .first_pulse_ms = 9U,
        .pulse_gap_ms = 4U,
        .second_pulse_ms = 3U,
    };
    const auto legacy = recipe;
    const auto selected = profile();

    REQUIRE(apply_audio_haptic_profile(recipe, selected));
    CHECK(recipe.frequency == selected.frequency);
    CHECK(recipe.second_frequency == selected.second_frequency);
    CHECK(recipe.audio_envelope == selected.envelope);
    CHECK(recipe.level == legacy.level);
    CHECK(recipe.noise == legacy.noise);
    CHECK(recipe.attack_ms == legacy.attack_ms);
    CHECK(recipe.release_ms == legacy.release_ms);
    CHECK(recipe.decay_per_second == legacy.decay_per_second);
    CHECK(recipe.modulation_hz == legacy.modulation_hz);
    CHECK(recipe.first_pulse_ms == legacy.first_pulse_ms);
    CHECK(recipe.pulse_gap_ms == legacy.pulse_gap_ms);
    CHECK(recipe.second_pulse_ms == legacy.second_pulse_ms);

    WaveRecipe empty;
    INFO("an empty base recipe and invalid profile are left untouched");
    CHECK_FALSE(apply_audio_haptic_profile(empty, selected));
    AudioHapticProfile invalid = selected;
    invalid.envelope.primary.back() = 0.1F;
    const auto before = recipe;
    CHECK_FALSE(apply_audio_haptic_profile(recipe, invalid));
    CHECK(recipe == before);
}

TEST_CASE("effects.audio_haptics.value_interpolation_and_legacy_isolation",
    "[unit][effects][audio_haptics]") {
    BodyVoice voice = audio_voice();
    voice.duration_ms = 20U;
    voice.wave.audio_envelope.primary[16] = 0.2F;
    voice.wave.audio_envelope.primary[17] = 0.5F;
    voice.wave.audio_envelope.secondary[16] = 0.1F;
    voice.wave.audio_envelope.secondary[17] = 0.4F;
    const BodyProgram program = one_voice(voice);
    std::array<float, std::size_t{960U} * 2U> output{};
    const auto result = build_body_pcm(program, MonoTime{}, output);
    REQUIRE(result.frame_count == 960U);

    constexpr std::size_t sample_frame = 504U;
    const double age_seconds = static_cast<double>(sample_frame) / 48'000.0;
    const double primary = std::sin(2.0 * std::numbers::pi * 100.0 * age_seconds);
    const double secondary = std::sin(2.0 * std::numbers::pi * 200.0 * age_seconds);
    const double expected = ((primary * 0.2825) + (secondary * 0.1825)) * 0.8;
    INFO("transient mode mixes independently interpolated value-owned bands and keeps level");
    CHECK(std::abs(static_cast<double>(output[stereo_index(sample_frame)]) - expected) < 2.0e-4);
    CHECK(output[stereo_index(sample_frame)] == output[stereo_index(sample_frame, 1U)]);
    CHECK(output[0] == 0.0F);
    CHECK(std::abs(output[stereo_index(959U)]) < 1.0e-4F);

    BodyVoice disabled = voice;
    disabled.wave.audio_envelope = {};
    BodyVoice held = voice;
    held.held = true;
    BodyVoice held_disabled = held;
    held_disabled.wave.audio_envelope = {};
    BodyVoice invalid = voice;
    invalid.wave.audio_envelope.primary.back() = 0.2F;
    const auto render = [&](const BodyVoice& candidate) {
        std::array<float, std::size_t{520U} * 2U> samples{};
        (void)build_body_pcm(one_voice(candidate), MonoTime{}, samples);
        return samples;
    };
    INFO("valid transient envelopes select the new mode; held and invalid values fall back");
    CHECK(render(voice) != render(disabled));
    CHECK(render(held) == render(held_disabled));
    BodyVoice overridden = voice;
    overridden.wave.frequency = 15.0F;
    BodyVoice held_override = overridden;
    held_override.held = true;
    CHECK(render(overridden) != render(held_override));
    BodyVoice invalid_disabled = invalid;
    invalid_disabled.wave.audio_envelope = {};
    CHECK(render(invalid) == render(invalid_disabled));
}

TEST_CASE("effects.audio_haptics.source_pulses_stretch_once_across_delivered_duration",
    "[unit][effects][audio_haptics]") {
    const auto samples_at = [](const BodyVoice& voice, int age_ms) {
        std::array<float, 192> samples{};
        static_cast<void>(build_body_pcm(one_voice(voice),
            voice.started_at + std::chrono::milliseconds{age_ms},
            samples));
        return samples;
    };
    const auto has_signal = [](const auto& samples) {
        return std::ranges::any_of(samples, [](float sample) { return std::abs(sample) > 0.01F; });
    };
    for (const float scale : {0.5F, 1.0F, 2.0F}) {
        CAPTURE(scale);
        BodyVoice voice = audio_voice(scale);
        voice.duration_ms = static_cast<std::uint32_t>(310.0F * scale);
        voice.wave.frequency = 250.0F;
        voice.wave.second_frequency = 500.0F;
        voice.wave.level = 0.5F;
        voice.wave.audio_envelope = {};
        voice.wave.audio_envelope.enabled = true;
        voice.wave.audio_envelope.primary[8] = 0.8F;
        voice.wave.audio_envelope.secondary[23] = 0.6F;
        REQUIRE(valid_audio_haptic_envelope(voice.wave.audio_envelope));

        // In a 310 ms source, these isolated knots peak at 80 ms and 230 ms.
        const auto first_peak = static_cast<int>(80.0F * scale);
        const auto second_peak = static_cast<int>(230.0F * scale);
        CHECK(has_signal(samples_at(voice, first_peak)));
        CHECK(has_signal(samples_at(voice, second_peak)));
        CHECK_FALSE(has_signal(samples_at(voice, static_cast<int>(120.0F * scale))));
        const auto expired = samples_at(voice, static_cast<int>(voice.duration_ms));
        CHECK(std::ranges::all_of(expired, [](float sample) { return sample == 0.0F; }));

        BodyVoice same_lifetime = voice;
        same_lifetime.duration_scale = 1.0F;
        INFO("delivered duration controls the audio envelope; metadata must not scale it again");
        CHECK(samples_at(voice, first_peak) == samples_at(same_lifetime, first_peak));
        CHECK(samples_at(voice, second_peak) == samples_at(same_lifetime, second_peak));
    }
}

TEST_CASE("effects.audio_haptics.chunk_continuity_duration_scale_expiry_and_identity",
    "[unit][effects][audio_haptics]") {
    for (const float duration_scale : {0.5F, 1.0F, 2.0F}) {
        BodyVoice voice = audio_voice(duration_scale);
        voice.duration_ms = 100U;
        const BodyProgram program = one_voice(voice);
        std::vector<float> whole(std::size_t{1'200U} * 2U);
        (void)build_body_pcm(program, MonoTime{}, whole);

        constexpr std::size_t split_frame = 431U;
        std::vector<float> second((std::size_t{1'200U} - split_frame) * 2U);
        const MonoTime second_start =
            MonoTime{} +
            std::chrono::duration_cast<MonoTime::duration>(
                std::chrono::duration<double>(static_cast<double>(split_frame) / 48'000.0));
        (void)build_body_pcm(program, second_start, second);
        INFO("overlapping windows preserve audio carrier phase at the same original age");
        CHECK(std::ranges::all_of(std::views::iota(std::size_t{0}, second.size()),
            [&](std::size_t index) {
                return std::abs(whole[(split_frame * std::size_t{2U}) + index] - second[index]) <
                       1.0e-5F;
            }));
    }

    BodyVoice voice = audio_voice();
    voice.duration_ms = 20U;
    std::array<float, std::size_t{20U} * 2U> expired{};
    (void)build_body_pcm(one_voice(voice), MonoTime{} + 20ms, expired);
    CHECK(std::ranges::all_of(expired, [](float sample) { return sample == 0.0F; }));

    voice.scale = 0.0F;
    std::array<float, std::size_t{4U} * 2U> muted{};
    (void)build_body_pcm(one_voice(voice), MonoTime{}, muted);
    CHECK(std::ranges::all_of(muted, [](float sample) { return sample == 0.0F; }));

    BodyVoice changed = voice;
    changed.scale = 1.0F;
    const BodyProgram original = one_voice(changed);
    changed.wave.audio_envelope.primary[10] += 0.1F;
    INFO("wave identity includes every audio envelope value");
    CHECK_FALSE(same_body_program_identity(original, one_voice(changed)));

    BodyVoice long_voice = audio_voice();
    long_voice.duration_ms = 1'000U;
    std::vector<float> bounded(static_cast<std::size_t>(body_pcm_max_frames + 4U) * 2U, 7.0F);
    const auto maximum = build_body_pcm(one_voice(long_voice), MonoTime{}, bounded);
    CHECK(maximum.frame_count == body_pcm_max_frames);
    CHECK(bounded[static_cast<std::size_t>(maximum.frame_count) * 2U] == 7.0F);
}
