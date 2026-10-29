#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "output_session/fake_output.hpp"
#include "output_session/support.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <chrono>
#include <cstdint>
#include <memory>

namespace {
using namespace std::chrono_literals;
using namespace inputnext::adapters;
using namespace inputnext::test::output_session;

TEST_CASE("effects.output_session.body_routes_sequence_receipts",
    "[component][effects][output_session]") {
    FakeOutput classic_output;
    FakeObserver classic_observer;
    const auto classic_session =
        std::make_unique<OutputSession>(classic_output, classic_observer, 0);
    TickClock classic_clock{.supplied = MonoTime{} + 40s, .use_steady_clock = false};
    bind_with_fresh_sample(*classic_session, classic_observer, classic_clock);

    OutputFrame transient = body_frame(classic_clock.now(), 41, 100ms);
    transient.rumble = RumbleIntent{
        .payload = {.low_frequency = 0.4F, .high_frequency = 0.2F},
        .ttl_ms = 80,
    };
    INFO("classic fallback send failed");
    REQUIRE(classic_session->send(transient, classic_clock.now(), classic_clock));
    INFO("classic-only capabilities submitted PCM for an active body program");
    REQUIRE((
        classic_output.body_calls.size() == 1 &&
        classic_output.body_calls.front().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
        classic_output.body_calls.front().samples.empty()));
    INFO("classic receipt did not preserve its fallback reason");
    REQUIRE((classic_session->diagnostics().accepted_body_route ==
                 INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC &&
             classic_session->diagnostics().body_fallback_reason ==
                 INPUT_NEXT_EFFECTS_BODY_FALLBACK_NOT_READY));
    INFO("body request lost its accepted rumble fallback");
    REQUIRE(classic_output.body_calls.front().request.fallback_rumble.low_frequency == 0.4F);

    classic_clock.supplied += 100ms;
    INFO("body stop failed");
    REQUIRE(classic_session->send(OutputFrame(), classic_clock.now(), classic_clock));
    INFO("classic-only body lease did not renew through its accepted sequence");
    REQUIRE((
        classic_output.body_calls.size() == 2 &&
        classic_output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
        classic_output.body_calls.back().request.sequence == 2));
    INFO("accepted body lease clear failed");
    REQUIRE(classic_session->clear_body(classic_clock));
    INFO("body clear did not continue the accepted lease sequence");
    REQUIRE((
        classic_output.body_clears.size() == 1 && classic_output.body_clears.back().sequence == 3));

    FakeOutput hd_output;
    advertise_hd(hd_output, 920);
    hd_output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    FakeObserver hd_observer;
    const auto hd_session = std::make_unique<OutputSession>(hd_output, hd_observer, 2);
    TickClock hd_clock{.supplied = MonoTime{} + 50s, .use_steady_clock = false};
    bind_with_fresh_sample(*hd_session, hd_observer, hd_clock);
    INFO("HD body send failed");
    REQUIRE(hd_session->send(body_frame(hd_clock.now(), 42), hd_clock.now(), hd_clock));
    INFO("HD route receipt did not bind the request to the advertised audio instance");
    REQUIRE((
        hd_output.body_calls.size() == 1 &&
        hd_output.body_calls.back().request.audio_instance_id == 920 &&
        hd_output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
        !hd_output.body_calls.back().samples.empty() &&
        hd_output.body_calls.back().request.route_preference ==
            INPUT_NEXT_EFFECTS_BODY_ROUTE_PREFER_HD &&
        hd_session->diagnostics().accepted_body_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD &&
        hd_session->diagnostics().body_fallback_reason == INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE));
}

TEST_CASE("effects.output_session.failed_transient_body_retry",
    "[component][effects][output_session]") {
    FakeOutput malformed_output;
    advertise_hd(malformed_output, 921);
    malformed_output.receipt_fault = ReceiptFault::size;
    FakeObserver malformed_observer;
    const auto malformed_session =
        std::make_unique<OutputSession>(malformed_output, malformed_observer, 3);
    TickClock malformed_clock{.supplied = MonoTime{} + 60s, .use_steady_clock = false};
    bind_with_fresh_sample(*malformed_session, malformed_observer, malformed_clock);
    const auto malformed_sample = malformed_clock.now();
    const auto malformed_frame = body_frame(malformed_sample, 43, 400ms);
    INFO("malformed body receipt was accepted");
    REQUIRE_FALSE(malformed_session->send(malformed_frame, malformed_sample, malformed_clock));
    INFO("malformed body receipt acquired a lease");
    REQUIRE_FALSE(malformed_session->has_body_owner());
    INFO("malformed body receipt lost its validation diagnostic");
    REQUIRE(malformed_session->diagnostics().last_status == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    malformed_clock.supplied += 50ms;
    INFO("malformed receipt allowed another mutation through its revoked context");
    REQUIRE_FALSE(malformed_session->send(malformed_frame, malformed_sample, malformed_clock));
    INFO("malformed receipt was cleared or not settled by exact context close");
    REQUIRE((malformed_output.body_calls.size() == 1 && malformed_output.body_clears.empty() &&
             malformed_session->close_and_drain(malformed_clock) &&
             malformed_output.ack_close_calls == 1));

    FakeOutput busy_output;
    advertise_hd(busy_output, 924);
    busy_output.body_status = INPUT_NEXT_STATUS_BUSY;
    FakeObserver busy_observer;
    const auto busy_session = std::make_unique<OutputSession>(busy_output, busy_observer, 3);
    TickClock busy_clock{.supplied = MonoTime{} + 61s, .use_steady_clock = false};
    bind_with_fresh_sample(*busy_session, busy_observer, busy_clock);
    const auto busy_sample = busy_clock.now();
    const auto busy_frame = body_frame(busy_sample, 46, 400ms);
    INFO("BUSY body attempt was reported as accepted");
    REQUIRE_FALSE(busy_session->send(busy_frame, busy_sample, busy_clock));
    busy_output.body_status = INPUT_NEXT_STATUS_OK;
    busy_clock.supplied += 50ms;
    INFO("body retry cooldown path failed");
    REQUIRE(busy_session->send(busy_frame, busy_sample, busy_clock));
    INFO("failed body request retried inside its bounded cooldown");
    REQUIRE(busy_output.body_calls.size() == 1);
    busy_clock.supplied += 50ms;
    busy_observer.publish(busy_clock.now());
    INFO("body retry after cooldown failed");
    REQUIRE(busy_session->send(busy_frame, busy_clock.now(), busy_clock));
    INFO("transient BUSY retry replayed PCM or advanced an unaccepted lease sequence");
    REQUIRE(
        (busy_output.body_calls.size() == 2 &&
            busy_output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
            busy_output.body_calls.back().request.sequence == 1 && busy_session->has_body_owner()));
}

TEST_CASE("effects.output_session.fallback_expiry_after_pcm_preparation",
    "[component][effects][output_session]") {
    const auto verify_fallback_after_pcm_preparation = [](std::chrono::milliseconds elapsed,
                                                           float expected_low_frequency,
                                                           float expected_high_frequency) {
        FakeOutput output;
        advertise_hd(output, 922);
        FakeObserver observer;
        const auto session = std::make_unique<OutputSession>(output, observer, 0);
        TickClock clock{.supplied = MonoTime{} + 70s, .use_steady_clock = false};
        bind_with_fresh_sample(*session, observer, clock);

        constexpr std::uint32_t fallback_ttl_ms = 100;
        auto frame = body_frame(clock.now(), 44, 2s);
        frame.rumble = RumbleIntent{
            .payload = {.low_frequency = 0.6F, .high_frequency = 0.3F},
            .ttl_ms = fallback_ttl_ms,
        };
        frame.left = trigger(200);
        const auto sampled_at = clock.now();

        // send() and body planning each check restriction before submit_body prepares PCM.
        // This checkpoint runs on submit_body's next restriction query, after PCM preparation;
        // advancing supplied time models elapsed preparation time, not a measured render duration.
        const auto preparation_query = observer.restriction_queries + 3;
        bool preparation_checkpoint_reached = false;
        observer.restriction_checkpoint = [&](std::uint32_t query) {
            if (query == preparation_query) {
                clock.supplied += elapsed;
                preparation_checkpoint_reached = true;
            }
        };

        INFO("fallback body send failed");
        REQUIRE(session->send(frame, sampled_at, clock));
        INFO("body PCM preparation checkpoint was not reached");
        REQUIRE(preparation_checkpoint_reached);
        INFO("fallback expiry caused an immediate compensating body submission");
        REQUIRE(output.body_calls.size() == 1);
        const auto& request = output.body_calls.back().request;
        INFO("fallback rumble did not match the sampled TTL at body submission");
        REQUIRE((request.fallback_rumble.low_frequency == expected_low_frequency &&
                 request.fallback_rumble.high_frequency == expected_high_frequency));
        INFO("fallback expiry removed the active PCM replacement");
        REQUIRE((request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
                 !output.body_calls.back().samples.empty()));
        INFO("fallback expiry suppressed the still-valid trigger");
        REQUIRE((output.triggers.size() == 1 &&
                 output.triggers.back().left_trigger.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK));
    };

    verify_fallback_after_pcm_preparation(99ms, 0.6F, 0.3F);
    verify_fallback_after_pcm_preparation(100ms, 0.0F, 0.0F);
    verify_fallback_after_pcm_preparation(101ms, 0.0F, 0.0F);

    FakeOutput retry_output;
    advertise_hd(retry_output, 923);
    FakeObserver retry_observer;
    const auto retry_session = std::make_unique<OutputSession>(retry_output, retry_observer, 0);
    TickClock retry_clock{.supplied = MonoTime{} + 80s, .use_steady_clock = false};
    bind_with_fresh_sample(*retry_session, retry_observer, retry_clock);
    auto accepted_frame = body_frame(retry_clock.now(), 45, 2s);
    accepted_frame.rumble = RumbleIntent{
        .payload = {.low_frequency = 0.1F, .high_frequency = 0.2F},
        .ttl_ms = 150,
    };
    const auto accepted_sampled_at = retry_clock.now();
    INFO("initial body owner setup failed");
    REQUIRE(retry_session->send(accepted_frame, accepted_sampled_at, retry_clock));
    INFO("initial body owner was not accepted");
    REQUIRE((retry_output.body_calls.size() == 1 && retry_session->has_body_owner()));
    const auto retry_sequence = retry_output.body_calls.back().request.sequence + 1;

    retry_output.body_status = INPUT_NEXT_STATUS_BUSY;
    auto stale_frame = accepted_frame;
    stale_frame.rumble = RumbleIntent{
        .payload = {.low_frequency = 0.7F, .high_frequency = 0.4F},
        .ttl_ms = 100,
    };
    const auto stale_sampled_at = retry_clock.now();
    const auto preparation_query = retry_observer.restriction_queries + 3;
    bool preparation_checkpoint_reached = false;
    retry_observer.restriction_checkpoint = [&](std::uint32_t query) {
        if (query == preparation_query) {
            retry_clock.supplied += 100ms;
            preparation_checkpoint_reached = true;
        }
    };

    INFO("failed body submission unexpectedly succeeded");
    REQUIRE_FALSE(retry_session->send(stale_frame, stale_sampled_at, retry_clock));
    INFO("failed body path did not reach PCM preparation checkpoint");
    REQUIRE(preparation_checkpoint_reached);
    INFO("failed expired fallback did not make exactly one update attempt");
    REQUIRE(retry_output.body_calls.size() == 2);
    INFO("failed body update used an unexpected lease sequence");
    REQUIRE(retry_output.body_calls.back().request.sequence == retry_sequence);
    INFO("failed body attempt did not record its expired fallback");
    REQUIRE((retry_output.body_calls.back().request.fallback_rumble.low_frequency == 0.0F &&
             retry_output.body_calls.back().request.fallback_rumble.high_frequency == 0.0F));

    retry_output.body_status = INPUT_NEXT_STATUS_OK;
    INFO("body send during retry cooldown was not treated as a skipped send");
    REQUIRE(retry_session->send(stale_frame, stale_sampled_at, retry_clock));
    INFO("failed body attempt bypassed cooldown with its expired fallback");
    REQUIRE(retry_output.body_calls.size() == 2);

    auto fresh_frame = body_frame(retry_clock.now(), 45, 2s);
    fresh_frame.rumble = RumbleIntent{
        .payload = {.low_frequency = 0.2F, .high_frequency = 0.8F},
        .ttl_ms = 180,
    };
    const auto fresh_sampled_at = retry_clock.now();
    INFO("fresh fallback did not recover body output");
    REQUIRE(retry_session->send(fresh_frame, fresh_sampled_at, retry_clock));
    INFO("fresh fallback sample did not replace the expired attempt state");
    REQUIRE((retry_output.body_calls.size() == 3 &&
             retry_output.body_calls.back().request.sequence == retry_sequence &&
             retry_output.body_calls.back().request.fallback_rumble.low_frequency == 0.2F &&
             retry_output.body_calls.back().request.fallback_rumble.high_frequency == 0.8F));

    retry_clock.supplied += 101ms;
    INFO("still-live fresh fallback send failed");
    REQUIRE(retry_session->send(fresh_frame, fresh_sampled_at, retry_clock));
    INFO("an older fallback deadline cleared the newly accepted sample");
    REQUIRE((retry_output.body_calls.size() == 4 &&
             retry_output.body_calls.back().request.sequence == retry_sequence + 1 &&
             retry_output.body_calls.back().request.fallback_rumble.low_frequency == 0.2F &&
             retry_output.body_calls.back().request.fallback_rumble.high_frequency == 0.8F));

    retry_clock.supplied += 80ms;
    INFO("fresh fallback expiry send failed");
    REQUIRE(retry_session->send(fresh_frame, fresh_sampled_at, retry_clock));
    INFO("fresh fallback did not expire at its own TTL");
    REQUIRE((retry_output.body_calls.size() == 5 &&
             retry_output.body_calls.back().request.sequence == retry_sequence + 2 &&
             retry_output.body_calls.back().request.fallback_rumble.low_frequency == 0.0F &&
             retry_output.body_calls.back().request.fallback_rumble.high_frequency == 0.0F));
}

TEST_CASE("effects.output_session.body_identity_retained_after_submission_wait",
    "[component][effects][output_session]") {
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 300s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);
    const auto submitted_at = clock.now();
    output.after_body_submit = [&clock] noexcept {
        clock.supplied += 150ms;
    };

    REQUIRE(session->send(body_frame(submitted_at, 700), submitted_at, clock));
    REQUIRE(session->has_body_owner());
    REQUIRE(clock.now() == submitted_at + 150ms);

    output.body_clear_status = INPUT_NEXT_STATUS_BUSY;
    clock.supplied = submitted_at + 200ms;
    REQUIRE_FALSE(session->clear_body(clock));
    REQUIRE(session->has_body_owner());
    REQUIRE(output.body_clears.size() == 1);

    clock.supplied = submitted_at + 350ms;
    REQUIRE(session->clear_body(clock));
    REQUIRE_FALSE(session->has_body_owner());
    REQUIRE(output.body_clears.size() == 1);
    REQUIRE(session->close_and_drain(clock));
}

} // namespace
