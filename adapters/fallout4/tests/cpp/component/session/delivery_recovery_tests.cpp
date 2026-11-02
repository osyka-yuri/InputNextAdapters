#include "fake_output.hpp"
#include "support.hpp"

#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/session.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <algorithm>
#include <chrono>
#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace fallout4_adapter::session_tests {

TEST_CASE("fallout4.session.context_change_drops_event_after_busy_query",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output, Config{});
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    PolicyEvent shot{};
    shot.arrived_at = now + 2ms;
    shot.weapon_identity = 17;
    require(session.enqueue(ticket, shot, shot.arrived_at), "event admitted before context change");
    output.next_resolve_status = INPUT_NEXT_STATUS_BUSY;
    session.tick(now + 2ms);
    require(session.diagnostics().delivery.last_status == INPUT_NEXT_STATUS_BUSY,
        "context fixture sees BUSY");
    session.control(Control::context_changed, now + 3ms);
    session.tick(now + 3ms);
    require(!session.enqueue(ticket, shot, now + 3ms),
        "menu transition rejects old callback epoch");
    const auto current_opt = session.ticket();
    const auto& current = require_value(current_opt, "context change renews native sampling epoch");
    require(current.epoch != ticket.epoch, "context change renews native sampling epoch");
    require(session.publish(current, observation(now + 4ms, Mode::vats_playback), now + 4ms),
        "fresh playback observation admitted");
    session.tick(now + 4ms);
    require(output.bodies.back().request.fallback_rumble.low_frequency == 0.0F &&
                output.bodies.back().request.fallback_rumble.high_frequency == 0.0F,
        "context transition never replays a pending gameplay shot");
}

TEST_CASE("fallout4.session.stale_observation_withdraws_outputs",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output, Config{});
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    session.tick(now + 152ms);
    require(output.trigger_clears > 0 && output.body_clears > 0,
        "stale observation withdraws outputs");
    require(!session.publish(ticket, observation(now + 500ms), now + 200ms),
        "future observation rejected");
    require(!session.publish(ticket, observation(now), now + 200ms),
        "expired observation rejected");
}

TEST_CASE("fallout4.session.busy_query_preserves_delivery_until_recovery",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput expected_output;
    const auto expected_owner = std::make_unique<Session>(expected_output, Config{});
    Session& expected = *expected_owner;
    const auto expected_ticket = activate(expected, now);

    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output, Config{});
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    PolicyEvent shot{};
    shot.kind = EventKind::shot;
    shot.arrived_at = now + 2ms;
    shot.weapon_identity = 17;
    require(expected.enqueue(expected_ticket, shot, shot.arrived_at), "reference shot is admitted");
    require(session.enqueue(ticket, shot, shot.arrived_at),
        "shot is admitted before a transient BUSY query");

    const auto triggers_before_busy = output.triggers.size();
    const auto bodies_before_busy = output.bodies.size();
    const auto trigger_clears_before_busy = output.trigger_clears;
    const auto body_clears_before_busy = output.body_clears;
    output.next_resolve_status = INPUT_NEXT_STATUS_BUSY;
    session.tick(now + 2ms);
    require(session.diagnostics().delivery.last_status == INPUT_NEXT_STATUS_BUSY,
        "target BUSY is diagnosed");
    require(output.triggers.size() == triggers_before_busy &&
                output.bodies.size() == bodies_before_busy &&
                output.trigger_clears == trigger_clears_before_busy &&
                output.body_clears == body_clears_before_busy,
        "a BUSY target query performs no policy delivery or output mutation");

    expected.tick(now + 3ms);
    session.tick(now + 3ms);
    expected.tick(now + 4ms);
    session.tick(now + 4ms);
    require(output.bodies.size() == expected_output.bodies.size(),
        "the retained shot is consumed once");
    require(output.bodies.back().request.fallback_rumble.low_frequency ==
                    expected_output.bodies.back().request.fallback_rumble.low_frequency &&
                output.bodies.back().request.fallback_rumble.high_frequency ==
                    expected_output.bodies.back().request.fallback_rumble.high_frequency &&
                output.bodies.back().route == expected_output.bodies.back().route,
        "BUSY recovery matches one uninterrupted delivery without replaying the shot");
}

