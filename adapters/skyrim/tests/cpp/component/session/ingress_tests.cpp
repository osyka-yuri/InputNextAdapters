#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <input_next_effects.h>
#include <memory>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>
#include <span>

namespace skyrim_adapter::tests::session {

TEST_CASE("skyrim.session.player_identity_gates_admission", "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    auto ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto pulse = raw_event(EventKind::voice_fire, 0x14, fixture.time);
    INFO("event admitted before player identity was observed");
    REQUIRE(!fixture.coordinator.enqueue(ticket, pulse));

    auto unavailable = idle();
    unavailable.available = false;
    fixture.observe(unavailable);
    ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("event admitted from an unavailable player observation");
    REQUIRE(!fixture.coordinator.enqueue(ticket, pulse));

    fixture.settle(idle());
    fixture.coordinator.control(Control::focus_lost, fixture.time);
    fixture.coordinator.control(Control::focus_gained, fixture.time);
    ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("event admitted after focus invalidated the observation");
    REQUIRE(!fixture.coordinator.enqueue(ticket, pulse));

    fixture.advance(idle());
    ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("event rejected after a fresh playable observation");
    REQUIRE(
        fixture.coordinator.enqueue(ticket, raw_event(EventKind::voice_fire, 0x14, fixture.time)));
}

TEST_CASE("skyrim.session.footsteps_admit_only_fresh_current_player_or_mount",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(idle());

    auto on_foot = idle();
    on_foot.world = WorldObservation{};
    on_foot.world->gait = MovementGait::walking;
    fixture.advance(on_foot, 1ms);
    auto ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("fresh player step was not admitted on foot");
    REQUIRE(
        fixture.coordinator.enqueue(ticket, raw_event(EventKind::footstep, 0x14, fixture.time)));
    INFO("unrelated NPC step was admitted");
    REQUIRE(
        !fixture.coordinator.enqueue(ticket, raw_event(EventKind::footstep, 0x99, fixture.time)));
    INFO("stale callback crossed the 150 ms ingress limit");
    REQUIRE(!fixture.coordinator.enqueue(ticket,
        raw_event(EventKind::footstep, 0x14, fixture.time - 151ms)));

    fixture.time += 151ms;
    ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("stale player observation admitted a later callback");
    REQUIRE(
        !fixture.coordinator.enqueue(ticket, raw_event(EventKind::footstep, 0x14, fixture.time)));

    auto mounted = idle();
    mounted.world = WorldObservation{};
    mounted.world->mounted = true;
    mounted.world->mount_id = 0x89;
    mounted.world->gait = MovementGait::walking;
    mounted.world->mount_gait = MovementGait::running;
    fixture.advance(mounted, 1ms);
    ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("fresh current-mount step was not admitted");
    REQUIRE(
        fixture.coordinator.enqueue(ticket, raw_event(EventKind::footstep, 0x89, fixture.time)));
    INFO("rider step was admitted while mounted");
    REQUIRE(
        !fixture.coordinator.enqueue(ticket, raw_event(EventKind::footstep, 0x14, fixture.time)));
    INFO("step from a changed or unrelated mount was admitted");
    REQUIRE(
        !fixture.coordinator.enqueue(ticket, raw_event(EventKind::footstep, 0x88, fixture.time)));
    INFO("mount identity broadened attribution for non-footstep events");
    REQUIRE(
        !fixture.coordinator.enqueue(ticket, raw_event(EventKind::voice_fire, 0x89, fixture.time)));
    INFO("player combat event was suppressed while mounted");
    REQUIRE(
        fixture.coordinator.enqueue(ticket, raw_event(EventKind::voice_fire, 0x14, fixture.time)));

    const auto old_epoch_ticket = ticket;
    fixture.coordinator.control(Control::focus_lost, fixture.time);
    fixture.coordinator.control(Control::focus_gained, fixture.time);
    ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("stale-epoch mount step crossed focus invalidation");
    REQUIRE(!fixture.coordinator.enqueue(old_epoch_ticket,
        raw_event(EventKind::footstep, 0x89, fixture.time)));
    INFO("mount step crossed focus invalidation without a fresh sample");
    REQUIRE(
        !fixture.coordinator.enqueue(ticket, raw_event(EventKind::footstep, 0x89, fixture.time)));

    fixture.advance(mounted, 1ms);
    fixture.coordinator.control(Control::menu_suppressed, fixture.time);
    ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("mount step crossed menu suppression");
    REQUIRE(
        !fixture.coordinator.enqueue(ticket, raw_event(EventKind::footstep, 0x89, fixture.time)));
    fixture.coordinator.control(Control::menu_resumed, fixture.time);
    fixture.advance(mounted, 1ms);
    fixture.coordinator.control(Control::pre_load, fixture.time);
    ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("mount step crossed pre_load");
    REQUIRE(
        !fixture.coordinator.enqueue(ticket, raw_event(EventKind::footstep, 0x89, fixture.time)));

    fixture.coordinator.control(Control::load_succeeded, fixture.time);
    fixture.advance(mounted, 1ms);
    ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("mount step was not admitted after successful load");
    REQUIRE(
        fixture.coordinator.enqueue(ticket, raw_event(EventKind::footstep, 0x89, fixture.time)));
}

TEST_CASE("skyrim.session.npc_storm_preserves_player_pulse", "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 910);
    fixture.settle(idle());
    const auto ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("player pulse was not admitted");
    REQUIRE(
        fixture.coordinator.enqueue(ticket, raw_event(EventKind::voice_fire, 0x14, fixture.time)));

    constexpr std::array kinds{EventKind::weapon_swing, EventKind::equip, EventKind::hit};
    for (std::size_t i = 0; i < inbox_capacity; ++i) {
        const auto kind = kinds[i % kinds.size()];
        const auto npc = static_cast<FormId>(0x100 + i);
        const auto event_to_enqueue =
            kind == EventKind::hit
                ? raw_event(kind, Combatants{.actor = npc, .target = npc + 1}, fixture.time)
                : raw_event(kind, npc, fixture.time);
        INFO("NPC event was admitted to the player inbox");
        REQUIRE(!fixture.coordinator.enqueue(ticket, event_to_enqueue));
    }
    INFO("NPC events overflowed the player inbox");
    REQUIRE(fixture.coordinator.diagnostics().inbox_overflows == 0);

    const auto capture = fixture.capture();
    INFO("NPC storm displaced the queued player pulse");
    REQUIRE((capture.count == 1 && capture.events[0].actor_id == 0x14));
    Action pulse{};
    pulse.event = capture.events[0];
    fixture.coordinator.publish(capture, idle(), std::span{&pulse, 1}, fixture.time);
    fixture.coordinator.tick(fixture.time);
    INFO("player pulse did not survive an interleaved NPC storm");
    REQUIRE(fixture.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
}

TEST_CASE("skyrim.session.player_hits_and_changed_identity", "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(idle());
    auto ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto incoming =
        raw_event(EventKind::hit, Combatants{.actor = 0x99, .target = 0x14}, fixture.time);
    const auto outgoing =
        raw_event(EventKind::hit, Combatants{.actor = 0x14, .target = 0x99}, fixture.time);
    const auto unrelated =
        raw_event(EventKind::hit, Combatants{.actor = 0x99, .target = 0x98}, fixture.time);
    INFO("incoming player hit was rejected");
    REQUIRE(fixture.coordinator.enqueue(ticket, incoming));
    INFO("outgoing player hit was rejected");
    REQUIRE(fixture.coordinator.enqueue(ticket, outgoing));
    INFO("unrelated hit was admitted");
    REQUIRE(!fixture.coordinator.enqueue(ticket, unrelated));
    const auto hits = fixture.capture();
    INFO("incoming and outgoing player hits were not retained");
    REQUIRE(
        (hits.count == 2 && hits.events[0].target_id == 0x14 && hits.events[1].actor_id == 0x14));

    auto changed = idle();
    changed.player_id = 0x15;
    fixture.advance(changed);
    ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("event from the previous player identity was admitted");
    REQUIRE(!fixture.coordinator.enqueue(ticket,
        raw_event(EventKind::weapon_swing, 0x14, fixture.time)));
    INFO("event from the freshly observed player identity was rejected");
    REQUIRE(fixture.coordinator.enqueue(ticket,
        raw_event(EventKind::weapon_swing, 0x15, fixture.time)));
}

TEST_CASE("skyrim.session.publication_age_and_overflow", "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(idle());
    auto read = fixture.capture();
    fixture.coordinator.publish(read, casting(), {}, fixture.time + 151ms);
    fixture.coordinator.tick(fixture.time + 151ms);
    INFO("publication rejuvenated an old native read");
    REQUIRE(fixture.output.submissions.empty());
    fixture.time += 200ms;
    fixture.advance(idle());
    read = fixture.capture();
    const auto ticket = require_ticket(fixture.coordinator.native_ticket());
    for (std::size_t i = 0; i <= inbox_capacity; ++i) {
        const auto raw = raw_event(EventKind::voice_fire, 0x14, fixture.time);
        INFO("player overflow event did not request reconciliation");
        REQUIRE(fixture.coordinator.enqueue(ticket, raw));
    }
    Action action{};
    action.event.kind = EventKind::voice_fire;
    action.event.actor_id = 0x14;
    action.event.observed_at = fixture.time;
    fixture.coordinator.publish(read, idle(), std::span{&action, 1}, fixture.time);
    fixture.coordinator.tick(fixture.time);
    INFO("overflow not reported");
    REQUIRE(fixture.coordinator.diagnostics().inbox_overflows == 1);
    INFO("captured pulse survived overflow revision");
    REQUIRE(fixture.output.submissions.empty());
}

TEST_CASE("skyrim.session.stale_capture_publication_after_epoch_invalidation_is_rejected",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    INFO("baseline casting hold was not established");
    REQUIRE(!fixture.output.submissions.empty());
    const auto initial_submissions = fixture.output.submissions.size();
    fixture.time += 10ms;

    const auto old_ticket = require_ticket(fixture.coordinator.native_ticket());
    const auto old_capture = fixture.capture();
    INFO("captured ticket epoch matches coordinator epoch");
    REQUIRE(old_capture.ticket.epoch == old_ticket.epoch);

    Action stale_action = spell_release(Hand::left, fixture.time, 101, SpellKind::fire);

    fixture.coordinator.control(Control::focus_lost, fixture.time);
    fixture.coordinator.tick(fixture.time);
    INFO("focus loss withdrew active intents");
    REQUIRE(!fixture.output.clears.empty());
    INFO("focus loss withdrew body ownership");
    REQUIRE(!fixture.output.body_clears.empty());

    fixture.coordinator.publish(old_capture, casting(), std::span{&stale_action, 1}, fixture.time);
    fixture.coordinator.tick(fixture.time);
    INFO("stale capture published while blurred did not submit trigger");
    REQUIRE(fixture.output.submissions.size() == initial_submissions);

    fixture.coordinator.control(Control::focus_gained, fixture.time);
    fixture.time += 1ms;
    const auto fresh_ticket = require_ticket(fixture.coordinator.native_ticket());
    INFO("focus transitions incremented native ticket epoch");
    REQUIRE(fresh_ticket.epoch != old_ticket.epoch);

    const auto body_before = fixture.output.body_submissions.size();
    fixture.coordinator.publish(old_capture, casting(), std::span{&stale_action, 1}, fixture.time);
    fixture.coordinator.tick(fixture.time);
    INFO("stale capture publication after focus restoration emitted trigger output");
    REQUIRE(fixture.output.submissions.size() == initial_submissions);
    INFO("stale capture publication after focus restoration emitted body output");
    REQUIRE(fixture.output.body_submissions.size() == body_before);
    INFO("stale publication remained fault-free");
    REQUIRE(!fixture.coordinator.diagnostics().faulted);

    fixture.advance(casting(), 1ms);
    INFO("fresh observation in restored epoch successfully re-established effects");
    REQUIRE(fixture.output.submissions.size() > initial_submissions);
}

TEST_CASE("skyrim.session.ingress_overflow_clears_queue_bumps_revision_and_admits_fresh_events",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 911);
    fixture.settle(idle());
    fixture.time += 10ms;
    const auto ticket = require_ticket(fixture.coordinator.native_ticket());

    const auto pre_overflow_capture =
        require_capture(fixture.coordinator.begin_read(ticket, fixture.time));
    const auto initial_burst_revision = pre_overflow_capture.burst_revision;

    for (std::size_t i = 0; i < inbox_capacity; ++i) {
        const auto raw = raw_event(EventKind::voice_fire, 0x14, fixture.time);
        INFO("valid player event within capacity was rejected");
        REQUIRE(fixture.coordinator.enqueue(ticket, raw));
    }
    INFO("inbox overflow reported prematurely");
    REQUIRE(fixture.coordinator.diagnostics().inbox_overflows == 0);

    const auto raw_overflow = raw_event(EventKind::voice_fire, 0x14, fixture.time);
    const bool overflow_result = fixture.coordinator.enqueue(ticket, raw_overflow);
    INFO("65th enqueue did not signal urgent reconciliation");
    REQUIRE(overflow_result);
    INFO("inbox overflow counter did not increment");
    REQUIRE(fixture.coordinator.diagnostics().inbox_overflows == 1);

    const auto empty_capture =
        require_capture(fixture.coordinator.begin_read(ticket, fixture.time));
    INFO("overflow did not discard the accumulated inbox batch");
    REQUIRE(empty_capture.count == 0);
    INFO("burst revision was not incremented after overflow");
    REQUIRE(empty_capture.burst_revision > initial_burst_revision);
    INFO("burst revision did not increment by exactly one on overflow");
    REQUIRE(empty_capture.burst_revision == initial_burst_revision + 1);

    const auto body_before_stale_publish = fixture.output.body_submissions.size();
    Action discarded_action{};
    discarded_action.event = raw_event(EventKind::voice_fire, 0x14, fixture.time);
    fixture.coordinator.publish(pre_overflow_capture,
        idle(),
        std::span{&discarded_action, 1},
        fixture.time);
    fixture.coordinator.tick(fixture.time);
    INFO("pre-overflow capture publication was not rejected by updated burst revision");
    REQUIRE(fixture.output.body_submissions.size() == body_before_stale_publish);

    fixture.time += 1ms;
    const auto raw_fresh = raw_event(EventKind::voice_fire, 0x14, fixture.time);
    INFO("fresh event after overflow was rejected");
    REQUIRE(fixture.coordinator.enqueue(ticket, raw_fresh));

    const auto fresh_capture =
        require_capture(fixture.coordinator.begin_read(ticket, fixture.time));
    INFO("fresh read after overflow did not contain exactly one event");
    REQUIRE(fresh_capture.count == 1);
    INFO("fresh event did not match newly enqueued player signal");
    REQUIRE((fresh_capture.events[0].kind == EventKind::voice_fire &&
             fresh_capture.events[0].actor_id == 0x14));
    INFO("burst revision was not preserved across fresh event in same burst");
    REQUIRE(fresh_capture.burst_revision == empty_capture.burst_revision);
    INFO("fresh capture burst revision is not newer than pre-overflow revision");
    REQUIRE(fresh_capture.burst_revision > initial_burst_revision);

    const auto body_before_pulse = fixture.output.body_submissions.size();
    Action fresh_pulse{};
    fresh_pulse.event = fresh_capture.events[0];
    fixture.coordinator.publish(fresh_capture, idle(), std::span{&fresh_pulse, 1}, fixture.time);
    fixture.coordinator.tick(fixture.time);

    INFO("body submissions container is empty after pulse tick");
    REQUIRE(!fixture.output.body_submissions.empty());
    INFO("fresh post-overflow event failed to submit new body output");
    REQUIRE(fixture.output.body_submissions.size() > body_before_pulse);
    INFO("fresh post-overflow event failed to emit body pulse");
    REQUIRE(fixture.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
}

TEST_CASE("skyrim.session.menu_resumption_enforces_strict_observation_sampling_boundary",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    advertise_hd(fixture.output, 912);
    fixture.settle(casting());
    INFO("baseline casting hold was not established");
    REQUIRE(!fixture.output.submissions.empty());
    const auto submissions_before_menu = fixture.output.submissions.size();
    const auto body_submissions_before_menu = fixture.output.body_submissions.size();

    const auto t_menu = fixture.time + 10ms;
    fixture.coordinator.control(Control::menu_suppressed, t_menu);
    fixture.coordinator.tick(t_menu);
    INFO("menu suppression did not withdraw triggers");
    REQUIRE(!fixture.output.clears.empty());

    const auto t_resume = t_menu + 100ms;
    fixture.coordinator.control(Control::menu_resumed, t_resume);
    const auto resume_ticket = require_ticket(fixture.coordinator.native_ticket());

    const auto at_boundary_capture =
        require_capture(fixture.coordinator.begin_read(resume_ticket, t_resume));
    INFO("capture read_started_at matches resume tick time");
    REQUIRE(at_boundary_capture.read_started_at == t_resume);

    const auto submissions_before_boundary = fixture.output.submissions.size();
    const auto body_submissions_before_boundary = fixture.output.body_submissions.size();
    INFO("triggers remained unchanged while menu suppressed");
    REQUIRE(submissions_before_boundary == submissions_before_menu);
    INFO("body submissions were preserved");
    REQUIRE(body_submissions_before_boundary >= body_submissions_before_menu);

    fixture.coordinator.publish(at_boundary_capture, casting(), {}, t_resume);
    fixture.coordinator.tick(t_resume);

    INFO("observation sampled exactly at gameplay_resumed_at boundary emitted gameplay triggers");
    REQUIRE(fixture.output.submissions.size() == submissions_before_boundary);
    for (std::size_t i = body_submissions_before_boundary;
        i < fixture.output.body_submissions.size();
        ++i) {
        INFO("boundary observation produced an unexpected PCM replace request");
        REQUIRE(fixture.output.body_submissions[i].request.pcm_command !=
                INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
    }

    const auto submissions_before_fresh = fixture.output.submissions.size();
    const auto body_submissions_before_fresh = fixture.output.body_submissions.size();

    const auto t_fresh = t_resume + 1ms;
    const auto fresh_capture =
        require_capture(fixture.coordinator.begin_read(resume_ticket, t_fresh));
    INFO("epoch is preserved across suspended boundary ticks");
    REQUIRE(fresh_capture.ticket.epoch == resume_ticket.epoch);
    fixture.coordinator.publish(fresh_capture, casting(), {}, t_fresh);
    fixture.coordinator.tick(t_fresh);

    INFO("observation sampled strictly after gameplay_resumed_at failed to resume triggers");
    REQUIRE((fixture.output.submissions.size() > submissions_before_fresh &&
             fixture.output.submissions.size() > submissions_before_menu));
    INFO("observation sampled strictly after gameplay_resumed_at failed to submit body output");
    REQUIRE(fixture.output.body_submissions.size() > body_submissions_before_fresh);
    INFO("body submissions container is empty");
    REQUIRE(!fixture.output.body_submissions.empty());
    INFO("observation sampled strictly after gameplay_resumed_at failed to resume casting PCM");
    REQUIRE(fixture.output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
}

} // namespace skyrim_adapter::tests::session
