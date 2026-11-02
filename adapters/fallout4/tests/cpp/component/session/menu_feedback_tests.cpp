#include "fake_output.hpp"
#include "support.hpp"

#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/session.hpp>
#include <input_next_effects.h>
#include <inputnext_adapters/led_policy.hpp>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>

namespace fallout4_adapter::session_tests {
namespace {
void require_neutral(const BodySubmission& submission) {
    CHECK(submission.request.fallback_rumble.low_frequency == 0.0F);
    CHECK(submission.request.fallback_rumble.high_frequency == 0.0F);
    CHECK(std::ranges::all_of(submission.samples, [](float value) { return value == 0.0F; }));
    CHECK(submission.request.ttl_ms > 0U);
    CHECK(submission.request.ttl_ms <= 1000U);
}
} // namespace

TEST_CASE("fallout4.session.menu_transition_keeps_silence_without_releasing_body",
    "[component][fallout4][session][menu]") {
    const auto now = MonoTime{} + 10s;
    FakeOutput output;
    output.configured_capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.configured_capabilities.audio_instance_id = 18U;
    output.accept_hd_route = true;
    const auto session_owner = std::make_unique<Session>(output);
    Session& session = *session_owner;
    const auto old_ticket = activate(session, now);

    const auto active = observation(now + 40ms);
    REQUIRE(session.publish(old_ticket, active, active.sampled_at));
    PolicyEvent shot{};
    shot.kind = EventKind::shot;
    shot.arrived_at = active.sampled_at;
    shot.weapon_identity = active.weapon.identity;
    REQUIRE(session.enqueue(old_ticket, shot, shot.arrived_at));
    session.tick(active.sampled_at);
    REQUIRE(!output.bodies.empty());
    REQUIRE(output.bodies.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
    const auto lease = output.bodies.back().request.lease_id;

    session.control(Control::context_changed, now + 42ms);
    session.tick(now + 42ms);
    INFO("the pending UI sample stops PCM on the existing lease and keeps zero rumble owned");
    CHECK(output.body_clears == 0U);
    CHECK(output.bodies.back().request.lease_id == lease);
    CHECK(output.bodies.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP);
    require_neutral(output.bodies.back());
    CHECK(!session.enqueue(old_ticket, shot, now + 42ms));

    const auto menu_ticket_value = session.ticket();
    const auto menu_ticket = require_value(menu_ticket_value, "context change retains lifecycle");
    shot.arrived_at = now + 43ms;
    CHECK(!session.enqueue(menu_ticket, shot, shot.arrived_at));
    const auto pulses = shot_pulse_count(output);
    for (int offset = 60; offset <= 300; offset += 40) {
        const auto time = now + std::chrono::milliseconds{offset};
        REQUIRE(session.publish(menu_ticket, observation(time, Mode::menu), time));
        session.tick(time);
        require_neutral(output.bodies.back());
        shot.arrived_at = time;
        CHECK(!session.enqueue(menu_ticket, shot, time));
    }
    CHECK(output.body_clears == 0U);
    CHECK(shot_pulse_count(output) == pulses);

    session.control(Control::context_changed, now + 301ms);
    session.tick(now + 301ms);
    const auto resumed_ticket_value = session.ticket();
    const auto resumed_ticket = require_value(resumed_ticket_value, "menu exit gets a fresh epoch");
    REQUIRE(session.publish(resumed_ticket, observation(now + 320ms), now + 320ms));
    session.tick(now + 320ms);
    INFO("resuming gameplay does not replay the pre-menu shot");
    CHECK(shot_pulse_count(output) == pulses);
    require_neutral(output.bodies.back());
    shot.arrived_at = now + 340ms;
    REQUIRE(session.enqueue(resumed_ticket, shot, shot.arrived_at));
    session.tick(shot.arrived_at);
    CHECK(shot_pulse_count(output) == pulses + 1U);
}

TEST_CASE("fallout4.session.menu_suppresses_led_and_restores_only_a_fresh_live_window",
    "[component][fallout4][session][menu][led]") {
    const auto now = MonoTime{} + 15s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output);
    Session& session = *session_owner;
    const auto ticket = activate(session, now);

    auto baseline = observation(now + 40ms);
    baseline.led.subject_id = 0x14U;
    baseline.led.health = inputnext::adapters::LedHealthSample{.current = 80.0F, .maximum = 100.0F};
    REQUIRE(session.publish(ticket, baseline, baseline.sampled_at));
    session.tick(baseline.sampled_at);
    REQUIRE(output.leds.empty());

    auto changed = baseline;
    changed.sampled_at = now + 80ms;
    changed.led.health->current = 79.0F;
    REQUIRE(session.publish(ticket, changed, changed.sampled_at));
    session.tick(changed.sampled_at);
    REQUIRE(output.leds.size() == 1U);
    const auto led_submissions_before_menu = output.leds.size();

    const auto menu_ticket = session.ticket();
    const auto menu = require_value(menu_ticket, "menu observation keeps the active session");
    const auto menu_sample = observation(now + 120ms, Mode::menu);
    REQUIRE(session.publish(menu, menu_sample, menu_sample.sampled_at));
    session.tick(menu_sample.sampled_at);
    CHECK(output.led_clears > 0U);
    CHECK(output.leds.size() == led_submissions_before_menu);

    auto resume_sample = observation(now + 320ms);
    resume_sample.led.subject_id = 0x14U;
    resume_sample.led.health =
        inputnext::adapters::LedHealthSample{.current = 60.0F, .maximum = 100.0F};
    const auto resumed_ticket = session.ticket();
    const auto resumed = require_value(resumed_ticket, "fresh gameplay observation is admitted");
    REQUIRE(session.publish(resumed, resume_sample, resume_sample.sampled_at));
    session.tick(resume_sample.sampled_at);
    INFO("changed HP on resume is a fresh baseline while the old timed LED is still live");
    REQUIRE(output.leds.size() == led_submissions_before_menu + 1U);
    CHECK(output.leds.back().leds.lightbar_rgb != 0U);
    CHECK(output.leds.back().ttl_ms <= 200U);
}

TEST_CASE("fallout4.session.coalesced_menu_controls_preserve_only_the_original_led_deadline",
    "[component][fallout4][session][menu][led]") {
    for (const bool prior_window : std::array{false, true}) {
        CAPTURE(prior_window);
        const auto now = MonoTime{} + 18s;
        FakeOutput output;
        Config config;
        config.policy.effects.leds.health_hold_ms = 200U;
        const auto session_owner = std::make_unique<Session>(output, config);
        Session& session = *session_owner;
        auto ticket = activate(session, now);
        auto value = observation(now + 40ms);
        value.led.subject_id = 0x14U;
        value.led.health =
            inputnext::adapters::LedHealthSample{.current = 80.0F, .maximum = 100.0F};
        REQUIRE(session.publish(ticket, value, value.sampled_at));
        session.tick(value.sampled_at);
        value.sampled_at = now + 80ms;
        value.led.health =
            inputnext::adapters::LedHealthSample{.current = prior_window ? 79.0F : 80.0F,
                .maximum = 100.0F};
        REQUIRE(session.publish(ticket, value, value.sampled_at));
        session.tick(value.sampled_at);
        CHECK(output.leds.size() == (prior_window ? 1U : 0U));

        session.control(Control::context_changed, now + 100ms);
        session.control(Control::context_changed, now + 120ms);
        session.control(Control::context_changed, now + 110ms); // Delayed duplicate notification.
        const auto ticket_value = session.ticket();
        ticket = require_value(ticket_value, "coalesced controls retain a fresh epoch");
        value.sampled_at = now + 160ms;
        value.led.health =
            inputnext::adapters::LedHealthSample{.current = 60.0F, .maximum = 100.0F};
        REQUIRE(session.publish(ticket, value, value.sampled_at));
        session.tick(value.sampled_at);
        CHECK(output.leds.size() == (prior_window ? 2U : 0U));

        value.sampled_at = now + 260ms;
        REQUIRE(session.publish(ticket, value, value.sampled_at));
        session.tick(value.sampled_at);
        if (prior_window) {
            REQUIRE(!output.leds.empty());
            CHECK(output.leds.back().ttl_ms <= 20U);
        }
        const auto submissions = output.leds.size();
        value.sampled_at = now + 280ms;
        REQUIRE(session.publish(ticket, value, value.sampled_at));
        session.tick(value.sampled_at);
        INFO("the original 80+200 ms deadline expires; resume HP did not start or extend it");
        CHECK(output.leds.size() == submissions);
    }
}

TEST_CASE("fallout4.session.pending_ui_neutral_bridge_expires_without_a_new_sample",
    "[component][fallout4][session][menu]") {
    const auto now = MonoTime{} + 20s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output);
    Session& session = *session_owner;
    static_cast<void>(activate(session, now));
    session.control(Control::context_changed, now + 2ms);
    session.tick(now + 2ms);
    CHECK(output.body_clears == 0U);
    require_neutral(output.bodies.back());
    const auto submissions = output.bodies.size();
    session.tick(now + 152ms);
    INFO("an old sample cannot renew the neutral bridge beyond its freshness window");
    CHECK(output.body_clears == 1U);
    CHECK(output.bodies.size() == submissions);
    session.tick(now + 200ms);
    CHECK(output.body_clears == 1U);
    CHECK(output.bodies.size() == submissions);
}

