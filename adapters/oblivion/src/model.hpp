#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace oblivion_adapter {

struct PlayerHealth {
    float current{0.0F};
    float maximum{0.0F};
    float ratio{0.0F};
    bool valid{false};

    [[nodiscard]] constexpr bool operator==(const PlayerHealth& other) const noexcept = default;
};

struct DamageEvent {
    float damage{0.0F};
    std::uint32_t attacker_form_id{0};
    std::uint32_t victim_form_id{0};
    bool is_player_victim{false};
    /// Set only when xOBSE's attacker pointer matches the player.
    bool is_player_attacker{false};
    /// The damage sequence increases across session resets.
    std::uint64_t sequence{0};
    std::uint64_t epoch{0};

    [[nodiscard]] constexpr bool operator==(const DamageEvent& other) const noexcept = default;
};

struct Observation {
    PlayerHealth health{};
    /// Latest accepted damage event. Player unavailability clears it.
    DamageEvent last_damage{};
    /// Counts accepted damage events in this epoch, including before temporary unavailability.
    std::uint64_t damage_event_count{0};
    std::uint32_t player_form_id{0};
    bool player_alive{false};
    bool player_in_combat{false};
    bool player_available{false};
    /// Counts ticks in this epoch, including menus and unreadable player state.
    std::uint64_t tick_count{0};
    std::uint64_t epoch{0};

    [[nodiscard]] constexpr bool operator==(const Observation& other) const noexcept = default;
};

struct DamageClassificationInput {
    float damage{0.0F};
    const void* victim_ptr{nullptr};
    const void* player_ptr{nullptr};
    const void* attacker_ptr{nullptr};
    std::uint32_t victim_form_id{0};
    std::uint32_t attacker_form_id{0};
};

[[nodiscard]] inline DamageEvent classify_damage_event(
    const DamageClassificationInput& input) noexcept {
    DamageEvent event{};
    if (!input.player_ptr || !input.victim_ptr || input.victim_ptr != input.player_ptr) {
        return event;
    }
    if (!std::isfinite(input.damage) || input.damage <= 0.0F) {
        return event;
    }
    event.is_player_victim = true;
    event.damage = input.damage;
    event.is_player_attacker =
        (input.attacker_ptr != nullptr && input.attacker_ptr == input.player_ptr);
    event.victim_form_id = input.victim_form_id;
    event.attacker_form_id = input.attacker_form_id;
    return event;
}

[[nodiscard]] inline PlayerHealth calculate_health(float current, float maximum) noexcept {
    PlayerHealth health{};
    if (!std::isfinite(current) || !std::isfinite(maximum) || maximum <= 0.0F) {
        health.current = 0.0F;
        health.maximum = (std::isfinite(maximum) && maximum > 0.0F) ? maximum : 0.0F;
        health.ratio = 0.0F;
        health.valid = false;
        return health;
    }

    current = std::max(current, 0.0F);
    health.current = current;
    health.maximum = maximum;
    health.ratio = std::clamp(current / maximum, 0.0F, 1.0F);
    health.valid = true;
    return health;
}

} // namespace oblivion_adapter
