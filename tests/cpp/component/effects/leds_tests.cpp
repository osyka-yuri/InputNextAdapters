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

TEST_CASE("effects.output_session.led_group_ttl_and_channel_independence",
    "[component][effects][output_session][led]") {
    FakeOutput output;
    enable_leds(output);
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 100s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    const auto sampled_at = clock.now();
    OutputFrame frame{};
    frame.left = trigger(200);
    frame.right = trigger(200);
    frame.leds = led(sampled_at, 180);
    INFO("independent trigger and LED delivery failed");
    REQUIRE(session->send(frame, sampled_at, clock));
    INFO("LED was combined with the trigger channel request");
    REQUIRE((output.triggers.size() == 1 &&
             output.triggers.front().channel_mask ==
                 (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER)));
    INFO("whole LED group was not delivered as its own request");
    REQUIRE((
        output.leds.size() == 1 && output.leds.front().channel_mask == INPUT_NEXT_EFFECTS_LEDS &&
        output.leds.front().ttl_ms == 180 &&
        output.leds.front().leds.lightbar_rgb == UINT32_C(0x00123456) &&
        output.leds.front().leds.player_index == 2 &&
        output.leds.front().leds.microphone_led == 1 && output.leds.front().leds.reserved == 0));

    frame.left.reset();
    frame.right.reset();
    clock.supplied += 10ms;
    INFO("removing triggers also removed a still-valid LED");
    REQUIRE(session->send(frame, sampled_at, clock));
    REQUIRE((output.trigger_clear_requests.size() == 1 &&
             output.trigger_clear_requests.front().channel_mask ==
                 (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER)));
    REQUIRE(output.led_clear_requests.empty());

    clock.supplied += 90ms;
    INFO("unchanged LED renewal failed");
    REQUIRE(session->send(frame, sampled_at, clock));
    INFO("LED was not renewed from the original source age");
    REQUIRE((output.leds.size() == 2 && output.leds.back().ttl_ms == 80));

    clock.supplied += 80ms;
    INFO("LED source expiry failed to clean its accepted contribution");
    REQUIRE(session->send(frame, sampled_at, clock));
    INFO("LED expiry did not conditionally clear the last accepted revision");
    REQUIRE((output.led_clear_requests.size() == 1 &&
             output.led_clear_requests.front().channel_mask == INPUT_NEXT_EFFECTS_LEDS &&
             output.led_clear_requests.front().channel_revisions[3] == 2));
    INFO("LED expiry cleared a trigger channel");
    REQUIRE(output.trigger_clear_requests.size() == 1);
}

