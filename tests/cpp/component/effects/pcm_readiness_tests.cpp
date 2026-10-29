#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "output_session/fake_output.hpp"
#include "output_session/support.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>

namespace {
using namespace std::chrono_literals;
using namespace inputnext::adapters;
using namespace inputnext::test::output_session;

TEST_CASE("effects.output_session.classic_pcm_readiness", "[component][effects][output_session]") {
    struct CapabilityShape {
        bool hd_ready{};
        std::uint64_t audio_instance_id{};
    };

    constexpr std::array shapes{
        CapabilityShape{},
        CapabilityShape{.hd_ready = true, .audio_instance_id = 0},
        CapabilityShape{.hd_ready = false, .audio_instance_id = 902},
    };

    for (const auto shape : shapes) {
        FakeOutput output;
        if (shape.hd_ready) {
            output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
        }
        output.capabilities.audio_instance_id = shape.audio_instance_id;
        FakeObserver observer;
        const auto session = std::make_unique<OutputSession>(output, observer, 0);
        TickClock clock{.supplied = MonoTime{} + 95s, .use_steady_clock = false};
        bind_with_fresh_sample(*session, observer, clock);

        auto active = body_frame(clock.now(), 510, 2s);
        active.rumble = RumbleIntent{
            .payload = {.low_frequency = 0.4F, .high_frequency = 0.2F},
            .ttl_ms = 80,
        };
        INFO("classic-only body fallback send failed");
        REQUIRE(session->send(active, clock.now(), clock));
        INFO("classic-only body ownership rendered PCM or lost its finite fallback");
        REQUIRE((output.body_calls.size() == 1 && session->has_body_owner() &&
                 output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
                 output.body_calls.back().samples.empty() &&
                 output.body_calls.back().request.audio_instance_id == 0 &&
                 output.body_calls.back().request.sequence == 1 &&
                 output.body_calls.back().request.ttl_ms == 200 &&
                 output.body_calls.back().request.fallback_rumble.low_frequency == 0.4F &&
                 output.body_calls.back().request.fallback_rumble.high_frequency == 0.2F));
        const auto accepted_lease = output.next_lease - 1;

        clock.supplied += 50ms;
        const auto changed_program = body_frame(clock.now(), 511, 2s);
        INFO("classic fallback withdrawal failed");
        REQUIRE(session->send(changed_program, clock.now(), clock));
        INFO("an unrenderable program change disturbed the classic lease or fallback withdrawal");
        REQUIRE((output.body_calls.size() == 2 &&
                 output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
                 output.body_calls.back().samples.empty() &&
                 output.body_calls.back().request.sequence == 2 &&
                 output.body_calls.back().request.fallback_rumble.low_frequency == 0.0F &&
                 output.body_calls.back().request.fallback_rumble.high_frequency == 0.0F));

        clock.supplied += 100ms;
        INFO("classic silent lease refresh failed");
        REQUIRE(session->send(changed_program, clock.now(), clock));
        INFO("classic-only silent ownership did not refresh on the accepted lease sequence");
        REQUIRE((output.body_calls.size() == 3 &&
                 output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
                 output.body_calls.back().samples.empty() &&
                 output.body_calls.back().request.sequence == 3 &&
                 output.body_calls.back().request.lease_id == accepted_lease));
        INFO("classic-only lease clear failed");
        REQUIRE(session->clear_body(clock));
        INFO("classic-only body clear did not address its exact owner and next sequence");
        REQUIRE((output.body_clears.size() == 1 && output.body_clears.back().sequence == 4 &&
                 output.body_clears.back().lease_id == accepted_lease &&
                 output.body_clears.back().audio_instance_id == 0));
    }
}

TEST_CASE("effects.output_session.unrenderable_body_retry",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 96s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);
    INFO("initial silent owner setup failed");
    REQUIRE(session->send(OutputFrame{}, clock.now(), clock));
    INFO("initial silent owner was not accepted");
    REQUIRE((output.body_calls.size() == 1 && session->has_body_owner()));

    output.body_status = INPUT_NEXT_STATUS_BUSY;
    clock.supplied += 10ms;
    auto failed_frame = body_frame(clock.now(), 520, 2s);
    failed_frame.rumble = RumbleIntent{
        .payload = {.low_frequency = 0.4F, .high_frequency = 0.2F},
        .ttl_ms = 200,
    };
    INFO("BUSY fallback update was reported as accepted");
    REQUIRE_FALSE(session->send(failed_frame, clock.now(), clock));
    INFO("classic-only failed fallback update unexpectedly submitted PCM");
    REQUIRE((output.body_calls.size() == 2 &&
             output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             output.body_calls.back().samples.empty() &&
             output.body_calls.back().request.sequence == 2));

    clock.supplied += 40ms;
    const auto calls_after_failure = output.body_calls.size();
    auto changed_program = body_frame(clock.now(), 521, 2s);
    output.body_status = INPUT_NEXT_STATUS_OK;
    INFO("cooldown skip was not treated as a completed send");
    REQUIRE(session->send(changed_program, clock.now(), clock));
    INFO("an unrenderable body program change bypassed the failed-request cooldown");
    REQUIRE(output.body_calls.size() == calls_after_failure);