TEST_CASE("fallout4.session.stale_menu_state_discards_the_paused_led_window",
    "[component][fallout4][session][menu][led]") {
    const auto now = MonoTime{} + 22s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output);
    Session& session = *session_owner;
    auto ticket = activate(session, now);
    auto value = observation(now + 20ms);
    value.led.subject_id = 0x14U;
    value.led.health = inputnext::adapters::LedHealthSample{.current = 80.0F, .maximum = 100.0F};
    REQUIRE(session.publish(ticket, value, value.sampled_at));
    session.tick(value.sampled_at);
    value.sampled_at = now + 40ms;
    value.led.health->current = 70.0F;
    REQUIRE(session.publish(ticket, value, value.sampled_at));
    session.tick(value.sampled_at);
    REQUIRE(output.leds.size() == 1U);

    session.control(Control::context_changed, now + 41ms);
    const auto ticket_value = session.ticket();
    ticket = require_value(ticket_value, "menu transition preserves lifecycle");
    REQUIRE(session.publish(ticket, observation(now + 60ms, Mode::menu), now + 60ms));
    session.tick(now + 60ms);
    session.tick(now + 211ms);

    value.sampled_at = now + 260ms;
    REQUIRE(session.publish(ticket, value, value.sampled_at));
    session.tick(value.sampled_at);
    INFO("a stale menu is a hard reset even while the old four-second deadline was still live");
    CHECK(output.leds.size() == 1U);
}

