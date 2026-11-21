#pragma once

#include "model.hpp"

#include <cstdint>
#include <mutex>

namespace oblivion_adapter {

class ObservationManager {
public:
    static ObservationManager& instance() noexcept;

    ObservationManager(const ObservationManager&) = delete;
    ObservationManager& operator=(const ObservationManager&) = delete;
    ObservationManager(ObservationManager&&) = delete;
    ObservationManager& operator=(ObservationManager&&) = delete;

    [[nodiscard]] Observation get_observation() const noexcept;

    void update_player_state(std::uint32_t form_id,
        const PlayerHealth& health,
        bool alive,
        bool in_combat) noexcept;

    [[nodiscard]] bool record_damage_event(const DamageEvent& event) noexcept;

    // Temporary unavailability, including cell loading, keeps the current save epoch.
    void invalidate_player() noexcept;

    void increment_tick() noexcept;

    // Start a new epoch for a game session boundary.
    void reset() noexcept;

    [[nodiscard]] std::uint64_t current_epoch() const noexcept;

private:
    ObservationManager() = default;
    ~ObservationManager() = default;

    void clear_player_state_locked() noexcept;

    mutable std::mutex mutex_;
    Observation observation_{.epoch = 1};
    std::uint64_t current_epoch_{1};
    std::uint64_t next_damage_sequence_{1};
};

} // namespace oblivion_adapter
