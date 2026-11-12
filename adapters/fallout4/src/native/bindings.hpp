#pragma once

#include <F4SE/Impl/PCH.h>
#include <RE/B/BSSpinLock.h>
#include <RE/B/BSTEvent.h>

#include <cstdint>
#include <optional>

namespace fallout4_adapter::native {
struct WeaponFiredEvent;

inline constexpr std::uintptr_t register_rva = 0x476000;
inline constexpr std::uintptr_t input_context_stack_lock_rva = 0x339FB98;

struct ShotFacts {
    std::uintptr_t shooter_address{};
    std::uint64_t weapon_identity{};
};

// Validated against the supported runtime and required instruction signatures.
[[nodiscard]] bool bindings_available() noexcept;
[[nodiscard]] std::uintptr_t checked_image_base() noexcept;
[[nodiscard]] std::size_t checked_image_size() noexcept;
// Borrowed engine lock; valid only while the validated executable remains loaded.
[[nodiscard]] RE::BSReadWriteLock* input_context_stack_lock() noexcept;
[[nodiscard]] bool register_weapon_fired(const RE::BSTEventSink<WeaponFiredEvent>* sink) noexcept;
// Consume only during the synchronous native callback; no engine pointer escapes.
[[nodiscard]] std::optional<ShotFacts> decode_weapon_fired(const WeaponFiredEvent& event) noexcept;
} // namespace fallout4_adapter::native
