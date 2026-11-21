#pragma once

#include "effect_settings.hpp"

#include <inputnext_adapters/audio_haptics.hpp>

#include <cstddef>
#include <filesystem>

namespace oblivion_adapter {

struct AudioLoadResult {
    inputnext::adapters::AudioHapticBankStatus status{};
    std::size_t profiles{};
};

[[nodiscard]] AudioLoadResult load_audio_haptics(const std::filesystem::path& config_path,
    EffectSettings& settings) noexcept;

} // namespace oblivion_adapter
