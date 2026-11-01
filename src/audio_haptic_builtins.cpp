#include <inputnext_adapters/audio_haptics.hpp>

#include <inputnext_adapters/audio_haptic_builtins.hpp>

#include <span>

namespace inputnext::adapters {
AudioHapticBankResult builtin_audio_haptic_bank(AudioHapticGame game) {
    switch (game) {
    case AudioHapticGame::fallout4:
        return parse_audio_haptic_bank(std::as_bytes(std::span{detail::fallout4_bank}));
    default:
        break;
    }
    return {.status = AudioHapticBankStatus::invalid, .bank = {}};
}
} // namespace inputnext::adapters
