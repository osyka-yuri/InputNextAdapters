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

TEST_CASE("effects.output_session.target_audio_rebind_baselines",
    "[component][effects][output_session]") {
    FakeOutput output;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 700;
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 30s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    OutputFrame frame = body_frame(clock.now(), 31);
    frame.left = trigger();
    INFO("initial body/trigger delivery failed");
    REQUIRE(session->send(frame, clock.now(), clock));
    INFO("initial body owner was not accepted");
    REQUIRE((session->has_body_owner() && output.body_calls.size() == 1));

    const auto old_target = output.target;
    const auto old_observation_at = clock.now();
    ++output.target.connection_id;
    clock.supplied += 1ms;
    INFO("target rebind reused an old observation");
    REQUIRE_FALSE(session->reconcile(old_observation_at, clock));
    INFO("target rebind used an ordinary body clear instead of revoking the old context");
    REQUIRE(output.body_clears.empty());
    INFO("target rebind used an ordinary trigger clear instead of revoking the old context");
    REQUIRE(output.trigger_clears.empty());
    INFO("new binding did not record its post-query start time");
    REQUIRE(session->binding_started_at() == clock.now());

    clock.supplied += 1ms;
    observer.publish(clock.now());
    INFO("fresh observation did not admit the replacement target");
    REQUIRE(session->reconcile(clock.now(), clock));
    INFO("replacement binding start time changed after fresh observation");
    REQUIRE((session->binding_started_at() && *session->binding_started_at() == clock.now() - 1ms));

    frame = body_frame(clock.now(), 32);
    INFO("replacement target body delivery failed");
    REQUIRE(session->send(frame, clock.now(), clock));
    INFO("body output did not use the rebound target");
    REQUIRE((output.body_calls.size() == 2 &&
             same_target(output.body_calls.back().request.target, output.target)));

    const auto prior_audio_instance = output.capabilities.audio_instance_id;
    const auto pre_audio_observation = clock.now();
    output.capabilities.audio_instance_id = 701;
    clock.supplied += 1ms;
    INFO("audio rebind lost the still-current target binding");
    REQUIRE(session->reconcile(pre_audio_observation, clock));
    INFO("audio instance change did not clear and report the previous body owner");
    REQUIRE((observer.body_rebound_count == 1 && output.body_clears.size() == 1 &&
             output.body_clears.back().audio_instance_id == prior_audio_instance));
    INFO("audio rebind did not reset observer freshness");
    REQUIRE_FALSE(observer.ready);

    clock.supplied += 1ms;
    observer.publish(clock.now());
    INFO("fresh observer with stale body sample was rejected");
    REQUIRE(session->send(frame, pre_audio_observation, clock));
    INFO("body was submitted before an observation newer than the audio baseline");
    REQUIRE(output.body_calls.size() == 2);

    clock.supplied += 1ms;
    observer.publish(clock.now());
    INFO("post-audio-rebind observation was not admitted");
    REQUIRE(session->reconcile(clock.now(), clock));
    frame = body_frame(clock.now(), 33);
    INFO("fresh body sample after audio rebind failed");
    REQUIRE(session->send(frame, clock.now(), clock));
    INFO("fresh body sample did not route to the replacement audio instance");
    REQUIRE((output.body_calls.size() == 3 &&
             output.body_calls.back().request.audio_instance_id == 701 &&
             output.body_calls.back().request.route_preference ==
                 INPUT_NEXT_EFFECTS_BODY_ROUTE_PREFER_HD));
}

} // namespace
