#pragma once

#include "event_reducer.hpp"

#include <fallout_gamebryo/model.hpp>

#include <cstdint>
#include <optional>

struct ActorHitData;

namespace fallout_new_vegas {
struct HitForms {
    std::uint32_t source{};
    std::uint32_t target{};
    std::uint32_t weapon{};
    fallout_gamebryo::WeaponClass weapon_class{fallout_gamebryo::WeaponClass::none};
};

struct ReactionState {
    std::uint8_t knocked_state{};
    std::int32_t animation_action{};
};

[[nodiscard]] std::optional<NativeSignal>
read_hit_signal(const ActorHitData& hit, const HitForms& forms, bool incoming) noexcept;

[[nodiscard]] std::optional<fallout_gamebryo::PlayerReaction> read_player_reaction(
    const ReactionState& state) noexcept;
} // namespace fallout_new_vegas
