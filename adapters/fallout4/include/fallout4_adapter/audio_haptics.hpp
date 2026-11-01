#pragma once

#include <fallout4_adapter/effect_settings.hpp>

#include <inputnext_adapters/audio_haptics.hpp>

#include <cstddef>
#include <filesystem>

namespace fallout4_adapter {
struct AudioHapticLoadResult {
    inputnext::adapters::AudioHapticBankStatus status{
        inputnext::adapters::AudioHapticBankStatus::not_found};
    std::size_t loaded_profiles{};
};

[[nodiscard]] std::size_t bind_audio_haptics(const inputnext::adapters::AudioHapticBank& bank,
    EffectSettings& settings) noexcept;
[[nodiscard]] AudioHapticLoadResult load_audio_haptics(const std::filesystem::path& config_path,
    EffectSettings& settings) noexcept;
} // namespace fallout4_adapter
