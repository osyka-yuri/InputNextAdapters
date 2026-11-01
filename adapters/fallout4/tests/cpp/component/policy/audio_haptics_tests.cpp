#include "support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fallout4_adapter/audio_haptics.hpp>
#include <fallout4_adapter/effect_settings.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>
#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/audio_haptics.hpp>

#include "effect_recipes.hpp"

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
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fallout4_adapter::policy_tests {
namespace {
inputnext::adapters::AudioHapticProfile audio_profile() {
    inputnext::adapters::AudioHapticProfile profile{.frequency = 240.0F, .second_frequency = 80.0F};
    profile.envelope.enabled = true;
    for (std::size_t index = 1; index + 1 < profile.envelope.primary.size(); ++index) {
        profile.envelope.primary[index] = 0.5F;
        profile.envelope.secondary[index] = 0.25F;
    }
    return profile;
}

void append_u16(std::vector<std::byte>& bytes, std::uint16_t value) {
    const std::uint32_t widened = value;
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

std::vector<std::byte> bank_bytes(inputnext::adapters::AudioHapticGame game,
    std::string_view section,
    const inputnext::adapters::AudioHapticProfile& profile) {
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
                     ("inputnext-fallout4-audio-haptics-" + std::to_string(unique));
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
        return directory_ / L"InputNextFallout4.ini";
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

TEST_CASE("fallout4.audio_haptics.binding_rejects_wrong_game_and_clears_disabled_profiles",
    "[component][fallout4][policy][audio_haptics]") {
    const auto settings = std::make_unique<EffectSettings>();
    inputnext::adapters::AudioHapticBank bank{.game =
                                                  inputnext::adapters::AudioHapticGame::fallout4,
        .entries = {{.section = "Body.RifleShot", .profile = audio_profile()},
            {.section = "Body.Unknown", .profile = audio_profile()}}};
    CHECK(bind_audio_haptics(bank, *settings) == 1);
    CHECK(
        settings->body[static_cast<std::size_t>(BodyEffect::rifle_shot)].audio_haptic.has_value());
    bank.game = inputnext::adapters::AudioHapticGame::skyrim_se;
    CHECK(bind_audio_haptics(bank, *settings) == 0);
    CHECK_FALSE(
        settings->body[static_cast<std::size_t>(BodyEffect::rifle_shot)].audio_haptic.has_value());
    settings->audio_haptics_enabled = false;
    bank.game = inputnext::adapters::AudioHapticGame::fallout4;
    CHECK(bind_audio_haptics(bank, *settings) == 0);
}

TEST_CASE("fallout4.audio_haptics.shot_uses_source_shape_and_preserves_classic_trigger_and_expiry",
    "[component][fallout4][policy][audio_haptics]") {
    const auto config = std::make_unique<PolicyConfig>();
    auto& adjustment = config->effects.body[static_cast<std::size_t>(BodyEffect::rifle_shot)];
    adjustment.audio_haptic = audio_profile();
    adjustment.frequency_hz = 300.0F;
    const auto audio = std::make_unique<Policy>(*config);
    adjustment.audio_haptic.reset();
    const auto legacy = std::make_unique<Policy>(*config);
    auto sample = gameplay_at(1000);
    sample.weapon.family = WeaponFamily::rifle;
    CHECK(voice_count(audio->update(sample, {}, at(1000))) == 0);
    CHECK(voice_count(legacy->update(sample, {}, at(1000))) == 0);
    const std::array actual_shot{event(EventKind::shot, at(1000), 1)};
    const auto audio_frame = audio->update(sample, actual_shot, at(1000));
    const auto legacy_frame = legacy->update(sample, actual_shot, at(1000));
    const auto* voice = find_voice(audio_frame, detail::Cue::rifle_shot);
    REQUIRE(voice != nullptr);
    CHECK(voice->wave.audio_envelope.enabled);
    CHECK(voice->wave.frequency == 300.0F);
    CHECK(std::abs(voice->wave.second_frequency - 100.0F) < 0.001F);
    CHECK(voice->started_at == at(1000));
    CHECK(voice->duration_ms == detail::recipe_for(detail::Cue::rifle_shot).duration_ms);
    sample.sampled_at = at(1020);
    const auto audio_tail = audio->update(sample, {}, at(1020));
    const auto legacy_tail = legacy->update(sample, {}, at(1020));
    REQUIRE(audio_tail.rumble.has_value());
    REQUIRE(legacy_tail.rumble.has_value());
    CHECK(checked_optional(legacy_tail.rumble).payload.low_frequency > 0.0F);
    CHECK(checked_optional(audio_tail.rumble).payload.low_frequency ==
          checked_optional(legacy_tail.rumble).payload.low_frequency);
    CHECK(checked_optional(audio_tail.rumble).payload.high_frequency ==
          checked_optional(legacy_tail.rumble).payload.high_frequency);
    REQUIRE(audio_frame.right.has_value());
    REQUIRE(legacy_frame.right.has_value());
    CHECK(checked_optional(audio_frame.right).payload.frequency ==
          checked_optional(legacy_frame.right).payload.frequency);
    sample.sampled_at = at(1100);
    const auto expired = audio->update(sample, std::span<const PolicyEvent>{}, at(1100));
    CHECK(cue_voice_count(expired, detail::Cue::rifle_shot) == 0);
}

TEST_CASE("fallout4.audio_haptics.carrier_bounds_apply_after_frequency_overrides",
    "[component][fallout4][policy][audio_haptics]") {
    struct CarrierCase {
        float primary;
        float secondary;
        float override_hz;
        float expected_secondary;
    };

    const std::array cases{
        CarrierCase{.primary = 66.0F,
            .secondary = 188.0F,
            .override_hz = 1000.0F,
            .expected_secondary = 1000.0F},
        CarrierCase{.primary = 66.0F,
            .secondary = 188.0F,
            .override_hz = 20.0F,
            .expected_secondary = 188.0F * (20.0F / 66.0F)},
        CarrierCase{.primary = 66.0F,
            .secondary = 188.0F,
            .override_hz = 100.0F,
            .expected_secondary = 188.0F * (100.0F / 66.0F)},
        CarrierCase{.primary = 66.0F,
            .secondary = 20.0F,
            .override_hz = 20.0F,
            .expected_secondary = 20.0F},
        CarrierCase{.primary = 66.0F,
            .secondary = 0.0F,
            .override_hz = 1000.0F,
            .expected_secondary = 0.0F},
    };
    for (const auto& carrier : cases) {
        const auto config = std::make_unique<PolicyConfig>();
        auto& adjustment = config->effects.body[static_cast<std::size_t>(BodyEffect::critical_hit)];
        auto profile = audio_profile();
        profile.frequency = carrier.primary;
        profile.second_frequency = carrier.secondary;
        if (carrier.secondary == 0.0F) {
            profile.envelope.secondary.fill(0.0F);
        }
        REQUIRE(inputnext::adapters::valid_audio_haptic_profile(profile));
        adjustment.audio_haptic = profile;
        adjustment.frequency_hz = carrier.override_hz;
        const auto policy = std::make_unique<Policy>(*config);
        const auto sample = gameplay_at(1000);
        static_cast<void>(policy->update(sample, {}, at(1000)));
        auto critical = event(EventKind::hit_dealt, at(1000), 1);
        critical.critical = true;
        const std::array events{critical};
        const auto frame = policy->update(sample, events, at(1000));
        const auto* voice = find_voice(frame, detail::Cue::critical_hit);
        REQUIRE(voice != nullptr);
        CHECK(voice->wave.frequency == carrier.override_hz);
        CHECK(std::abs(voice->wave.second_frequency - carrier.expected_secondary) < 0.001F);
        CHECK(voice->wave.audio_envelope == profile.envelope);
        CHECK(voice->wave.frequency >= 20.0F);
        CHECK(voice->wave.frequency <= 1000.0F);
        if (carrier.secondary > 0.0F) {
            CHECK(voice->wave.second_frequency >= 20.0F);
            CHECK(voice->wave.second_frequency <= 1000.0F);
        }
    }
}

TEST_CASE("fallout4.audio_haptics.builtins_are_embedded_and_load_without_an_external_bank",
    "[component][fallout4][policy][audio_haptics]") {
    const auto builtin = inputnext::adapters::builtin_audio_haptic_bank(
        inputnext::adapters::AudioHapticGame::fallout4);
    REQUIRE(builtin.status == inputnext::adapters::AudioHapticBankStatus::loaded);
    CHECK(builtin.bank.game == inputnext::adapters::AudioHapticGame::fallout4);
    REQUIRE(builtin.bank.entries.size() == 30U);
    CHECK(std::ranges::is_sorted(builtin.bank.entries,
        {},
        [](const auto& entry) -> std::string_view { return entry.section; }));

    const auto pistol = std::ranges::find(builtin.bank.entries,
        std::string_view{"Body.PistolShot"},
        &inputnext::adapters::AudioHapticEntry::section);
    REQUIRE(pistol != builtin.bank.entries.end());
    constexpr std::array expected_primary{0.0F,
        0.39901525F,
        0.39901525F,
        0.42134643F,
        0.4364215F,
        0.4806582F,
        0.5128742F,
        0.5089177F,
        0.5191629F,
        0.49321756F,
        0.4927995F,
        0.55098695F,
        0.5884977F,
        0.6484961F,
        0.7193811F,
        0.67697805F,
        0.5802393F,
        0.5088887F,
        0.39934292F,
        0.32512137F,
        // Exact baked fixture sample; retain its bits rather than round to inverse pi.
        std::bit_cast<float>(std::uint32_t{0x3EA2EAF6U}),
        0.30218482F,
        0.2695889F,
        0.33187217F,
        0.40229392F,
        0.42165038F,
        0.39778453F,
        0.4670238F,
        0.59816957F,
        0.7653191F,
        0.8570342F,
        0.0F};
    constexpr std::array expected_secondary{0.0F,
        0.12713692F,
        0.12713692F,
        0.12616058F,
        0.120501116F,
        0.10901442F,
        0.10561645F,
        0.10191309F,
        0.09214902F,
        0.09672737F,
        0.098477684F,
        0.095747046F,
        0.09391494F,
        0.080353715F,
        0.06303781F,
        0.04931145F,
        0.040683765F,
        0.035545025F,
        0.037333693F,
        0.03547915F,
        0.032391522F,
        0.029235182F,
        0.016823778F,
        0.01719466F,
        0.018601451F,
        0.025289884F,
        0.028213257F,
        0.02872269F,
        0.028796278F,
        0.024163077F,
        0.022965753F,
        0.0F};
    CHECK(pistol->profile.frequency == 112.0F);
    CHECK(pistol->profile.second_frequency == 76.0F);
    CHECK(pistol->profile.envelope.primary == expected_primary);
    CHECK(pistol->profile.envelope.secondary == expected_secondary);

    const TemporaryAudioBank files;
    EffectSettings settings;
    const auto loaded = load_audio_haptics(files.config_path(), settings);
    CHECK(loaded.status == inputnext::adapters::AudioHapticBankStatus::loaded);
    CHECK(loaded.loaded_profiles == 30U);
    CHECK(settings.body[static_cast<std::size_t>(BodyEffect::pistol_shot)].audio_haptic ==
          pistol->profile);
}

TEST_CASE("fallout4.audio_haptics.external_bank_overlays_one_builtin_and_bad_banks_keep_defaults",
    "[component][fallout4][policy][audio_haptics]") {
    const TemporaryAudioBank files;
    EffectSettings settings;
    inputnext::adapters::AudioHapticProfile replacement = audio_profile();
    replacement.frequency = 201.0F;
    replacement.second_frequency = 87.0F;

    files.write(
        bank_bytes(inputnext::adapters::AudioHapticGame::fallout4, "Body.PistolShot", replacement));
    const auto overlaid = load_audio_haptics(files.config_path(), settings);
    CHECK(overlaid.status == inputnext::adapters::AudioHapticBankStatus::loaded);
    CHECK(overlaid.loaded_profiles == 30U);
    CHECK(settings.body[static_cast<std::size_t>(BodyEffect::pistol_shot)].audio_haptic ==
          replacement);
    REQUIRE(
        settings.body[static_cast<std::size_t>(BodyEffect::rifle_shot)].audio_haptic.has_value());
    CHECK(checked_optional(
              settings.body[static_cast<std::size_t>(BodyEffect::rifle_shot)].audio_haptic)
              .frequency == 86.0F);

    files.write_corrupt();
    const auto corrupt = load_audio_haptics(files.config_path(), settings);
    CHECK(corrupt.status == inputnext::adapters::AudioHapticBankStatus::invalid);
    CHECK(corrupt.loaded_profiles == 30U);
    CHECK(checked_optional(
              settings.body[static_cast<std::size_t>(BodyEffect::pistol_shot)].audio_haptic)
              .frequency == 112.0F);

    files.write(bank_bytes(inputnext::adapters::AudioHapticGame::skyrim_le,
        "Body.PistolShot",
        replacement));
    const auto wrong_game = load_audio_haptics(files.config_path(), settings);
    CHECK(wrong_game.status == inputnext::adapters::AudioHapticBankStatus::invalid);
    CHECK(wrong_game.loaded_profiles == 30U);
    CHECK(checked_optional(
              settings.body[static_cast<std::size_t>(BodyEffect::pistol_shot)].audio_haptic)
              .frequency == 112.0F);

    settings.audio_haptics_enabled = false;
    const auto disabled = load_audio_haptics(files.config_path(), settings);
    CHECK(disabled.loaded_profiles == 0U);
    CHECK_FALSE(
        settings.body[static_cast<std::size_t>(BodyEffect::pistol_shot)].audio_haptic.has_value());
}

TEST_CASE("fallout4.audio_haptics.io_error_keeps_compiled_defaults",
    "[component][fallout4][policy][audio_haptics]") {
    const TemporaryAudioBank files;
    auto bank_path = files.config_path();
    bank_path.replace_extension(L".audiohaptics");
    REQUIRE(std::filesystem::create_directory(bank_path));
    EffectSettings settings;
    const auto loaded = load_audio_haptics(files.config_path(), settings);
    CHECK(loaded.status == inputnext::adapters::AudioHapticBankStatus::io_error);
    const auto builtin = inputnext::adapters::builtin_audio_haptic_bank(
        inputnext::adapters::AudioHapticGame::fallout4);
    CHECK(loaded.loaded_profiles == builtin.bank.entries.size());
    for (const auto& entry : builtin.bank.entries) {
        const auto descriptor = std::ranges::find(body_effect_descriptors,
            entry.section,
            &BodyEffectDescriptor::section);
        REQUIRE(descriptor != body_effect_descriptors.end());
        CHECK(settings.body[static_cast<std::size_t>(descriptor->effect)].audio_haptic ==
              entry.profile);
    }
}
} // namespace fallout4_adapter::policy_tests
