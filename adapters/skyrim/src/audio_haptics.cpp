#include <skyrim_adapter/audio_haptics.hpp>
#include <skyrim_adapter/effect_settings.hpp>

#include <inputnext_adapters/audio_haptic_profile.hpp>
#include <inputnext_adapters/audio_haptics.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>

namespace skyrim_adapter {
namespace {
bool supports_held_audio_profile(EffectId id) noexcept {
    switch (id) {
    case EffectId::bow_tension_draw:
    case EffectId::bow_tension_attached:
    case EffectId::bow_tension_drawn:
    case EffectId::fire_held:
    case EffectId::frost_held:
    case EffectId::shock_held:
    case EffectId::healing_held:
    case EffectId::ward_held:
    case EffectId::generic_held:
        return true;
    default:
        return false;
    }
}

void clear_audio_profiles(EffectSettings& settings) noexcept {
    for (auto& adjustment : settings.body) {
        adjustment.audio_haptic.reset();
    }
}

std::size_t overlay_audio_haptics(const inputnext::adapters::AudioHapticBank& bank,
    inputnext::adapters::AudioHapticGame expected_game,
    EffectSettings& settings) noexcept {
    if (!settings.audio_haptics_enabled || bank.game != expected_game) {
        return 0;
    }
    std::size_t loaded{};
    for (const auto& entry : bank.entries) {
        const auto descriptor =
            std::ranges::find(effect_descriptors, entry.section, &EffectDescriptor::section);
        if (descriptor == effect_descriptors.end() ||
            (!descriptor->transient && !supports_held_audio_profile(descriptor->id)) ||
            !inputnext::adapters::valid_audio_haptic_profile(entry.profile)) {
            continue;
        }
        settings.body[static_cast<std::size_t>(descriptor->id)].audio_haptic = entry.profile;
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
    inputnext::adapters::AudioHapticGame expected_game,
    EffectSettings& settings) noexcept {
    clear_audio_profiles(settings);
    return overlay_audio_haptics(bank, expected_game, settings);
}

AudioHapticLoadResult load_audio_haptics(const std::filesystem::path& config_path,
    inputnext::adapters::AudioHapticGame expected_game,
    EffectSettings& settings) noexcept {
    using inputnext::adapters::AudioHapticBankStatus;
    clear_audio_profiles(settings);
    if (!settings.audio_haptics_enabled) {
        return {};
    }
    AudioHapticBankStatus status = AudioHapticBankStatus::invalid;
    try {
        const auto builtins = inputnext::adapters::builtin_audio_haptic_bank(expected_game);
        if (builtins.status == AudioHapticBankStatus::loaded &&
            builtins.bank.game == expected_game) {
            static_cast<void>(overlay_audio_haptics(builtins.bank, expected_game, settings));
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
        if (result.bank.game != expected_game) {
            return {.status = AudioHapticBankStatus::invalid,
                .loaded_profiles = effective_profiles};
        }
        static_cast<void>(overlay_audio_haptics(result.bank, expected_game, settings));
        return {.status = AudioHapticBankStatus::loaded,
            .loaded_profiles = count_audio_profiles(settings)};
    } catch (...) {
        return {.status = AudioHapticBankStatus::io_error,
            .loaded_profiles = count_audio_profiles(settings)};
    }
}
} // namespace skyrim_adapter
