#pragma once

#include <inputnext_adapters/audio_haptic_profile.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace inputnext::adapters {
// NOLINTNEXTLINE(performance-enum-size): Preserve the public enum's 32-bit representation.
enum class AudioHapticGame : std::uint32_t { skyrim_se = 1, skyrim_le = 2, fallout4 = 3 };

struct AudioHapticEntry {
    std::string section;
    AudioHapticProfile profile;
};

struct AudioHapticBank {
    AudioHapticGame game{AudioHapticGame::skyrim_se};
    std::vector<AudioHapticEntry> entries;
};

enum class AudioHapticBankStatus : std::uint8_t { loaded, not_found, invalid, io_error };

struct AudioHapticBankResult {
    AudioHapticBankStatus status{AudioHapticBankStatus::invalid};
    AudioHapticBank bank;
};

[[nodiscard]] AudioHapticBankResult builtin_audio_haptic_bank(AudioHapticGame game);
[[nodiscard]] AudioHapticBankResult parse_audio_haptic_bank(std::span<const std::byte> bytes);
[[nodiscard]] AudioHapticBankResult load_audio_haptic_bank(
    const std::filesystem::path& path) noexcept;
} // namespace inputnext::adapters
