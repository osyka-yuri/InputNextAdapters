#pragma once

#include "native_combat.hpp"
#include "native_observation.hpp"
#include "nvse_sdk.hpp"

#include <fallout_gamebryo/model.hpp>

#include <cstdint>
#include <optional>

namespace fallout_new_vegas {
enum class PlayerAnimation : std::uint8_t { other, melee_attack, special_idle, throw_release };
enum class VatsSelection : std::uint8_t { target, limb };

struct GameSample {
    fallout_gamebryo::Observation observation;
    std::optional<VatsSelection> vats_selection{std::nullopt};
    std::optional<ReactionState> raw_reaction{std::nullopt};
    PlayerAnimation animation{PlayerAnimation::other};
    bool sneaking{};
};

[[nodiscard]] PlayerCharacter* current_player() noexcept;
[[nodiscard]] std::uint32_t form_id(const TESForm* form) noexcept;
[[nodiscard]] fallout_gamebryo::WeaponClass classify_weapon(const TESObjectWEAP* weapon) noexcept;
[[nodiscard]] GameSample sample_game(ObservationFacts facts) noexcept;
} // namespace fallout_new_vegas
