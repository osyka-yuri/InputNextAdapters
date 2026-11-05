#include "session_test_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <skyrim_adapter/model.hpp>
#include <stdexcept>
#include <vector>

namespace skyrim_adapter::session_test {

[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(message != nullptr ? message : "session test failure");
}

Observation idle() {
    Observation observation{};
    observation.player_id = 0x14;
    observation.available = observation.alive = observation.focused = observation.menus_clear =
        true;
    observation.weapon_drawn = true;
    observation.health_ratio = 1.0F;
    return observation;
}

Observation casting() {
    auto observation = idle();
    auto& hand = observation.hands[0];
    hand.equipped_id = hand.caster_source_id = 77;
    hand.equipment = EquipmentKind::spell;
    hand.spell = SpellKind::shock;
    hand.cast_mode = CastMode::concentration;
    hand.cast_phase = CastPhase::casting;
    return observation;
}

RawEvent raw_event(EventKind kind, FormId actor, MonoTime observed_at) {
    RawEvent event{};
    event.kind = kind;
    event.actor_id = actor;
    event.target_id = 0;
    event.observed_at = observed_at;
    return event;
}

RawEvent raw_event(EventKind kind, Combatants combatants, MonoTime observed_at) {
    RawEvent event{};
    event.kind = kind;
    event.actor_id = combatants.actor;
    event.target_id = combatants.target;
    event.observed_at = observed_at;
    return event;
}

Action spell_release(Hand hand, MonoTime observed_at, std::uint64_t sequence, SpellKind spell) {
    Action action{};
    action.event = raw_event(EventKind::spell_fire, 0x14, observed_at);
    action.event.hand = hand;
    action.event.sequence = sequence;
    action.source_kind = EquipmentKind::spell;
    action.spell = spell;
    action.cast_mode = CastMode::fire_and_forget;
    action.source_known = true;
    action.outgoing = true;
    return action;
}

float channel_peak(const std::vector<float>& samples, std::size_t channel) {
    float peak = 0.0F;
    for (std::size_t index = channel; index < samples.size(); index += 2) {
        peak = std::max(peak, std::abs(samples[index]));
    }
    return peak;
}

} // namespace skyrim_adapter::session_test
