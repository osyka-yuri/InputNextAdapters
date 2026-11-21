#pragma once

#include "effect_model.hpp"

// This is an xOBSE engine form type; its public non-virtual destructor is part of the SDK ABI.
class TESForm; // NOLINT(cppcoreguidelines-virtual-class-destructor)

namespace oblivion_adapter::native {

// OnMagicCast requires a spell, power, scroll or staff. OnMagicApply also
// accepts effects from other sources, such as potions and enchantments.
[[nodiscard]] bool read_magic_form(const TESForm* form, EffectEvent& event) noexcept;

} // namespace oblivion_adapter::native