TEST_CASE("fallout4.session.weapon_change_after_busy_rejects_old_shot",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    output.configured_capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.configured_capabilities.audio_instance_id = 931;
    const auto session_owner = std::make_unique<Session>(output, Config{});
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    PolicyEvent old_shot{};
    old_shot.kind = EventKind::shot;
    old_shot.arrived_at = now + 2ms;
    old_shot.weapon_identity = 17;
    require(session.enqueue(ticket, old_shot, old_shot.arrived_at),
        "old-weapon shot is accepted before BUSY");
    output.next_resolve_status = INPUT_NEXT_STATUS_BUSY;
    session.tick(now + 2ms);

    auto replacement = observation(now + 3ms);
    replacement.weapon.identity = 18;
    replacement.weapon.family = WeaponFamily::shotgun;
    require(session.publish(ticket, replacement, now + 3ms),
        "replacement weapon observation is published");
    const auto pulses_before_replacement = shot_pulse_count(output);
    const auto bodies_before_replacement = output.bodies.size();
    session.tick(now + 3ms);
    require(shot_pulse_count(output) == pulses_before_replacement &&
                output.bodies.size() == bodies_before_replacement,
        "a retained old-weapon event is rejected under the replacement weapon");

    PolicyEvent current_shot{};
    current_shot.kind = EventKind::shot;
    current_shot.arrived_at = now + 4ms;
    current_shot.weapon_identity = 18;
    require(session.enqueue(ticket, current_shot, current_shot.arrived_at),
        "replacement-weapon shot is admitted");
    session.tick(now + 4ms);
    require(shot_pulse_count(output) > pulses_before_replacement &&
                output.bodies.size() > bodies_before_replacement,
        "a post-boundary shot uses the replacement weapon normally");
}

TEST_CASE("fallout4.session.busy_event_expires_while_observation_stale",
    "[component][fallout4][session]") {
    const auto now = MonoTime{} + 1s;
    FakeOutput output;
    output.configured_capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.configured_capabilities.audio_instance_id = 930;
    const auto session_owner = std::make_unique<Session>(output, Config{});
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    PolicyEvent shot{};
    shot.kind = EventKind::shot;
    shot.arrived_at = now + 2ms;
    shot.weapon_identity = 17;
    require(session.enqueue(ticket, shot, shot.arrived_at), "event is queued before a BUSY query");
    output.next_resolve_status = INPUT_NEXT_STATUS_BUSY;
    session.tick(now + 2ms);
    require(session.diagnostics().delivery.last_status == INPUT_NEXT_STATUS_BUSY,
        "expiry fixture sees BUSY");

    session.tick(now + 200ms);
    const auto pulses_before_fresh = shot_pulse_count(output);
    const auto fresh_at = now + 201ms;
    require(session.publish(ticket, observation(fresh_at), fresh_at),
        "fresh observation follows expiry");
    session.tick(fresh_at);
    require(shot_pulse_count(output) == pulses_before_fresh &&
                std::ranges::none_of(output.bodies,
                    [](const auto& body) {
                        return body.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE;
                    }),
        "a retained event expires during a stale observation and cannot replay on recovery");
}

TEST_CASE("fallout4.session.slow_target_poll_withdraws_expired_output",
    "[component][fallout4][session]") {
    auto target_poll_delay = 170ms;
    SECTION("poll returns near the body lease deadline") {
        target_poll_delay = 170ms;
    }
    SECTION("poll outlives the body lease") {
        target_poll_delay = 220ms;
    }

    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output, Config{});
    Session& session = *session_owner;
    const auto activated_at = std::chrono::steady_clock::now();
    const auto ticket = activate(session, activated_at - 10ms);

    const auto sampled_at = std::chrono::steady_clock::now();
    require(session.publish(ticket, observation(sampled_at), sampled_at),
        "current observation before the slow poll");

    PolicyEvent shot{};
    shot.kind = EventKind::shot;
    shot.weapon_identity = 17;
    shot.arrived_at = std::chrono::steady_clock::now();
    require(session.enqueue(ticket, shot, shot.arrived_at),
        "current shot is accepted before the slow poll");
    session.tick(shot.arrived_at);
    require(!output.triggers.empty() && !output.bodies.empty(),
        "the shot has accepted trigger and body output");

    const auto trigger_submissions = output.triggers.size();
    const auto body_submissions = output.bodies.size();
    const auto trigger_clears = output.trigger_clears;
    const auto body_clears = output.body_clears;
    const auto body_retained_until =
        shot.arrived_at + std::chrono::milliseconds{output.bodies.back().request.ttl_ms};
    output.next_get_target_delay = target_poll_delay;

    const auto slow_tick_started = std::chrono::steady_clock::now();
    session.tick();
    const auto slow_tick_finished = std::chrono::steady_clock::now();
    require(slow_tick_finished - slow_tick_started >= target_poll_delay &&
                output.next_get_target_delay == 0ms,
        "the fake target poll consumes its one-shot delay");
    require(output.triggers.size() == trigger_submissions,
        "expired admission sends no fresh trigger after I/O");
    require(output.bodies.size() == body_submissions,
        "expired admission sends no fresh body request after I/O");
    require(output.trigger_clears > trigger_clears,
        "slow target I/O withdraws previously accepted triggers");
    require(!session.diagnostics().delivery.body_owner_present,
        "slow target I/O retires the previously accepted body owner");
    require(output.body_clears > body_clears || slow_tick_finished >= body_retained_until,
        "the body lease is explicitly cleared unless its retention deadline has elapsed");

    const auto fresh_at = std::chrono::steady_clock::now();
    require(session.publish(ticket, observation(fresh_at), fresh_at),
        "fresh observation reopens the same epoch");
    const auto before_rebaseline = output.triggers.size();
    session.tick(fresh_at);
    require(output.triggers.size() > before_rebaseline,
        "fresh state establishes a new trigger baseline");
}

} // namespace fallout4_adapter::session_tests
