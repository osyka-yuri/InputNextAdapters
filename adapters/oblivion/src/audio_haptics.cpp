#include "audio_haptics.hpp"

#include "effect_settings.hpp"

#include <inputnext_adapters/audio_haptics.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <utility>

namespace oblivion_adapter {
namespace {

using inputnext::adapters::AudioHapticBank;
using inputnext::adapters::AudioHapticBankStatus;
using inputnext::adapters::AudioHapticGame;

void overlay(AudioHapticBank bank, EffectSettings& settings) {
    const auto owned_bank = std::make_shared<const AudioHapticBank>(std::move(bank));
    for (const auto& entry : owned_bank->entries) {
        const auto match = std::ranges::find(effect_sections, entry.section);
        if (match != effect_sections.end()) {
            const auto index = static_cast<std::size_t>(match - effect_sections.begin());
            settings.profiles[index].audio = {owned_bank, &entry.profile};
        }
    }
}

std::size_t profile_count(const EffectSettings& settings) noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(settings.profiles,
        [](const ProfileAdjustment& profile) { return static_cast<bool>(profile.audio); }));
}

} // namespace

AudioLoadResult load_audio_haptics(const std::filesystem::path& config_path,
    EffectSettings& settings) noexcept {
    for (auto& profile : settings.profiles) {
        profile.audio.reset();
    }
    if (!settings.audio_haptics_enabled) {
        return {.status = AudioHapticBankStatus::not_found, .profiles = 0};
    }

    try {
        auto stock = inputnext::adapters::builtin_audio_haptic_bank(AudioHapticGame::oblivion);
        if (stock.status == AudioHapticBankStatus::loaded &&
            stock.bank.game == AudioHapticGame::oblivion) {
            overlay(std::move(stock.bank), settings);
        }

        auto path = config_path;
        path.replace_extension(L".audiohaptics");
        auto external = inputnext::adapters::load_audio_haptic_bank(path);
        if (external.status == AudioHapticBankStatus::not_found) {
            return {.status = stock.status, .profiles = profile_count(settings)};
        }
        if (external.status != AudioHapticBankStatus::loaded) {
            return {.status = external.status, .profiles = profile_count(settings)};
        }
        if (external.bank.game != AudioHapticGame::oblivion) {
            return {.status = AudioHapticBankStatus::invalid, .profiles = profile_count(settings)};
        }
        overlay(std::move(external.bank), settings);
        return {.status = AudioHapticBankStatus::loaded, .profiles = profile_count(settings)};
    } catch (...) {
        return {.status = AudioHapticBankStatus::io_error, .profiles = profile_count(settings)};
    }
}

} // namespace oblivion_adapter
