#include "fake_output.hpp"
#include "support.hpp"

#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/session.hpp>

#include <input_next_effects.h>

#include <algorithm>
#include <chrono>
#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace fallout4_adapter::session_tests {

TEST_CASE("fallout4.session.event_mode_and_timestamp_boundaries",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output, Config{});
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    PolicyEvent shot{};
    shot.arrived_at = now + 2ms;
    shot.weapon_identity = 17;
    require(session.publish(ticket, observation(now + 3ms), now + 3ms),
        "newer sample in same mode");
    require(session.enqueue(ticket, shot, now + 4ms),
        "recent event predating the latest sample is accepted");
    session.tick(now + 4ms);
    require(output.bodies.back().request.fallback_rumble.low_frequency > 0.0F,
        "delayed current shot produces recoil");
    require(session.publish(ticket, observation(now + 5ms, Mode::vats_playback), now + 5ms),
        "playback starts");
    shot.arrived_at = now + 4ms;
    require(!session.enqueue(ticket, shot, now + 6ms), "pre-transition shot cannot enter playback");
    shot.arrived_at = now + 20ms;
    require(!session.enqueue(ticket, shot, now + 6ms), "future event is rejected before queuing");
}

TEST_CASE("fallout4.session.post_drain_event_accepts_current_shot",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output, Config{});
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    const auto worker_snapshot = now + 2ms;
    PolicyEvent shot{};
    shot.kind = EventKind::shot;
    shot.weapon_identity = 17;
    shot.arrived_at = now + 3ms;
    const auto after_drain_validation = now + 4ms;
    require(shot.arrived_at > worker_snapshot,
        "the producer arrives after the worker loop snapshot");
    require(session.enqueue(ticket, shot, after_drain_validation),
        "post-drain validation accepts an event produced after the worker loop snapshot");
    session.tick(after_drain_validation);
    require(output.bodies.back().request.fallback_rumble.low_frequency > 0.0F,
        "the current event produces its body cue");
}

TEST_CASE("fallout4.session.delayed_shot_keeps_remaining_trigger_lifetime",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output, Config{});
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    const auto observation_at = now + 1ms;
    PolicyEvent shot{};
    shot.kind = EventKind::shot;
    shot.weapon_identity = 17;
    shot.arrived_at = observation_at + 30ms;
    require(session.enqueue(ticket, shot, shot.arrived_at),
        "shot newer than the latest observation is accepted");

    session.tick(observation_at + 40ms);
    const auto pulse = std::ranges::find_if(output.triggers.rbegin(),
        output.triggers.rend(),
        [](const auto& request) {
            return request.channel_mask == INPUT_NEXT_EFFECTS_RIGHT_TRIGGER &&
                   request.right_trigger.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION;
        });
    require(pulse != output.triggers.rend(), "the shot creates its short trigger pulse");
    require(pulse->ttl_ms == 38U, "the 10 ms-old shot retains its 38 ms trigger lifetime");
    require(output.bodies.back().route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC &&
                output.bodies.back().request.fallback_rumble.low_frequency > 0.0F &&
                output.bodies.back().request.fallback_rumble.high_frequency > 0.0F,
        "the classic fallback remains active when the frame is sent");

    session.tick(observation_at + 55ms);
    require(output.bodies.back().route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC &&
                output.bodies.back().request.fallback_rumble.low_frequency > 0.0F &&
                output.bodies.back().request.fallback_rumble.high_frequency > 0.0F,
        "classic fallback remains alive 15 ms into its undoubled remaining lifetime");
}

TEST_CASE("fallout4.session.opt_in_vats_cue_survives_gameplay_withdrawal",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    Config config{};
    config.policy.ui_cues = true;
    const auto session_owner = std::make_unique<Session>(output, config);
    Session& session = *session_owner;
    static_cast<void>(activate(session, now));
    session.control(Control::context_changed, now + 2ms);
    session.tick(now + 2ms);
    const auto ticket = session.ticket();
    require(ticket &&
                session.publish(*ticket, observation(now + 3ms, Mode::vats_selection), now + 3ms),
        "fresh selection after menu notice");
    session.tick(now + 3ms);
    session.tick(now + 13ms);
    require(output.bodies.back().request.fallback_rumble.high_frequency > 0.0F,
        "opt-in VATS cue survives prompt withdrawal of gameplay effects");
}

} // namespace fallout4_adapter::session_tests