TEST_CASE("effects.output_session.led_validation_capabilities_and_explicit_withdrawal",
    "[component][effects][output_session][led]") {
    FakeOutput output;
    enable_leds(output);
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 110s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    const auto source_at = clock.now();
    const auto binding_baseline = session->binding_started_at();
    const auto binding_started_at =
        require_value(binding_baseline, "LED validation setup had no binding start time");
    const auto base = led(source_at);
    std::array<LedIntent, 8> invalid{
        base,
        base,
        base,
        base,
        base,
        base,
        base,
        base,
    };
    invalid[0].ttl_ms = 0;
    invalid[1].ttl_ms = 201;
    invalid[2].source_at = source_at + 1ms;
    invalid[3].source_at = binding_started_at;
    invalid[4].payload.lightbar_rgb = UINT32_C(0xff123456);
    invalid[5].payload.player_index = 5;
    invalid[6].payload.microphone_led = 2;
    invalid[7].payload.reserved = 1;
    for (const auto& intent : invalid) {
        OutputFrame frame{};
        frame.leds = intent;
        INFO("invalid or non-fresh LED intent was submitted");
        REQUIRE(session->send(frame, source_at, clock));
        REQUIRE(output.leds.empty());
    }

    OutputFrame frame{};
    frame.leds = base;
    REQUIRE(session->send(frame, source_at, clock));
    REQUIRE(output.leds.size() == 1);

    output.capabilities.feature_flags &= ~INPUT_NEXT_EFFECTS_CAP_LIGHTBAR;
    clock.supplied += 1ms;
    INFO("capability loss did not clear only the accepted LED group");
    REQUIRE(session->reconcile(clock.now(), clock));
    REQUIRE(session->send(frame, source_at, clock));
    REQUIRE((output.led_clear_requests.size() == 1 &&
             output.led_clear_requests.back().channel_mask == INPUT_NEXT_EFFECTS_LEDS &&
             output.led_clear_requests.back().channel_revisions[3] == 1));

    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_LIGHTBAR;
    output.capabilities.supported_channels &= ~INPUT_NEXT_EFFECTS_LEDS;
    clock.supplied += 1ms;
    REQUIRE(session->reconcile(clock.now(), clock));
    frame.leds = led(clock.now());
    REQUIRE(session->send(frame, clock.now(), clock));
    REQUIRE(output.leds.size() == 1);

    output.capabilities.supported_channels |= INPUT_NEXT_EFFECTS_LEDS;
    clock.supplied += 1ms;
    REQUIRE(session->reconcile(clock.now(), clock));
    frame.left = trigger(200);
    frame.leds = led(clock.now());
    REQUIRE(session->send(frame, clock.now(), clock));
    REQUIRE(output.leds.size() == 2);
    INFO("explicit LED withdrawal did not clear the exact LED revision");
    REQUIRE(session->withdraw_leds());
    REQUIRE((output.led_clear_requests.size() == 2 &&
             output.led_clear_requests.back().channel_mask == INPUT_NEXT_EFFECTS_LEDS &&
             output.led_clear_requests.back().channel_revisions[3] == 3));
    REQUIRE(output.trigger_clear_requests.empty());
    REQUIRE(session->withdraw_triggers());
    REQUIRE(
        (output.trigger_clear_requests.size() == 1 &&
            output.trigger_clear_requests.front().channel_mask == INPUT_NEXT_EFFECTS_LEFT_TRIGGER));
}

TEST_CASE("effects.output_session.led_cleanup_after_blocking_body_and_restriction",
    "[component][effects][output_session][led]") {
    FakeOutput output;
    enable_leds(output);
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 120s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    const auto sampled_at = clock.now();
    OutputFrame frame = body_frame(sampled_at, 81, 500ms);
    frame.left = trigger(200);
    frame.leds = led(sampled_at, 50);
    bool trigger_preceded_led = false;
    bool led_preceded_body = false;
    output.after_led_submit = [&] {
        trigger_preceded_led = output.triggers.size() == 1;
    };
    output.after_body_submit = [&] {
        led_preceded_body = output.leds.size() == 1;
        clock.supplied += 60ms;
    };
    INFO("blocking body submission failed to finish with the LED removed");
    REQUIRE(session->send(frame, sampled_at, clock));
    REQUIRE(trigger_preceded_led);
    REQUIRE(led_preceded_body);
    REQUIRE(output.body_calls.size() == 1);
    REQUIRE((output.led_clear_requests.size() == 1 &&
             output.led_clear_requests.front().channel_mask == INPUT_NEXT_EFFECTS_LEDS &&
             output.led_clear_requests.front().channel_revisions[3] == 1));

    FakeOutput restricted_output;
    enable_leds(restricted_output);
    FakeObserver restricted_observer;
    const auto restricted_session =
        std::make_unique<OutputSession>(restricted_output, restricted_observer, 1);
    TickClock restricted_clock{.supplied = MonoTime{} + 130s, .use_steady_clock = false};
    bind_with_fresh_sample(*restricted_session, restricted_observer, restricted_clock);
    OutputFrame restricted_frame{};
    restricted_frame.leds = led(restricted_clock.now());
    restricted_output.after_led_submit = [&] {
        restricted_observer.restriction = FrameRestriction::neutral;
    };
    INFO("neutral restriction after LED acceptance did not clear its exact revision");
    REQUIRE(restricted_session->send(restricted_frame, restricted_clock.now(), restricted_clock));
    REQUIRE(
        (restricted_output.leds.size() == 1 && restricted_output.led_clear_requests.size() == 1 &&
            restricted_output.led_clear_requests.front().channel_revisions[3] == 1));

    FakeOutput expired_output;
    enable_leds(expired_output);
    FakeObserver expired_observer;
    const auto expired_session =
        std::make_unique<OutputSession>(expired_output, expired_observer, 2);
    TickClock expired_clock{.supplied = MonoTime{} + 140s, .use_steady_clock = false};
    bind_with_fresh_sample(*expired_session, expired_observer, expired_clock);
    OutputFrame expired_frame{};
    expired_frame.leds = led(expired_clock.now());
    expired_output.after_led_submit = [&] {
        expired_observer.ready = false;
    };
    INFO("admission loss after LED acceptance retained a live context");
    REQUIRE_FALSE(expired_session->send(expired_frame, expired_clock.now(), expired_clock));
    REQUIRE_FALSE(expired_session->bound());
    REQUIRE(expired_output.leds.size() == 1);
    REQUIRE(expired_output.closes.size() == 1);
    REQUIRE(expired_output.acknowledgements.size() == 1);
    REQUIRE(FakeOutput::same_context(expired_output.closes.front().context,
        expired_output.leds.front().context));
}

