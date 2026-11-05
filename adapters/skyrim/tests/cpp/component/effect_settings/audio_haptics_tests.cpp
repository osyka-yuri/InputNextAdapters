#include "support.hpp"
#include "test_support.hpp"

#include "effect_tuning.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/audio_haptics.hpp>
#include <inputnext_adapters/body_program.hpp>
#include <skyrim_adapter/audio_haptics.hpp>
#include <skyrim_adapter/body_waveform.hpp>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace skyrim_adapter::effect_settings_tests {
using inputnext::adapters::AudioHapticBank;
using inputnext::adapters::AudioHapticEntry;
using inputnext::adapters::AudioHapticGame;
using inputnext::adapters::AudioHapticProfile;
using skyrim_adapter::test::require_value;

namespace {
AudioHapticProfile audio_profile() {
    AudioHapticProfile profile{.frequency = 240.0F, .second_frequency = 80.0F};
    profile.envelope.enabled = true;
    for (std::size_t index = 1; index + 1 < profile.envelope.primary.size(); ++index) {
        profile.envelope.primary[index] = 0.5F;
        profile.envelope.secondary[index] = 0.25F;
    }
    return profile;
}

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

std::vector<std::byte>
bank_bytes(AudioHapticGame game, std::string_view section, const AudioHapticProfile& profile) {
    std::vector<std::byte> payload;
    append_u16(payload, static_cast<std::uint16_t>(section.size()));
    for (const char value : section) {
        payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    append_u32(payload, std::bit_cast<std::uint32_t>(profile.frequency));
    append_u32(payload, std::bit_cast<std::uint32_t>(profile.second_frequency));
    for (const float value : profile.envelope.primary) {
        append_u32(payload, std::bit_cast<std::uint32_t>(value));
    }
    for (const float value : profile.envelope.secondary) {
        append_u32(payload, std::bit_cast<std::uint32_t>(value));
    }

    std::vector<std::byte> bytes{std::byte{'I'}, std::byte{'N'}, std::byte{'H'}, std::byte{'B'}};
    append_u32(bytes, 1U);
    append_u32(bytes, static_cast<std::uint32_t>(game));
    append_u32(bytes, 1U);
    append_u32(bytes, static_cast<std::uint32_t>(payload.size()));
    append_u32(bytes, crc32(payload));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

class TemporaryAudioBank {
public:
    TemporaryAudioBank() {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("inputnext-skyrim-audio-haptics-" + std::to_string(unique));
        std::filesystem::create_directories(directory_);
    }

    TemporaryAudioBank(const TemporaryAudioBank&) = delete;
    TemporaryAudioBank& operator=(const TemporaryAudioBank&) = delete;
    TemporaryAudioBank(TemporaryAudioBank&&) = delete;
    TemporaryAudioBank& operator=(TemporaryAudioBank&&) = delete;

    ~TemporaryAudioBank() noexcept {
        try {
            std::error_code error;
            std::filesystem::remove_all(directory_, error);
        } catch (...) {
            static_cast<void>(std::fputs("failed to remove temporary audio bank\n", stderr));
        }
    }

    [[nodiscard]] std::filesystem::path config_path() const {
        return directory_ / L"InputNextSkyrim.ini";
    }

    void write(std::span<const std::byte> bytes) const {
        auto bank_path = config_path();
        bank_path.replace_extension(L".audiohaptics");
        std::ofstream stream(bank_path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            throw std::runtime_error("failed to write audio-haptics test bank");
        }
    }

    void write_corrupt() const {
        const std::array corrupt{std::byte{0}, std::byte{1}, std::byte{2}};
        write(corrupt);
    }

private:
    std::filesystem::path directory_;
};
} // namespace

TEST_CASE("skyrim.audio_haptics.binding_is_game_scoped_for_transient_and_supported_held_profiles",
    "[component][skyrim][effect_settings][audio_haptics]") {
    const auto settings = std::make_unique<EffectSettings>();
    const AudioHapticBank bank{.game = AudioHapticGame::skyrim_se,
        .entries = {{.section = "Body.Melee.Sword.Swing", .profile = audio_profile()},
            {.section = "Body.Bow.TensionDraw", .profile = audio_profile()},
            {.section = "Body.Bow.TensionAttached", .profile = audio_profile()},
            {.section = "Body.Bow.TensionDrawn", .profile = audio_profile()},
            {.section = "Body.Magic.Fire.Concentration", .profile = audio_profile()},
            {.section = "Body.Magic.Frost.Concentration", .profile = audio_profile()},
            {.section = "Body.Magic.Shock.Concentration", .profile = audio_profile()},
            {.section = "Body.Magic.Healing.Concentration", .profile = audio_profile()},
            {.section = "Body.Magic.Ward.Concentration", .profile = audio_profile()},
            {.section = "Body.Magic.Generic.Concentration", .profile = audio_profile()},
            {.section = "Body.NotAnEffect", .profile = audio_profile()}}};
    CHECK(bind_audio_haptics(bank, AudioHapticGame::skyrim_se, *settings) == 10U);
    CHECK(settings->body[static_cast<std::size_t>(EffectId::sword_swing)].audio_haptic.has_value());
    for (const auto id : {EffectId::bow_tension_draw,
             EffectId::bow_tension_attached,
             EffectId::bow_tension_drawn,
             EffectId::fire_held,
             EffectId::frost_held,
             EffectId::shock_held,
             EffectId::healing_held,
             EffectId::ward_held,
             EffectId::generic_held}) {
        CHECK(settings->body[static_cast<std::size_t>(id)].audio_haptic.has_value());
    }
    CHECK(bind_audio_haptics(bank, AudioHapticGame::skyrim_le, *settings) == 0);
    CHECK_FALSE(
        settings->body[static_cast<std::size_t>(EffectId::sword_swing)].audio_haptic.has_value());
    settings->audio_haptics_enabled = false;
    CHECK(bind_audio_haptics(bank, AudioHapticGame::skyrim_se, *settings) == 0);
}

TEST_CASE("skyrim.audio_haptics.held_profile_projects_source_means_to_a_stationary_legacy_recipe",
    "[component][skyrim][effect_settings][audio_haptics]") {
    const auto settings_storage = std::make_unique<EffectSettings>();
    auto& settings = *settings_storage;
    auto& adjustment = settings.body[static_cast<std::size_t>(EffectId::fire_held)];
    adjustment.audio_haptic = audio_profile();
    adjustment.frequency_hz = 300.0F;
    adjustment.duration_scale = 2.0F;

    BodyCue tuned{};
    tuned.kind = BodyCueKind::concentration;
    tuned.spell = SpellKind::fire;
    tuned.started_at = at(1000);
    tuned.duration_ms = 1000;
    tuned.scale = 1.0F;
    REQUIRE(detail::tune_body_cue(tuned, settings));
    REQUIRE(tuned.audio_haptic.has_value());
    CHECK(tuned.duration_ms == 1000);
    CHECK(tuned.duration_scale == 1.0F);

    BodyCue legacy = tuned;
    legacy.audio_haptic.reset();
    const auto tuned_plan_storage = std::make_unique<BodyPlan>();
    auto& tuned_plan = *tuned_plan_storage;
    tuned_plan.held[0] = tuned;
    const auto legacy_plan_storage = std::make_unique<BodyPlan>();
    auto& legacy_plan = *legacy_plan_storage;
    legacy_plan.held[0] = legacy;
    const auto tuned_program = make_body_program(tuned_plan);
    const auto legacy_program = make_body_program(legacy_plan);
    REQUIRE(tuned_program.voices[0].has_value());
    REQUIRE(legacy_program.voices[0].has_value());
    const auto& wave = require_value(tuned_program.voices[0]).wave;
    const auto& legacy_wave = require_value(legacy_program.voices[0]).wave;

    constexpr float primary_mean = 15.0F / 32.0F;
    constexpr float secondary_mean = 7.5F / 32.0F;
    constexpr float total_mean = primary_mean + secondary_mean;
    CHECK(wave.frequency == 300.0F);
    CHECK(std::abs(wave.second_frequency - 100.0F) < 0.001F);
    CHECK(std::abs(wave.second_mix - (secondary_mean / total_mean)) < 0.001F);
    CHECK(std::abs(wave.level - (legacy_wave.level * total_mean)) < 0.001F);
    CHECK_FALSE(wave.audio_envelope.enabled);
    CHECK(wave.noise == legacy_wave.noise);
    CHECK(wave.attack_ms == legacy_wave.attack_ms);
    CHECK(wave.release_ms == legacy_wave.release_ms);
    CHECK(wave.decay_per_second == legacy_wave.decay_per_second);
    CHECK(wave.modulation_hz == legacy_wave.modulation_hz);
    CHECK(wave.first_pulse_ms == legacy_wave.first_pulse_ms);
    CHECK(wave.pulse_gap_ms == legacy_wave.pulse_gap_ms);
    CHECK(wave.second_pulse_ms == legacy_wave.second_pulse_ms);

    const auto different_source_storage = std::make_unique<BodyPlan>(tuned_plan);
    auto& same_projection_different_source = *different_source_storage;
    auto& source_cue = require_value(same_projection_different_source.held[0]);
    auto& source_primary = require_value(source_cue.audio_haptic).envelope.primary;
    source_primary[1] = 0.25F;
    source_primary[2] = 0.75F;
    CHECK_FALSE(same_body_plan_identity(tuned_plan, same_projection_different_source));

    AudioHapticProfile subnormal_profile{
        .frequency = 200.0F,
        .second_frequency = 0.0F,
    };
    subnormal_profile.envelope.enabled = true;
    subnormal_profile.envelope.primary[1] = std::numeric_limits<float>::denorm_min();
    REQUIRE(inputnext::adapters::valid_audio_haptic_profile(subnormal_profile));
    BodyCue degenerate = legacy;
    degenerate.audio_haptic = subnormal_profile;
    const auto degenerate_plan_storage = std::make_unique<BodyPlan>();
    auto& degenerate_plan = *degenerate_plan_storage;
    degenerate_plan.held[0] = degenerate;
    const auto degenerate_program = make_body_program(degenerate_plan);
    REQUIRE(degenerate_program.voices[0].has_value());
    CHECK(require_value(degenerate_program.voices[0]).wave == legacy_wave);

    AudioHapticProfile invalid_profile = audio_profile();
    invalid_profile.envelope.primary[1] = std::numeric_limits<float>::quiet_NaN();
    BodyCue invalid = legacy;
    invalid.audio_haptic = invalid_profile;
    const auto invalid_plan_storage = std::make_unique<BodyPlan>();
    auto& invalid_plan = *invalid_plan_storage;
    invalid_plan.held[0] = invalid;
    const auto invalid_program = make_body_program(invalid_plan);
    REQUIRE(invalid_program.voices[0].has_value());
    CHECK(require_value(invalid_program.voices[0]).wave == legacy_wave);

    const auto disabled_storage = std::make_unique<EffectSettings>(settings);
    auto& disabled = *disabled_storage;
    disabled.audio_haptics_enabled = false;
    BodyCue disabled_cue{};
    disabled_cue.kind = BodyCueKind::concentration;
    disabled_cue.spell = SpellKind::fire;
    disabled_cue.duration_ms = 1000;
    disabled_cue.scale = 1.0F;
    REQUIRE(detail::tune_body_cue(disabled_cue, disabled));
    CHECK_FALSE(disabled_cue.audio_haptic.has_value());
}

TEST_CASE("skyrim.audio_haptics.held_projection_is_duration_independent_and_keeps_original_phase",
    "[component][skyrim][effect_settings][audio_haptics]") {
    BodyCue held{};
    held.kind = BodyCueKind::concentration;
    held.spell = SpellKind::frost;
    held.started_at = at(1000);
    held.duration_ms = 1000;
    held.scale = 1.0F;
    held.stable_id = 42;
    held.audio_haptic = audio_profile();

    BodyPlan long_plan{};
    long_plan.held[0] = held;
    BodyPlan short_plan = long_plan;
    require_value(short_plan.held[0]).duration_ms = 100;
    const auto long_program = make_body_program(long_plan);
    const auto short_program = make_body_program(short_plan);
    REQUIRE(long_program.voices[0].has_value());
    REQUIRE(short_program.voices[0].has_value());
    CHECK(
        require_value(long_program.voices[0]).wave == require_value(short_program.voices[0]).wave);
    CHECK_FALSE(require_value(long_program.voices[0]).wave.audio_envelope.enabled);

    std::vector<float> early_long(960);
    std::vector<float> early_short(960);
    static_cast<void>(inputnext::adapters::build_body_pcm(long_program, at(1020), early_long));
    static_cast<void>(inputnext::adapters::build_body_pcm(short_program, at(1020), early_short));
    CHECK(early_long == early_short);

    std::vector<float> expired_short(960);
    const auto expired_window =
        inputnext::adapters::build_body_pcm(short_program, at(1100), expired_short);
    CHECK_FALSE(expired_window.contains_held);
    CHECK(std::ranges::all_of(expired_short, [](float sample) { return sample == 0.0F; }));

    std::vector<float> continuous(9600);
    std::vector<float> continued_chunk(960);
    const auto full_window =
        inputnext::adapters::build_body_pcm(long_program, at(1100), continuous);
    const auto chunk_window =
        inputnext::adapters::build_body_pcm(long_program, at(1150), continued_chunk);
    REQUIRE(full_window.frame_count == 4800U);
    REQUIRE(chunk_window.frame_count == 480U);
    CHECK(std::equal(continued_chunk.begin(), continued_chunk.end(), continuous.begin() + 4800));

    std::vector<float> late_long(960);
    const auto late_window = inputnext::adapters::build_body_pcm(long_program, at(1900), late_long);
    CHECK(late_window.contains_held);
    CHECK(peak(std::vector<float>(late_long.begin(), late_long.end())) > 0.01F);
}

TEST_CASE("skyrim.audio_haptics.policy_carries_source_shape_without_changing_classic_or_deadline",
    "[component][skyrim][effect_settings][audio_haptics]") {
    const auto config = std::make_unique<PolicyConfig>();
    auto& adjustment = config->effects.body[static_cast<std::size_t>(EffectId::sword_swing)];
    adjustment.audio_haptic = audio_profile();
    adjustment.frequency_hz = 300.0F;
    adjustment.duration_scale = 1.5F;
    const auto audio_owner = std::make_unique<Policy>(*config);
    adjustment.audio_haptic.reset();
    const auto legacy_owner = std::make_unique<Policy>(*config);
    auto swing = action(EventKind::weapon_swing, Hand::right, at(1000), 10);
    swing.source_known = true;
    swing.source_kind = EquipmentKind::weapon;
    swing.weapon = WeaponClass::sword;
    const std::array actions{swing};
    const auto audio_frame = audio_owner->step(observation(at(1000)), actions, at(1000));
    const auto legacy_frame = legacy_owner->step(observation(at(1000)), actions, at(1000));
    REQUIRE(audio_frame.body.hand_transient[1].has_value());
    const auto& cue = require_value(audio_frame.body.hand_transient[1]);
    CHECK(cue.audio_haptic.has_value());
    CHECK(cue.started_at == at(1000));
    CHECK(cue.duration_ms == 90);
    const auto program = make_body_program(audio_frame.body);
    REQUIRE(program.voices[3].has_value());
    const auto& voice = require_value(program.voices[3]);
    CHECK(voice.wave.audio_envelope.enabled);
    CHECK(voice.wave.frequency == 300.0F);
    CHECK(std::abs(voice.wave.second_frequency - 100.0F) < 0.001F);
    REQUIRE(audio_frame.rumble.has_value());
    REQUIRE(legacy_frame.rumble.has_value());
    CHECK(require_value(audio_frame.rumble).payload.low_frequency ==
          require_value(legacy_frame.rumble).payload.low_frequency);
    CHECK(require_value(audio_frame.rumble).payload.high_frequency ==
          require_value(legacy_frame.rumble).payload.high_frequency);
    CHECK(require_value(audio_frame.rumble).ttl_ms == require_value(legacy_frame.rumble).ttl_ms);
    auto other = audio_frame.body;
    auto changed_cue = cue;
    changed_cue.audio_haptic.reset();
    other.hand_transient[1] = changed_cue;
    CHECK_FALSE(same_body_plan_identity(audio_frame.body, other));
}

TEST_CASE("skyrim.audio_haptics.modifiers_scale_both_cached_carriers",
    "[component][skyrim][effect_settings][audio_haptics]") {
    constexpr auto power = static_cast<std::uint32_t>(HitFlag::power_attack);
    constexpr auto blocked = static_cast<std::uint32_t>(HitFlag::blocked);
    constexpr auto bash = static_cast<std::uint32_t>(HitFlag::bash_attack);

    struct Variant {
        std::uint32_t flags;
        bool shield;
        float factor;
    };

    constexpr std::array variants{Variant{.flags = 0, .shield = false, .factor = 1.0F},
        Variant{.flags = power, .shield = false, .factor = 0.92F},
        Variant{.flags = blocked, .shield = false, .factor = 0.90F},
        Variant{.flags = blocked, .shield = true, .factor = 0.78F},
        Variant{.flags = bash, .shield = false, .factor = 1.20F}};
    for (const auto variant : variants) {
        BodyCue cue{};
        cue.kind = BodyCueKind::melee_hit;
        cue.weapon = WeaponClass::sword;
        cue.duration_ms = 80;
        cue.started_at = at(1000);
        cue.scale = 1.0F;
        cue.flags = variant.flags;
        cue.shield_block = variant.shield;
        cue.audio_haptic = audio_profile();
        const auto plan_storage = std::make_unique<BodyPlan>();
        auto& plan = *plan_storage;
        plan.body_transient = cue;
        const auto program = make_body_program(plan);
        REQUIRE(program.voices[4].has_value());
        const auto& wave = require_value(program.voices[4]).wave;
        CHECK(std::abs(wave.frequency - (240.0F * variant.factor)) < 0.001F);
        CHECK(std::abs(wave.second_frequency - (80.0F * variant.factor)) < 0.001F);
    }
}

TEST_CASE("skyrim.audio_haptics.carrier_bounds_apply_after_overrides_and_hit_modifiers",
    "[component][skyrim][effect_settings][audio_haptics]") {
    struct CarrierCase {
        float primary{};
        float secondary{};
        float override_hz{};
        float expected_primary{};
        float expected_secondary{};
        std::uint32_t flags{};
        bool shield{};
    };

    const float above_boundary = std::nextafter(375.0F, 1000.0F);
    const std::array cases{CarrierCase{.primary = 54.0F,
                               .secondary = 144.0F,
                               .override_hz = 1000.0F,
                               .expected_primary = 1000.0F,
                               .expected_secondary = 1000.0F},
        CarrierCase{.primary = 144.0F,
            .secondary = 54.0F,
            .override_hz = 20.0F,
            .expected_primary = 20.0F,
            .expected_secondary = 20.0F},
        CarrierCase{.primary = 54.0F,
            .secondary = 144.0F,
            .override_hz = 375.0F,
            .expected_primary = 375.0F,
            .expected_secondary = 1000.0F},
        CarrierCase{.primary = 54.0F,
            .secondary = 144.0F,
            .override_hz = above_boundary,
            .expected_primary = above_boundary,
            .expected_secondary = 1000.0F},
        CarrierCase{.primary = 240.0F,
            .secondary = 80.0F,
            .override_hz = 300.0F,
            .expected_primary = 300.0F,
            .expected_secondary = 100.0F},
        CarrierCase{.primary = 240.0F,
            .secondary = 0.0F,
            .override_hz = 1000.0F,
            .expected_primary = 1000.0F,
            .expected_secondary = 0.0F},
        CarrierCase{.primary = 1000.0F,
            .secondary = 1000.0F,
            .override_hz = 0.0F,
            .expected_primary = 1000.0F,
            .expected_secondary = 1000.0F,
            .flags = static_cast<std::uint32_t>(HitFlag::bash_attack)},
        CarrierCase{.primary = 20.0F,
            .secondary = 20.0F,
            .override_hz = 0.0F,
            .expected_primary = 20.0F,
            .expected_secondary = 20.0F,
            .flags = static_cast<std::uint32_t>(HitFlag::blocked),
            .shield = true},
        CarrierCase{.primary = 54.0F,
            .secondary = 144.0F,
            .override_hz = 0.0F,
            .expected_primary = 54.0F,
            .expected_secondary = 144.0F}};
    for (const auto& value : cases) {
        CAPTURE(value.primary, value.secondary, value.override_hz, value.flags);
        BodyCue cue{};
        cue.kind = BodyCueKind::melee_hit;
        cue.weapon = WeaponClass::two_handed;
        cue.duration_ms = 110;
        cue.started_at = at(1000);
        cue.scale = 1.0F;
        cue.frequency_hz = value.override_hz;
        cue.flags = value.flags;
        cue.shield_block = value.shield;
        cue.audio_haptic = audio_profile();
        cue.audio_haptic->frequency = value.primary;
        cue.audio_haptic->second_frequency = value.secondary;
        if (value.secondary == 0.0F) {
            cue.audio_haptic->envelope.secondary.fill(0.0F);
        }
        REQUIRE(inputnext::adapters::valid_audio_haptic_profile(*cue.audio_haptic));
        const auto plan_storage = std::make_unique<BodyPlan>();
        auto& plan = *plan_storage;
        plan.body_transient = cue;
        auto program = make_body_program(plan);
        REQUIRE(program.voices[4].has_value());
        auto& wave = require_value(program.voices[4]).wave;
        CHECK(std::abs(wave.frequency - value.expected_primary) < 0.001F);
        CHECK(std::abs(wave.second_frequency - value.expected_secondary) < 0.001F);
        CHECK(wave.frequency >= 20.0F);
        CHECK(wave.frequency <= 1000.0F);
        CHECK((wave.second_frequency == 0.0F ||
               (wave.second_frequency >= 20.0F && wave.second_frequency <= 1000.0F)));
        CHECK(wave.audio_envelope == cue.audio_haptic->envelope);
        std::vector<float> audio_samples(960);
        std::vector<float> legacy_samples(960);
        static_cast<void>(inputnext::adapters::build_body_pcm(program, at(1010), audio_samples));
        wave.audio_envelope = {};
        static_cast<void>(inputnext::adapters::build_body_pcm(program, at(1010), legacy_samples));
        CHECK(audio_samples != legacy_samples);
    }
}

TEST_CASE("skyrim.audio_haptics.builtins_are_embedded_and_load_without_an_external_bank",
    "[component][skyrim][effect_settings][audio_haptics]") {
    static constexpr std::array expected_profiles{
        std::pair{AudioHapticGame::skyrim_se, std::size_t{49}},
        std::pair{AudioHapticGame::skyrim_le, std::size_t{18}},
    };
    for (const auto [game, expected_count] : expected_profiles) {
        const auto builtin = inputnext::adapters::builtin_audio_haptic_bank(game);
        REQUIRE(builtin.status == inputnext::adapters::AudioHapticBankStatus::loaded);
        CHECK(builtin.bank.game == game);
        CHECK(builtin.bank.entries.size() == expected_count);
        CHECK(std::ranges::is_sorted(builtin.bank.entries,
            {},
            [](const auto& entry) -> std::string_view { return entry.section; }));
    }

    const auto se_bank = inputnext::adapters::builtin_audio_haptic_bank(AudioHapticGame::skyrim_se);
    const auto unarmed = std::ranges::find(se_bank.bank.entries,
        std::string_view{"Body.Melee.Unarmed.Hit"},
        &AudioHapticEntry::section);
    REQUIRE(unarmed != se_bank.bank.entries.end());
    static constexpr std::array expected_primary{0.0F,
        0.20876037F,
        0.21353184F,
        0.33012015F,
        0.3320893F,
        0.42974117F,
        0.47182545F,
        0.52715653F,
        0.51866674F,
        0.5416433F,
        0.54917765F,
        0.59878916F,
        0.6816604F,
        0.71929365F,
        0.75782174F,
        0.7232199F,
        0.77348393F,
        0.7897994F,
        0.809153F,
        0.88000005F,
        0.8759427F,
        0.83966285F,
        0.8340626F,
        0.768461F,
        0.73226297F,
        0.7053265F,
        0.68289465F,
        0.6483515F,
        0.65478754F,
        0.6413344F,
        0.6399702F,
        0.0F};
    CHECK(unarmed->profile.frequency == 78.0F);
    CHECK(unarmed->profile.second_frequency == 0.0F);
    CHECK(unarmed->profile.envelope.primary == expected_primary);
    CHECK(std::ranges::all_of(unarmed->profile.envelope.secondary,
        [](float value) { return value == 0.0F; }));

    const TemporaryAudioBank files;
    const auto settings_storage = std::make_unique<EffectSettings>();
    auto& settings = *settings_storage;
    const auto loaded =
        load_audio_haptics(files.config_path(), AudioHapticGame::skyrim_se, settings);
    CHECK(loaded.status == inputnext::adapters::AudioHapticBankStatus::loaded);
    CHECK(loaded.loaded_profiles == 49U);
    CHECK(settings.body[static_cast<std::size_t>(EffectId::unarmed_hit)].audio_haptic ==
          unarmed->profile);

    const auto le_bank = inputnext::adapters::builtin_audio_haptic_bank(AudioHapticGame::skyrim_le);
    static constexpr std::array<std::string_view, 18> le_sections{
        "Body.Bow.Draw",
        "Body.Bow.Release",
        "Body.CriticalHealth",
        "Body.IncomingHit",
        "Body.Melee.Axe.Hit",
        "Body.Melee.Axe.Swing",
        "Body.Melee.Dagger.Hit",
        "Body.Melee.Dagger.Swing",
        "Body.Melee.Mace.Hit",
        "Body.Melee.Sword.Hit",
        "Body.Melee.Sword.Swing",
        "Body.Melee.TwoHanded.Hit",
        "Body.Melee.TwoHanded.Swing",
        "Body.Melee.Unarmed.Hit",
        "Body.Melee.Unarmed.Swing",
        "Body.Transformation.VampireLord",
        "Body.Transformation.Werewolf",
        "Body.Voice",
    };
    REQUIRE(le_bank.bank.entries.size() == le_sections.size());
    for (std::size_t index = 0; index < le_sections.size(); ++index) {
        CHECK(le_bank.bank.entries[index].section == le_sections[index]);
    }

    const auto le_settings_storage = std::make_unique<EffectSettings>();
    auto& le_settings = *le_settings_storage;
    const auto le_loaded =
        load_audio_haptics(files.config_path(), AudioHapticGame::skyrim_le, le_settings);
    CHECK(le_loaded.status == inputnext::adapters::AudioHapticBankStatus::loaded);
    CHECK(le_loaded.loaded_profiles == le_sections.size());
    for (const auto& descriptor : effect_descriptors) {
        const auto entry =
            std::ranges::find(le_bank.bank.entries, descriptor.section, &AudioHapticEntry::section);
        const auto& adjustment = le_settings.body[static_cast<std::size_t>(descriptor.id)];
        if (entry == le_bank.bank.entries.end()) {
            CHECK_FALSE(adjustment.audio_haptic.has_value());
        } else {
            CHECK(adjustment.audio_haptic == entry->profile);
        }
    }
}

TEST_CASE("skyrim.audio_haptics.external_bank_overlays_one_builtin_and_bad_banks_keep_defaults",
    "[component][skyrim][effect_settings][audio_haptics]") {
    const TemporaryAudioBank files;
    const auto settings_storage = std::make_unique<EffectSettings>();
    auto& settings = *settings_storage;
    AudioHapticProfile replacement = audio_profile();
    replacement.frequency = 333.0F;
    replacement.second_frequency = 99.0F;

    files.write(bank_bytes(AudioHapticGame::skyrim_se, "Body.Melee.Unarmed.Hit", replacement));
    const auto overlaid =
        load_audio_haptics(files.config_path(), AudioHapticGame::skyrim_se, settings);
    CHECK(overlaid.status == inputnext::adapters::AudioHapticBankStatus::loaded);
    CHECK(overlaid.loaded_profiles == 49U);
    CHECK(
        settings.body[static_cast<std::size_t>(EffectId::unarmed_hit)].audio_haptic == replacement);
    REQUIRE(settings.body[static_cast<std::size_t>(EffectId::sword_hit)].audio_haptic.has_value());
    CHECK(require_value(settings.body[static_cast<std::size_t>(EffectId::sword_hit)].audio_haptic)
              .frequency == 216.0F);

    files.write_corrupt();
    const auto corrupt =
        load_audio_haptics(files.config_path(), AudioHapticGame::skyrim_se, settings);
    CHECK(corrupt.status == inputnext::adapters::AudioHapticBankStatus::invalid);
    CHECK(corrupt.loaded_profiles == 49U);
    CHECK(require_value(settings.body[static_cast<std::size_t>(EffectId::unarmed_hit)].audio_haptic)
              .frequency == 78.0F);

    files.write(bank_bytes(AudioHapticGame::fallout4, "Body.Melee.Unarmed.Hit", replacement));
    const auto wrong_game =
        load_audio_haptics(files.config_path(), AudioHapticGame::skyrim_se, settings);
    CHECK(wrong_game.status == inputnext::adapters::AudioHapticBankStatus::invalid);
    CHECK(wrong_game.loaded_profiles == 49U);
    CHECK(require_value(settings.body[static_cast<std::size_t>(EffectId::unarmed_hit)].audio_haptic)
              .frequency == 78.0F);

    settings.audio_haptics_enabled = false;
    const auto disabled =
        load_audio_haptics(files.config_path(), AudioHapticGame::skyrim_se, settings);
    CHECK(disabled.loaded_profiles == 0U);
    CHECK_FALSE(
        settings.body[static_cast<std::size_t>(EffectId::unarmed_hit)].audio_haptic.has_value());
}

TEST_CASE("skyrim.audio_haptics.io_error_keeps_compiled_defaults",
    "[component][skyrim][effect_settings][audio_haptics]") {
    for (const auto game : {AudioHapticGame::skyrim_se, AudioHapticGame::skyrim_le}) {
        const TemporaryAudioBank files;
        auto bank_path = files.config_path();
        bank_path.replace_extension(L".audiohaptics");
        REQUIRE(std::filesystem::create_directory(bank_path));
        const auto settings_storage = std::make_unique<EffectSettings>();
        auto& settings = *settings_storage;
        const auto loaded = load_audio_haptics(files.config_path(), game, settings);
        CHECK(loaded.status == inputnext::adapters::AudioHapticBankStatus::io_error);
        const auto builtin = inputnext::adapters::builtin_audio_haptic_bank(game);
        CHECK(loaded.loaded_profiles == builtin.bank.entries.size());
        for (const auto& entry : builtin.bank.entries) {
            const auto descriptor =
                std::ranges::find(effect_descriptors, entry.section, &EffectDescriptor::section);
            REQUIRE(descriptor != effect_descriptors.end());
            CHECK(settings.body[static_cast<std::size_t>(descriptor->id)].audio_haptic ==
                  entry.profile);
        }
    }
}
} // namespace skyrim_adapter::effect_settings_tests
