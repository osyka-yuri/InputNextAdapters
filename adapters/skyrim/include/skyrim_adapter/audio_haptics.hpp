#pragma once

#include <skyrim_adapter/effect_settings.hpp>

#include <inputnext_adapters/audio_haptics.hpp>

#include <cstddef>
#include <filesystem>

namespace skyrim_adapter {
struct AudioHapticLoadResult {
    inputnext::adapters::AudioHapticBankStatus status{
        inputnext::adapters::AudioHapticBankStatus::not_found};
    std::size_t loaded_profiles{};
};

[[nodiscard]] std::size_t bind_audio_haptics(const inputnext::adapters::AudioHapticBank& bank,
    inputnext::adapters::AudioHapticGame expected_game,
    EffectSettings& settings) noexcept;
[[nodiscard]] AudioHapticLoadResult load_audio_haptics(const std::filesystem::path& config_path,
    inputnext::adapters::AudioHapticGame expected_game,
    EffectSettings& settings) noexcept;
} // namespace skyrim_adapter