    changed_program = body_frame(clock.now(), 522, 2s);
    changed_program.rumble = RumbleIntent{
        .payload = {.low_frequency = 0.5F, .high_frequency = 0.3F},
        .ttl_ms = 200,
    };
    INFO("changed fallback did not retain retry policy");
    REQUIRE(session->send(changed_program, clock.now(), clock));
    INFO("a genuine fallback update did not retry independently of classic PCM readiness");
    REQUIRE((output.body_calls.size() == calls_after_failure + 1 &&
             output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             output.body_calls.back().samples.empty() &&
             output.body_calls.back().request.sequence == 2 &&
             output.body_calls.back().request.fallback_rumble.low_frequency == 0.5F &&
             output.body_calls.back().request.fallback_rumble.high_frequency == 0.3F));
}

TEST_CASE("effects.output_session.advertised_classic_pcm_recovery",
    "[component][effects][output_session]") {
    FakeOutput output;
    advertise_hd(output, 903);
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 97s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    const auto active = body_frame(clock.now(), 530, 500ms);
    INFO("advertised-HD classic-route body send failed");
    REQUIRE(session->send(active, clock.now(), clock));
    INFO("an advertised-HD CLASSIC receipt did not retain the PCM attempt and route fallback");
    REQUIRE((
        output.body_calls.size() == 1 &&
        output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
        !output.body_calls.back().samples.empty() &&
        session->diagnostics().accepted_body_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC &&
        session->diagnostics().body_fallback_reason == INPUT_NEXT_EFFECTS_BODY_FALLBACK_NOT_READY));

    clock.supplied += 100ms;
    INFO("classic-route PCM cleanup was not accepted");
    REQUIRE(session->send(OutputFrame{}, clock.now(), clock));
    INFO("advertised-HD CLASSIC delivery did not preserve its accepted PCM stop policy");
    REQUIRE((output.body_calls.size() == 2 &&
             output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP &&
             output.body_calls.back().request.sequence == 2));
}

TEST_CASE("effects.output_session.hd_readiness_transitions",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 98s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    const auto short_sampled_at = clock.now();
    const auto short_transient = body_frame(short_sampled_at, 540, 50ms);
    INFO("classic transient baseline failed");
    REQUIRE(session->send(short_transient, clock.now(), clock));
    const auto classic_owner = output.body_calls.back().request;
    const auto classic_lease = output.next_lease - 1;
    INFO("classic transient baseline unexpectedly submitted PCM");
    REQUIRE((classic_owner.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             output.body_calls.back().samples.empty()));

    clock.supplied += 60ms;
    advertise_hd(output, 904);
    INFO("HD gain lost the current target binding");
    REQUIRE(session->reconcile(clock.now() - 1ms, clock));
    INFO("HD gain did not clear the exact classic owner and establish a fresh body baseline");
    REQUIRE(
        (observer.body_rebound_count == 1 && !observer.ready && output.body_clears.size() == 1 &&
            output.body_clears.back().lease_id == classic_lease &&
            output.body_clears.back().sequence == classic_owner.sequence + 1 &&
            output.body_clears.back().audio_instance_id == 0 &&
            same_target(output.body_clears.back().target, classic_owner.target)));
    clock.supplied += 1ms;
    observer.publish(clock.now());
    INFO("fresh observation after HD gain was rejected");
    REQUIRE(session->reconcile(clock.now(), clock));
    INFO("expired transient was not skipped");
    REQUIRE(session->send(short_transient, short_sampled_at, clock));
    INFO("HD gain replayed a transient after its original cue had expired");
    REQUIRE((output.body_calls.size() == 2 &&
             output.body_calls.back().request.audio_instance_id == 904 &&
             output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             output.body_calls.back().samples.empty()));

    const auto hd_sampled_at = clock.now();
    const auto live_hd_transient = body_frame(hd_sampled_at, 541, 500ms);
    INFO("HD transient setup failed");
    REQUIRE(session->send(live_hd_transient, clock.now(), clock));
    const auto hd_owner = output.body_calls.back().request;
    INFO("HD-ready body program was not submitted as PCM");
    REQUIRE((hd_owner.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             !output.body_calls.back().samples.empty()));

    clock.supplied += 1ms;
    output.capabilities.feature_flags &= ~INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 0;
    INFO("HD loss lost the current target binding");
    REQUIRE(session->reconcile(clock.now() - 1ms, clock));
    INFO("HD loss did not clear the exact old audio owner and establish a fresh body baseline");
    REQUIRE(
        (observer.body_rebound_count == 2 && !observer.ready && output.body_clears.size() == 2 &&
            output.body_clears.back().lease_id == hd_owner.lease_id &&
            output.body_clears.back().sequence == hd_owner.sequence + 1 &&
            output.body_clears.back().audio_instance_id == 904 &&
            same_target(output.body_clears.back().target, hd_owner.target)));
    clock.supplied += 1ms;
    observer.publish(clock.now());
    INFO("fresh observation after HD loss was rejected");
    REQUIRE(session->reconcile(clock.now(), clock));
    INFO("classic recovery send failed");
    REQUIRE(session->send(live_hd_transient, hd_sampled_at, clock));
    INFO("HD loss left PCM enabled for a classic-only body owner");
    REQUIRE(
        (output.body_calls.size() == 4 && output.body_calls.back().request.audio_instance_id == 0 &&
            output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
            output.body_calls.back().samples.empty()));
}

} // namespace
