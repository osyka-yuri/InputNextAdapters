#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "output_session/fake_output.hpp"
#include "output_session/support.hpp"

#include <input_next_effects.h>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <chrono>
#include <memory>

namespace {
using namespace std::chrono_literals;
using namespace inputnext::adapters;
using namespace inputnext::test::output_session;

TEST_CASE("effects.output_session.independent_sessions", "[component][effects][output_session]") {
    FakeOutput first_output;
    FakeOutput second_output;
    for (FakeOutput* output : {&first_output, &second_output}) {
        output->capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
        output->capabilities.audio_instance_id = 990;
        output->body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    }
    FakeObserver first_observer;
    FakeObserver second_observer;
    const auto first = std::make_unique<OutputSession>(first_output, first_observer, 1);
    const auto second = std::make_unique<OutputSession>(second_output, second_observer, 1);
    TickClock clock{.supplied = MonoTime{} + 70s, .use_steady_clock = false};
    bind_with_fresh_sample(*first, first_observer, clock);
    bind_with_fresh_sample(*second, second_observer, clock);

    const auto shared_start = clock.now();
    OutputFrame shared = body_frame(shared_start, 99, 1000ms, true);
    shared.left = trigger();
    INFO("first independent session send failed");
    REQUIRE(first->send(shared, shared_start, clock));
    INFO("second independent session send failed");
    REQUIRE(second->send(shared, shared_start, clock));
    const auto& shared_voice4 =
        require_value(shared.body.voices[4], "shared body voice 4 must be engaged");
    INFO("independent ports did not start with matching but separate session state");
    REQUIRE((first_output.body_calls.size() == 1 && second_output.body_calls.size() == 1 &&
             first_output.body_calls[0].request.sequence == 1 &&
             second_output.body_calls[0].request.sequence == 1 &&
             first_output.body_calls[0].request.audio_instance_id ==
                 second_output.body_calls[0].request.audio_instance_id &&
             same_target(first_output.body_calls[0].request.target,
                 second_output.body_calls[0].request.target) &&
             shared_voice4.stable_id == 99));
    INFO("one independent session reused the other's trigger cache");
    REQUIRE((first_output.triggers.size() == 1 && second_output.triggers.size() == 1));

    clock.supplied += 20ms;
    OutputFrame changed_first = shared;
    auto& voice = changed_first.body.voices[4];
    if (!voice.has_value()) {
        fail("shared body voice 4 must be engaged");
    }
    voice->wave.level += 0.1F;
    INFO("first session semantic body update failed");
    REQUIRE(first->send(changed_first, clock.now(), clock));
    INFO("second session cached body check failed");
    REQUIRE(second->send(shared, clock.now(), clock));
    INFO("one session's changed program perturbed the other session's independent attempt history");
    REQUIRE((first_output.body_calls.size() == 2 &&
             first_output.body_calls.back().request.sequence == 2 &&
             second_output.body_calls.size() == 1));

    clock.supplied += 80ms;
    INFO("second session held refresh failed");
    REQUIRE(second->send(shared, clock.now(), clock));
    INFO("second session did not retain its own accepted sequence and refresh history");
    REQUIRE((second_output.body_calls.size() == 2 &&
             second_output.body_calls.back().request.sequence == 2));
    INFO("clearing one independent port affected the other session's body owner");
    REQUIRE((first->clear_body(clock) && !first->has_body_owner() && second->has_body_owner() &&
             first_output.body_clears.size() == 1 && second_output.body_clears.empty()));
    INFO("withdrawing triggers from one independent port affected the other port");
    REQUIRE((first->withdraw_triggers() && first_output.trigger_clears.size() == 1 &&
             second_output.trigger_clears.empty()));
}

} // namespace