TEST_CASE("effects.output_session.led_busy_retains_exact_cache_and_diagnostics",
    "[component][effects][output_session][led]") {
    FakeOutput output;
    enable_leds(output);
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 140s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    const auto first_sample = clock.now();
    OutputFrame accepted{};
    accepted.leds = led(first_sample, 200);
    REQUIRE(session->send(accepted, first_sample, clock));
    REQUIRE(output.leds.size() == 1);

    clock.supplied += 1ms;
    OutputFrame replacement = body_frame(clock.now(), 82, 500ms);
    replacement.leds = led(clock.now(), 200);
    replacement.leds->payload.lightbar_rgb = UINT32_C(0x00654321);
    replacement.rumble = RumbleIntent{
        .payload = {.low_frequency = 0.4F, .high_frequency = 0.2F},
        .ttl_ms = 200,
    };
    output.led_status = INPUT_NEXT_STATUS_BUSY;
    INFO("BUSY LED replacement did not allow the independent body channel to progress");
    REQUIRE_FALSE(session->send(replacement, clock.now(), clock));
    REQUIRE(output.leds.size() == 2);
    REQUIRE(output.body_calls.size() == 2);
    REQUIRE(output.led_clear_requests.empty());
    REQUIRE(output.contexts.front().channel_revisions[3] == 1);
    INFO("later successful body delivery erased the LED failure diagnostic");
    REQUIRE(session->diagnostics().last_status == INPUT_NEXT_STATUS_BUSY);

    FakeOutput clear_output;
    enable_leds(clear_output);
    FakeObserver clear_observer;
    const auto clear_session = std::make_unique<OutputSession>(clear_output, clear_observer, 0);
    TickClock clear_clock{.supplied = MonoTime{} + 150s, .use_steady_clock = false};
    bind_with_fresh_sample(*clear_session, clear_observer, clear_clock);
    OutputFrame clear_frame{};
    clear_frame.leds = led(clear_clock.now(), 200);
    REQUIRE(clear_session->send(clear_frame, clear_clock.now(), clear_clock));
    clear_output.led_clear_status = INPUT_NEXT_STATUS_BUSY;
    INFO("BUSY clear discarded the accepted LED owner");
    REQUIRE_FALSE(clear_session->send(OutputFrame{}, clear_clock.now(), clear_clock));
    REQUIRE_FALSE(clear_output.led_clear_requests.empty());
    for (const auto& clear : clear_output.led_clear_requests) {
        REQUIRE(clear.channel_mask == INPUT_NEXT_EFFECTS_LEDS);
        REQUIRE(clear.channel_revisions[3] == 1);
        REQUIRE(FakeOutput::same_context(clear.context, clear_output.leds.front().context));
    }
    REQUIRE(clear_output.contexts.front().channel_revisions[3] == 1);
    clear_output.led_clear_status = INPUT_NEXT_STATUS_OK;
    INFO("retry of the retained exact LED owner failed");
    REQUIRE(clear_session->withdraw_leds());
    REQUIRE(clear_output.led_clear_requests.back().channel_revisions[3] == 1);
}