TEST_CASE("fallout4.session.transient_ui_capture_does_not_release_or_renew_menu_silence",
    "[component][fallout4][session][menu]") {
    for (const bool pending : std::array{false, true}) {
        CAPTURE(pending);
        const auto now = MonoTime{} + 25s;
        FakeOutput output;
        const auto session_owner = std::make_unique<Session>(output);
        Session& session = *session_owner;
        auto ticket = activate(session, now);
        REQUIRE(session.publish(ticket, observation(now + 40ms, Mode::menu), now + 40ms));
        session.tick(now + 40ms);
        if (pending) {
            session.control(Control::context_changed, now + 41ms);
            const auto ticket_value = session.ticket();
            ticket = require_value(ticket_value, "context bridge retains a lifecycle ticket");
        }
        for (const auto offset : std::array{60ms, 100ms, 140ms, 180ms}) {
            const auto time = now + offset;
            INFO("a transient capture failure keeps the previous fresh menu baseline");
            CHECK(!session.publish(ticket, observation(time, Mode::transition), time));
            session.tick(time);
            CHECK(output.body_clears == 0U);
            require_neutral(output.bodies.back());
        }
        const auto submissions = output.bodies.size();
        session.tick(now + 191ms);
        INFO("failed captures cannot renew freshness beyond the last valid menu sample");
        CHECK(output.body_clears == 1U);
        CHECK(output.bodies.size() == submissions);
    }
}

TEST_CASE("fallout4.session.neutral_menu_does_not_survive_hard_admission_loss",
    "[component][fallout4][session][menu]") {
    for (const auto loss : std::array{Control::focus_lost, Control::pre_load, Control::fault}) {
        CAPTURE(loss);
        const auto now = MonoTime{} + 30s;
        FakeOutput output;
        const auto session_owner = std::make_unique<Session>(output);
        Session& session = *session_owner;
        const auto ticket = activate(session, now);
        REQUIRE(session.publish(ticket, observation(now + 40ms, Mode::menu), now + 40ms));
        session.tick(now + 40ms);
        CHECK(output.body_clears == 0U);
        require_neutral(output.bodies.back());
        const auto submissions = output.bodies.size();
        session.control(loss, now + 41ms);
        session.tick(now + 41ms);
        CHECK(output.body_clears == 1U);
        CHECK(output.bodies.size() == submissions);
    }
}

TEST_CASE("fallout4.session.fresh_menu_sample_withdraws_on_death_or_staleness",
    "[component][fallout4][session][menu]") {
    const auto now = MonoTime{} + 40s;
    FakeOutput output;
    const auto session_owner = std::make_unique<Session>(output);
    Session& session = *session_owner;
    const auto ticket = activate(session, now);
    REQUIRE(session.publish(ticket, observation(now + 40ms, Mode::menu), now + 40ms));
    session.tick(now + 40ms);
    SECTION("death or unavailable player") {
        REQUIRE(session.publish(ticket, observation(now + 50ms, Mode::suspended), now + 50ms));
        session.tick(now + 50ms);
    }
    SECTION("stale menu sample") {
        session.tick(now + 191ms);
    }
    CHECK(output.body_clears == 1U);
}
} // namespace fallout4_adapter::session_tests
