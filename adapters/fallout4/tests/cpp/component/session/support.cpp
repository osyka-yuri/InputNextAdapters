#include "support.hpp"
#include "fake_output.hpp"

#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/session.hpp>

#include <input_next_effects.h>

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace fallout4_adapter::session_tests {

Observation observation(MonoTime now, Mode mode) {
    Observation result{};
    result.sampled_at = now;
    result.mode = mode;
    result.weapon = {
        .identity = 17,
        .family = WeaponFamily::rifle,
        .automatic = false,
        .drawn = true,
        .ready = true,
    };
    result.attack_trigger = TriggerSide::right;
    result.aim_trigger = TriggerSide::left;
    return result;
}

Ticket activate(Session& session, MonoTime now) {
    session.control(Control::data_ready, now);
    session.control(Control::new_game, now);
    session.control(Control::focus_gained, now);
    const auto ticket_opt = session.ticket();
    const auto& ticket = require_value(ticket_opt, "native lifecycle admits observation ticket");
    require(session.publish(ticket, observation(now), now), "initial observation");
    session.tick(now);
    require(session.publish(ticket, observation(now + 1ms), now + 1ms), "postbinding observation");
    session.tick(now + 1ms);
    return ticket;
}

std::size_t shot_pulse_count(const FakeOutput& output) {
    return static_cast<std::size_t>(std::count_if(output.triggers.begin(),
        output.triggers.end(),
        [](const input_next_effects_request_v1& request) {
            return request.channel_mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER &&
                   request.right_trigger.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION;
        }));
}

} // namespace fallout4_adapter::session_tests
