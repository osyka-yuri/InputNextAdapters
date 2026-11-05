#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <input_next_core.h>
#include <input_next_effects.h>
#include <memory>
#include <skyrim_adapter/body_waveform.hpp>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>
#include <vector>

namespace skyrim_adapter::tests::session {

TEST_CASE("skyrim.session.capabilities_and_leases", "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.output.capabilities.feature_flags =
        INPUT_NEXT_EFFECTS_CAP_HIGH_RUMBLE | INPUT_NEXT_EFFECTS_CAP_BODY_LEASE |
        INPUT_NEXT_EFFECTS_CAP_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_CAP_RIGHT_TRIGGER;
    fixture.settle(casting());
    INFO("supported body lease was discarded");
    REQUIRE(!fixture.output.body_submissions.empty());
    for (const auto& request : fixture.output.submissions) {
        INFO("body fallback leaked into the discrete rumble channel");
        REQUIRE((request.channel_mask & INPUT_NEXT_EFFECTS_RUMBLE) == 0);
        INFO("unbounded lease");
        REQUIRE((request.ttl_ms > 0 && request.ttl_ms <= hold_ttl_ms));
        INFO("malformed SDK request");
        REQUIRE((
            request.reserved == 0 && request.struct_size == sizeof(input_next_effects_request_v1)));
    }
    INFO("unsupported low rumble motor entered the body fallback");
    REQUIRE(fixture.output.body_submissions.back().request.fallback_rumble.low_frequency == 0.0F);
    INFO("body lease TTL did not use the finite 200ms limit");
    REQUIRE(fixture.output.body_submissions.back().request.ttl_ms == hold_ttl_ms);
    fixture.output.capabilities.supported_channels = 0;
    fixture.advance(casting());
    INFO("capability removal did not withdraw trigger channels");
    REQUIRE(!fixture.output.clears.empty());
    INFO("capability removal did not clear body ownership");
    REQUIRE(!fixture.output.body_clears.empty());
}

TEST_CASE("skyrim.session.replacement_and_error", "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    const auto old_ticket = require_ticket(fixture.coordinator.native_ticket());
    auto old_event = RawEvent{};
    old_event.kind = EventKind::voice_fire;
    old_event.actor_id = 0x14;
    old_event.observed_at = fixture.time;
    const auto count = fixture.output.submissions.size();
    const auto prior_target = fixture.output.target;
    fixture.output.target.connection_id = 23;
    fixture.coordinator.tick(fixture.time + 1ms);
    INFO("old snapshot affected replacement controller");
    REQUIRE(fixture.output.submissions.size() == count);
    INFO("pre-binding callback pulse entered replacement controller");
    REQUIRE(!fixture.coordinator.enqueue(old_ticket, old_event));
    fixture.time += 50ms;
    const auto read = fixture.capture();
    INFO("pre-binding callback pulse entered replacement controller");
    REQUIRE(read.count == 0);
    fixture.coordinator.publish(read, casting(), {}, fixture.time);
    fixture.coordinator.tick(fixture.time);
    INFO("held state could not bind the replacement connection");
    REQUIRE(same_device_target(fixture.output.submissions.back().target, fixture.output.target));
    fixture.output.submit_status = INPUT_NEXT_STATUS_STALE_TARGET;
    fixture.advance(casting(), 110ms);
    INFO("submit error not diagnosed");
    REQUIRE(fixture.coordinator.diagnostics().last_status == INPUT_NEXT_STATUS_STALE_TARGET);
    const auto failed_count = fixture.output.submissions.size();
    fixture.coordinator.tick(fixture.time + 1ms);
    INFO("rejected intent retried without fresh snapshot");
    REQUIRE(fixture.output.submissions.size() == failed_count);
    for (const auto& clear : fixture.output.clears) {
        INFO("clear was retargeted from the original connection");
        REQUIRE(same_device_target(clear.target, prior_target));
    }
}

TEST_CASE("skyrim.session.body_route_and_audio_instance_rebind", "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    fixture.output.capabilities.audio_instance_id = 71;
    fixture.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    fixture.settle(idle());
    INFO("ready target did not acquire neutral body ownership");
    REQUIRE(fixture.output.body_submissions.size() == 1);
    const auto& neutral = fixture.output.body_submissions.front().request;
    INFO("initial body acquisition did not bind the advertised ready instance");
    REQUIRE((neutral.lease_id == 0 && neutral.sequence == 1 && neutral.audio_instance_id == 71));
    INFO("new body ownership did not hold finite intentional silence");
    REQUIRE((neutral.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP && neutral.ttl_ms == 200));
    INFO("valid body receipt was not accepted");
    REQUIRE(fixture.output.active_body_lease != 0);

    fixture.advance(casting());
    INFO("held cue did not replace neutral PCM");
    REQUIRE(fixture.output.body_submissions.size() == 2);
    const auto& held = fixture.output.body_submissions.back();
    INFO("held cue renewal did not use its exact owner and increasing sequence");
    REQUIRE((held.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             held.request.lease_id == fixture.output.active_body_lease &&
             held.request.sequence == 2 && held.request.audio_instance_id == 71));
    INFO("REPLACE did not carry a complete bounded 48kHz stereo window");
    REQUIRE(
        (held.request.pcm.frame_count == body_pcm_max_frames && held.request.pcm.channels == 2 &&
            held.request.pcm.sample_rate == body_pcm_sample_rate && held.samples.size() == 19'200));
    for (const auto& request : fixture.output.submissions) {
        INFO("trigger output interfered with body ownership through discrete rumble");
        REQUIRE((request.channel_mask & INPUT_NEXT_EFFECTS_RUMBLE) == 0);
    }

    const auto trigger_count = fixture.output.submissions.size();
    const auto prior_lease_id = fixture.output.active_body_lease;
    fixture.output.capabilities.audio_instance_id = 72;
    fixture.coordinator.tick(fixture.time);
    INFO("instance replacement did not clear the old body lease");
    REQUIRE(!fixture.output.body_clears.empty());
    const auto& clear = fixture.output.body_clears.back();
    INFO("audio rebind did not withdraw the exact prior body owner");
    REQUIRE(
        (same_device_target(clear.target, fixture.output.target) && clear.audio_instance_id == 71 &&
            clear.lease_id == prior_lease_id && clear.sequence == 3));
    INFO("audio rebind adopted the replacement instance before a fresh body baseline");
    REQUIRE(fixture.output.body_submissions.size() == 2);
    INFO("audio rebind reset independently valid trigger state");
    REQUIRE(fixture.output.submissions.size() == trigger_count);

    fixture.advance(casting());
    INFO("fresh observation did not acquire the replacement instance");
    REQUIRE(fixture.output.body_submissions.size() == 3);
    const auto& rebound = fixture.output.body_submissions.back().request;
    INFO("replacement audio instance did not receive a fresh held baseline");
    REQUIRE((rebound.audio_instance_id == 72 && rebound.lease_id == 0 && rebound.sequence == 1 &&
             rebound.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE));
    INFO("body-only rebind regressed the trigger channel cache");
    REQUIRE(fixture.output.submissions.size() == trigger_count);
}

TEST_CASE("skyrim.session.body_instance_zero_cannot_adopt_readiness",
    "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(idle());
    INFO("classic-only target unexpectedly advertised an HD instance");
    REQUIRE(fixture.output.body_submissions.front().request.audio_instance_id == 0);
    fixture.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    fixture.output.capabilities.audio_instance_id = 91;
    fixture.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    fixture.coordinator.tick(fixture.time);
    INFO("zero-bound body owner silently adopted newly ready audio");
    REQUIRE(fixture.output.body_submissions.size() == 1);
    INFO("zero-instance owner was not cleared with its exact original identity");
    REQUIRE((!fixture.output.body_clears.empty() &&
             fixture.output.body_clears.back().audio_instance_id == 0));
    fixture.advance(idle());
    INFO("fresh baseline did not acquire the newly ready audio instance");
    REQUIRE((fixture.output.body_submissions.size() == 2 &&
             fixture.output.body_submissions.back().request.audio_instance_id == 91 &&
             fixture.output.body_submissions.back().request.lease_id == 0));
}

} // namespace skyrim_adapter::tests::session
