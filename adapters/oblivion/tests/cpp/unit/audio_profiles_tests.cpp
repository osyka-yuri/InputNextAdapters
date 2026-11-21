#include "audio_haptics.hpp"
#include "body_effects.hpp"
#include "effect_settings.hpp"
#include "inputnext_adapters/audio_haptic_profile.hpp"
#include "inputnext_adapters/audio_haptics.hpp"
#include "inputnext_adapters/output_frame.hpp"
#include "support/test_runner.hpp"

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
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
using namespace oblivion_adapter;
using namespace inputnext::adapters;

struct ManifestWindow {
    std::string_view section;
    std::uint32_t duration_ms;
};

constexpr auto manifest_windows = std::to_array<ManifestWindow>({
#include "audio_manifest_windows.inc"
});

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

void append_word(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xFFFF'FFFFU;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((0U - (crc & 1U)) & 0xEDB8'8320U);
        }
    }
    return ~crc;
}

std::vector<std::uint8_t>
single_profile(AudioHapticGame game, EffectId effect = EffectId::bow_shot, float gain = 0.8F) {
    const auto section = effect_sections[profile_index(effect)];
    std::vector<std::uint8_t> payload{static_cast<std::uint8_t>(section.size()), 0U};
    for (const char character : section) {
        payload.push_back(static_cast<std::uint8_t>(character));
    }
    append_word(payload, std::bit_cast<std::uint32_t>(444.0F));
    append_word(payload, 0U);
    for (std::size_t index = 0; index < 2U * audio_haptic_knot_count; ++index) {
        append_word(payload, index == 9U ? std::bit_cast<std::uint32_t>(gain) : 0U);
    }
    std::vector<std::uint8_t> bytes{'I', 'N', 'H', 'B'};
    append_word(bytes, 1U);
    append_word(bytes, static_cast<std::uint32_t>(game));
    append_word(bytes, 1U);
    append_word(bytes, static_cast<std::uint32_t>(payload.size()));
    append_word(bytes, crc32(payload));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

class TemporaryBank {
public:
    TemporaryBank() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("inputnext-oblivion-audio-" + std::to_string(stamp));
        require(std::filesystem::create_directory(directory_), "create private audio test folder");
    }

    ~TemporaryBank() noexcept {
        std::error_code error;
        try {
            std::filesystem::remove_all(directory_, error);
        } catch (...) {
            // Cleanup is best-effort; allocation failures must not escape the destructor.
            error = std::make_error_code(std::errc::io_error);
        }
    }

    TemporaryBank(const TemporaryBank&) = delete;
    TemporaryBank& operator=(const TemporaryBank&) = delete;
    TemporaryBank(TemporaryBank&&) = delete;
    TemporaryBank& operator=(TemporaryBank&&) = delete;

    [[nodiscard]] std::filesystem::path config_path() const {
        return directory_ / "InputNextOblivion.ini";
    }

    void write(std::span<const std::uint8_t> bytes) const {
        std::ofstream output(directory_ / "InputNextOblivion.audiohaptics", std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        require(output.good(), "write external bank");
    }

private:
    std::filesystem::path directory_;
};

void require_manifest_profiles(const EffectSettings& settings) {
    for (const auto& window : manifest_windows) {
        const auto section = std::ranges::find(effect_sections, window.section);
        if (section == effect_sections.end()) {
            throw std::runtime_error{"unknown audio profile: " + std::string{window.section}};
        }
        const auto effect = static_cast<EffectId>(section - effect_sections.begin());
        if (!settings.profiles[profile_index(effect)].audio) {
            throw std::runtime_error{"missing stock audio profile: " + std::string{window.section}};
        }
        // Casting uses a held texture. Its source window has no transient duration to match.
        if (effect != EffectId::casting && window.duration_ms != base_effect_duration_ms(effect)) {
            throw std::runtime_error{
                "audio window must match base effect duration: " + std::string{window.section}};
        }
    }
}

void test_stock_and_external_profiles() {
    const TemporaryBank fixture;
    EffectSettings settings;
    const auto stock = load_audio_haptics(fixture.config_path(), settings);
    require(stock.status == AudioHapticBankStatus::loaded &&
                stock.profiles == std::size(manifest_windows),
        "all Oblivion stock profiles are embedded");
    require_manifest_profiles(settings);
    const auto stock_shot = settings.profiles[profile_index(EffectId::bow_shot)].audio;
    const auto stock_draw = settings.profiles[profile_index(EffectId::bow_draw)].audio;
    if (!stock_shot || !stock_draw) {
        throw std::runtime_error{"bow draw and confirmed shot both have sources"};
    }

    fixture.write(single_profile(AudioHapticGame::oblivion));
    const auto overlaid = load_audio_haptics(fixture.config_path(), settings);
    const auto& overridden_shot = settings.profiles[profile_index(EffectId::bow_shot)].audio;
    const auto& retained_draw = settings.profiles[profile_index(EffectId::bow_draw)].audio;
    require(overlaid.status == AudioHapticBankStatus::loaded &&
                overlaid.profiles == stock.profiles && overridden_shot &&
                overridden_shot->frequency == 444.0F && retained_draw &&
                *retained_draw == *stock_draw,
        "only matching stock profile is replaced by valid external bank");

    fixture.write(single_profile(AudioHapticGame::skyrim_le));
    const auto wrong_game = load_audio_haptics(fixture.config_path(), settings);
    const auto& restored_shot = settings.profiles[profile_index(EffectId::bow_shot)].audio;
    require(wrong_game.status == AudioHapticBankStatus::invalid && restored_shot &&
                *restored_shot == *stock_shot,
        "wrong edition keeps stock profiles");

    auto malformed = single_profile(AudioHapticGame::oblivion);
    malformed.pop_back();
    fixture.write(malformed);
    const auto corrupt = load_audio_haptics(fixture.config_path(), settings);
    require(corrupt.status == AudioHapticBankStatus::invalid && corrupt.profiles == stock.profiles,
        "corrupt override keeps all stock profiles");

    settings.audio_haptics_enabled = false;
    const auto disabled = load_audio_haptics(fixture.config_path(), settings);
    require(disabled.profiles == 0U && std::ranges::none_of(settings.profiles,
                                           [](const ProfileAdjustment& profile) {
                                               return static_cast<bool>(profile.audio);
                                           }),
        "opt-out clears stock and external profiles");
}

void test_silent_envelope_keeps_a_finite_casting_texture() {
    const auto casting = profile_index(EffectId::casting);
    const TemporaryBank fixture;
    EffectSettings settings;
    require(load_audio_haptics(fixture.config_path(), settings).status ==
                AudioHapticBankStatus::loaded,
        "Stock profiles load before the external override");
    const auto stock = settings.profiles[casting].audio;
    fixture.write(single_profile(AudioHapticGame::oblivion, EffectId::casting, 0.0F));
    const auto& retained = settings.profiles[casting].audio;
    require(load_audio_haptics(fixture.config_path(), settings).status ==
                    AudioHapticBankStatus::invalid &&
                stock && retained && *retained == *stock,
        "All-zero envelope fails validation and preserves the stock casting profile");

    const auto held_wave = [](const EffectSettings& effect_settings) {
        const auto effects = std::make_unique<BodyEffects>(effect_settings);
        const HeldBodyEffect held{.effect = EffectId::casting, .started_at = {}};
        OutputFrame frame;
        effects->render(frame, std::span{&held, 1}, MonoTime{});
        const auto& voice = frame.body.voices.front();
        require(voice && voice->held, "Casting still renders a held voice");
        return voice->wave;
    };
    require(std::isfinite(held_wave(settings).second_mix), "Stock fallback has a finite mix");

    // Inject the silent profile directly to exercise validation outside the bank reader.
    EffectSettings synthesized;
    const auto baseline = held_wave(synthesized);
    synthesized.profiles[casting].audio = std::make_shared<const AudioHapticProfile>(
        AudioHapticProfile{.frequency = 444.0F, .envelope = {.enabled = true}});
    const auto wave = held_wave(synthesized);
    require(std::isfinite(wave.level) && std::isfinite(wave.second_mix) && wave == baseline,
        "A silent programmatic override leaves the finite synthesized voice intact");
}

} // namespace

int main() {
    return inputnext::test::run_main("Oblivion audio profiles", [] {
        test_stock_and_external_profiles();
        test_silent_envelope_keeps_a_finite_casting_texture();
        return 0;
    });
}
