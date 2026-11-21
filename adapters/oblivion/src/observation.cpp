#include "observation.hpp"
#include "model.hpp"

#include <cmath>
#include <cstdint>
#include <mutex>

namespace oblivion_adapter {

ObservationManager& ObservationManager::instance() noexcept {
    static ObservationManager s_instance;
    return s_instance;
}

Observation ObservationManager::get_observation() const noexcept {
    std::scoped_lock lock(mutex_);
    return observation_;
}

std::uint64_t ObservationManager::current_epoch() const noexcept {
    std::scoped_lock lock(mutex_);
    return current_epoch_;
}

void ObservationManager::clear_player_state_locked() noexcept {
    observation_.player_available = false;
    observation_.health = PlayerHealth{};
    observation_.player_alive = false;
    observation_.player_in_combat = false;
    observation_.player_form_id = 0;
    observation_.last_damage = DamageEvent{};
}

void ObservationManager::update_player_state(std::uint32_t form_id,
    const PlayerHealth& health,
    bool alive,
    bool in_combat) noexcept {
    std::scoped_lock lock(mutex_);
    if (!health.valid) {
        clear_player_state_locked();
        return;
    }
    observation_.player_form_id = form_id;
    observation_.health = health;
    observation_.player_alive = alive;
    observation_.player_in_combat = in_combat;
    observation_.player_available = true;
    observation_.epoch = current_epoch_;
}

bool ObservationManager::record_damage_event(const DamageEvent& event) noexcept {
    std::scoped_lock lock(mutex_);
    if (!observation_.player_available || !event.is_player_victim || !std::isfinite(event.damage) ||
        event.damage <= 0.0F) {
        return false;
    }
    DamageEvent sequenced = event;
    sequenced.epoch = current_epoch_;
    sequenced.sequence = next_damage_sequence_++;
    observation_.last_damage = sequenced;
    ++observation_.damage_event_count;
    return true;
}

void ObservationManager::invalidate_player() noexcept {
    std::scoped_lock lock(mutex_);
    clear_player_state_locked();
}

void ObservationManager::increment_tick() noexcept {
    std::scoped_lock lock(mutex_);
    ++observation_.tick_count;
}

void ObservationManager::reset() noexcept {
    std::scoped_lock lock(mutex_);
    ++current_epoch_;
    observation_ = Observation{};
    observation_.epoch = current_epoch_;
}

} // namespace oblivion_adapter
