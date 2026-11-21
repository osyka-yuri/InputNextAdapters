#pragma once

#include "effect_model.hpp"
#include "model.hpp"
#include "xobse_sdk.hpp"

#include <cstdint>
#include <optional>

namespace oblivion_adapter::native {

struct ProjectileScan;

class PlayerStateReader {
public:
    [[nodiscard]] static PlayerCharacter* get_player() noexcept;
    [[nodiscard]] static bool is_player_pointer_valid(const PlayerCharacter* player) noexcept;
    [[nodiscard]] static bool is_game_mode() noexcept;
    [[nodiscard]] static EffectMode read_effect_mode() noexcept;

    [[nodiscard]] static std::optional<PlayerHealth> read_player_health(
        PlayerCharacter* player) noexcept;
    [[nodiscard]] static std::optional<bool> read_player_alive(PlayerCharacter* player) noexcept;
    [[nodiscard]] static std::optional<bool> read_player_in_combat(
        PlayerCharacter* player) noexcept;
    [[nodiscard]] static std::optional<std::uint32_t> read_player_form_id(
        const PlayerCharacter* player) noexcept;
    [[nodiscard]] static std::uint32_t read_player_cell_id(PlayerCharacter* player) noexcept;

    // Guard SDK calls and pointer reads with SEH using the Oblivion 1.2.0.416 layouts.
    static void read_effect_state(PlayerCharacter* player,
        EffectObservation& observation,
        bool read_stealth) noexcept;
    [[nodiscard]] static Weapon read_weapon(const TESForm* form) noexcept;
    [[nodiscard]] static Weapon read_equipped_weapon(PlayerCharacter* player,
        std::uint32_t& out_id) noexcept;
    [[nodiscard]] static bool read_projectile_actor(const TESForm* form,
        const PlayerCharacter* player) noexcept;
    [[nodiscard]] static bool
    scan_projectiles(PlayerCharacter* player, std::uint32_t cell_id, ProjectileScan& scan) noexcept;
};

} // namespace oblivion_adapter::native
