#include <inputnext_adapters/audio_haptics.hpp>

#include <inputnext_adapters/audio_haptic_builtins.hpp>

#include <span>

namespace inputnext::adapters {
AudioHapticBankResult builtin_audio_haptic_bank(AudioHapticGame game) {
    switch (game) {
    case AudioHapticGame::skyrim_se:
        return parse_audio_haptic_bank(std::as_bytes(std::span{detail::skyrim_se_bank}));
    case AudioHapticGame::skyrim_le:
        return parse_audio_haptic_bank(std::as_bytes(std::span{detail::skyrim_le_bank}));
    case AudioHapticGame::fallout4:
        return parse_audio_haptic_bank(std::as_bytes(std::span{detail::fallout4_bank}));
    }
    return {.status = AudioHapticBankStatus::invalid, .bank = {}};
}
} // namespace inputnext::adapters
