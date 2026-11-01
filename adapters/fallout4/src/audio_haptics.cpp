#include <fallout4_adapter/audio_haptics.hpp>
#include <fallout4_adapter/effect_settings.hpp>

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/audio_haptics.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>

namespace fallout4_adapter {
namespace {
void clear_audio_profiles(EffectSettings& settings) noexcept {
    for (auto& adjustment : settings.body) {
        adjustment.audio_haptic.reset();
    }
}

std::size_t overlay_audio_haptics(const inputnext::adapters::AudioHapticBank& bank,
    EffectSettings& settings) noexcept {
    using inputnext::adapters::AudioHapticGame;
    if (!settings.audio_haptics_enabled || bank.game != AudioHapticGame::fallout4) {
        return 0;
    }
    std::size_t loaded{};
    for (const auto& entry : bank.entries) {
        const auto descriptor = std::ranges::find(body_effect_descriptors,
            entry.section,
            &BodyEffectDescriptor::section);
        if (descriptor == body_effect_descriptors.end() ||
            !inputnext::adapters::valid_audio_haptic_profile(entry.profile)) {
            continue;
        }
        settings.body[static_cast<std::size_t>(descriptor->effect)].audio_haptic = entry.profile;
        ++loaded;
    }
    return loaded;
}

std::size_t count_audio_profiles(const EffectSettings& settings) noexcept {
    return static_cast<std::size_t>(std::count_if(settings.body.begin(),
        settings.body.end(),
        [](const auto& adjustment) { return adjustment.audio_haptic.has_value(); }));
}
} // namespace

std::size_t bind_audio_haptics(const inputnext::adapters::AudioHapticBank& bank,
    EffectSettings& settings) noexcept {
    clear_audio_profiles(settings);
    return overlay_audio_haptics(bank, settings);
}

AudioHapticLoadResult load_audio_haptics(const std::filesystem::path& config_path,
    EffectSettings& settings) noexcept {
    using inputnext::adapters::AudioHapticBankStatus;
    clear_audio_profiles(settings);
    if (!settings.audio_haptics_enabled) {
        return {};
    }
    AudioHapticBankStatus status = AudioHapticBankStatus::invalid;
    try {
        const auto builtins = inputnext::adapters::builtin_audio_haptic_bank(
            inputnext::adapters::AudioHapticGame::fallout4);
        if (builtins.status == AudioHapticBankStatus::loaded &&
            builtins.bank.game == inputnext::adapters::AudioHapticGame::fallout4) {
            static_cast<void>(overlay_audio_haptics(builtins.bank, settings));
            status = AudioHapticBankStatus::loaded;
        }

        auto path = config_path;
        path.replace_extension(L".audiohaptics");
        const auto result = inputnext::adapters::load_audio_haptic_bank(path);
        const std::size_t effective_profiles = count_audio_profiles(settings);
        if (result.status == AudioHapticBankStatus::not_found) {
            return {.status = status, .loaded_profiles = effective_profiles};
        }
        if (result.status != AudioHapticBankStatus::loaded) {
            return {.status = result.status, .loaded_profiles = effective_profiles};
        }
        if (result.bank.game != inputnext::adapters::AudioHapticGame::fallout4) {
            return {.status = AudioHapticBankStatus::invalid,
                .loaded_profiles = effective_profiles};
        }
        static_cast<void>(overlay_audio_haptics(result.bank, settings));
        return {.status = AudioHapticBankStatus::loaded,
            .loaded_profiles = count_audio_profiles(settings)};
    } catch (...) {
        return {.status = AudioHapticBankStatus::io_error,
            .loaded_profiles = count_audio_profiles(settings)};
    }
}
} // namespace fallout4_adapter