TEST_CASE("effects.output_session.led_receipt_failure_retains_context_cleanup",
    "[component][effects][output_session][led]") {
    FakeOutput output;
    enable_leds(output);
    output.receipt_fault = ReceiptFault::revision;
    output.close_status = INPUT_NEXT_STATUS_BUSY;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 160s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    OutputFrame frame{};
    frame.leds = led(clock.now());
    INFO("malformed LED revision receipt was accepted");
    REQUIRE_FALSE(session->send(frame, clock.now(), clock));
    REQUIRE_FALSE(session->bound());
    REQUIRE(output.leds.size() == 1);
    REQUIRE_FALSE(output.contexts.empty());
    REQUIRE_FALSE(output.closes.empty());
    const auto retired_context = output.closes.front().context;
    REQUIRE(FakeOutput::same_context(retired_context, output.contexts.front().context));

    output.close_status = INPUT_NEXT_STATUS_REVOKED;
    INFO("retained exact-context close and acknowledgement did not drain");
    REQUIRE(session->close_and_drain(clock));
    REQUIRE(output.contexts.empty());
    REQUIRE(output.acknowledgements.size() == 1);
    REQUIRE(FakeOutput::same_context(output.acknowledgements.front().context, retired_context));
}

TEST_CASE("effects.output_session.led_samples_do_not_cross_rebinding",
    "[component][effects][output_session][led]") {
    FakeOutput output;
    enable_leds(output);
    advertise_hd(output, 901);
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 170s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    const auto old_sample = clock.now();
    OutputFrame frame{};
    frame.leds = led(old_sample, 200);
    REQUIRE(session->send(frame, old_sample, clock));
    REQUIRE(output.leds.size() == 1);

    advertise_hd(output, 902);
    clock.supplied += 1ms;
    REQUIRE(session->reconcile(clock.now(), clock));
    clock.supplied += 1ms;
    observer.publish(clock.now());
    REQUIRE(session->reconcile(clock.now(), clock));
    INFO("audio-only rebound discarded the same-context accepted LED cache");
    REQUIRE(session->send(frame, old_sample, clock));
    REQUIRE(output.leds.size() == 1);

    ++output.target.connection_id;
    clock.supplied += 1ms;
    INFO("new target did not replace the binding before cleanup");
    REQUIRE_FALSE(session->reconcile(clock.now(), clock));
    REQUIRE(session->bound());
    clock.supplied += 1ms;
    observer.publish(clock.now());
    REQUIRE(session->reconcile(clock.now(), clock));
    INFO("an LED sample from the previous binding was replayed to the new target");
    REQUIRE(session->send(frame, old_sample, clock));
    REQUIRE(output.leds.size() == 1);
}

TEST_CASE("effects.output_session.blocking_led_clear_rechecks_trigger_expiry",
    "[component][effects][output_session][led]") {
    FakeOutput output;
    enable_leds(output);
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 180s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    const auto sampled_at = clock.now();
    auto frame = body_frame(sampled_at, 83, 500ms);
    frame.left = trigger(100);
    frame.leds = led(sampled_at, 20);
    output.after_body_submit = [&] {
        clock.supplied += 30ms;
    };
    output.after_led_clear = [&] {
        clock.supplied += 80ms;
    };

    REQUIRE(session->send(frame, sampled_at, clock));
    REQUIRE(output.led_clear_requests.size() == 1);
    REQUIRE_FALSE(output.contexts.empty());
    INFO("a final LED clear must not leave an expired accepted trigger behind");
    REQUIRE(output.contexts.front().channel_revisions[0] == 2);
}
} // namespace
