#pragma once

#include "effect_model.hpp"
#include "memory_guard.hpp"
#include "xobse_sdk.hpp"

#include <cstring>

namespace oblivion_adapter::native {

namespace detail {

// OnHealthDamage packs the float bits into the pointer-sized argument.
[[nodiscard]] inline float decode_damage_payload(const void* raw_arg) noexcept {
    float damage_value = 0.0F;
    static_assert(sizeof(const void*) == sizeof(float));
    std::memcpy(&damage_value, static_cast<const void*>(&raw_arg), sizeof(float));
    return damage_value;
}

// The player-only hook passes a null calling object and a temporary reference to
// the filled soul gem in the player's inventory.
[[nodiscard]] inline bool read_player_soul_trap(const void* captured_arg,
    const void* gem_arg,
    const TESObjectREFR* calling_object,
    EffectEvent& event) noexcept {
    if (calling_object || !captured_arg || !gem_arg) {
        return false;
    }
    return guarded_game_read([&] {
        const auto* captured = static_cast<const TESForm*>(captured_arg);
        const auto* gem = static_cast<const TESForm*>(gem_arg);
        if ((captured->typeID != kFormType_ACHR && captured->typeID != kFormType_ACRE) ||
            captured->refID == 0 || gem->typeID != kFormType_REFR) {
            return false;
        }
        const auto* base = static_cast<const TESObjectREFR*>(gem)->baseForm;
        if (!base || base->typeID != kFormType_SoulGem || base->refID == 0) {
            return false;
        }
        event.target_id = captured->refID;
        event.source_id = base->refID;
        return true;
    });
}

} // namespace detail

class EventHandler {
public:
    static void register_events(OBSEEventManagerInterface* event_mgr) noexcept;
    static void flush_contacts() noexcept;

    static void on_health_damage(void* arg0, void* arg1, TESObjectREFR* this_object) noexcept;

    [[nodiscard]] static DamageEvent interpret_health_damage(void* arg0,
        void* arg1,
        TESObjectREFR* this_object,
        PlayerCharacter* player) noexcept;

    static void discard_contacts() noexcept;
};

} // namespace oblivion_adapter::native
