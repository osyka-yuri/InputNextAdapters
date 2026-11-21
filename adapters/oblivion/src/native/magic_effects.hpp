#pragma once

#include "effect_model.hpp"

#include <cstdint>

namespace oblivion_adapter::native {

[[nodiscard]] constexpr std::uint32_t
effect_code(char first_byte, char second_byte, char third_byte, char fourth_byte) noexcept {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(first_byte)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(second_byte)) << 8U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(third_byte)) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(fourth_byte)) << 24U);
}

// EffectSetting/EffectItem store the little-endian codes listed by xOBSE GameForms.cpp.
[[nodiscard]] constexpr MagicKind classify_magic_effect(std::uint32_t code) noexcept {
    switch (code) {
    case effect_code('F', 'I', 'D', 'G'):
        return MagicKind::fire;
    case effect_code('F', 'R', 'D', 'G'):
        return MagicKind::frost;
    case effect_code('S', 'H', 'D', 'G'):
        return MagicKind::shock;
    case effect_code('R', 'E', 'H', 'E'):
        return MagicKind::healing;
    case effect_code('S', 'H', 'L', 'D'):
    case effect_code('F', 'I', 'S', 'H'):
    case effect_code('F', 'R', 'S', 'H'):
    case effect_code('L', 'I', 'S', 'H'):
        return MagicKind::shield;
    default:
        return MagicKind::generic;
    }
}

struct CastEffects {
    void add(std::uint32_t code) noexcept {
        const auto kind = classify_magic_effect(code);
        if (!present_) {
            common_ = kind;
            present_ = true;
        } else if (common_ != kind) {
            common_ = MagicKind::generic;
        }
    }

    [[nodiscard]] MagicKind kind(bool complete) const noexcept {
        return complete ? common_ : MagicKind::generic;
    }

private:
    MagicKind common_{MagicKind::generic};
    bool present_{};
};

} // namespace oblivion_adapter::native
