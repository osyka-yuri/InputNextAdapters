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

TEST_CASE("effects.output_session.trigger_cache_ttl", "[component][effects][output_session]") {
    FakeOutput output;
    output.disable_body_leases();
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 10s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    OutputFrame frame{};
    frame.left = trigger();
    INFO("initial trigger send failed");
    REQUIRE(session->send(frame, clock.now(), clock));
    INFO("initial trigger intent was not submitted");
    REQUIRE(output.triggers.size() == 1);

    clock.supplied += 10ms;
    INFO("cached trigger send failed");
    REQUIRE(session->send(frame, clock.now(), clock));
    INFO("unchanged trigger was refreshed before its refresh window");
    REQUIRE(output.triggers.size() == 1);

    clock.supplied += 90ms;
    INFO("due trigger refresh failed");
    REQUIRE(session->send(frame, clock.now(), clock));
    INFO("trigger TTL/cache did not refresh at its boundary");
    REQUIRE(output.triggers.size() == 2);

    clock.supplied += 1ms;
    INFO("trigger withdrawal failed");
    REQUIRE(session->send(OutputFrame(), clock.now(), clock));
    INFO("removed trigger intent did not clear exactly its accepted channel");
    REQUIRE((output.trigger_clears.size() == 1 &&
             output.trigger_clears.back().second == INPUT_NEXT_EFFECTS_LEFT_TRIGGER));

    FakeOutput slow_output;
    FakeObserver slow_observer;
    const auto slow_session = std::make_unique<OutputSession>(slow_output, slow_observer, 1);
    TickClock slow_clock{.supplied = MonoTime{} + 20s, .use_steady_clock = false};
    bind_with_fresh_sample(*slow_session, slow_observer, slow_clock);
    OutputFrame slow_frame = body_frame(slow_clock.now(), 18, 500ms);
    slow_frame.left = trigger(50);
    const auto sampled_at = slow_clock.now();
    bool trigger_preceded_body_completion = false;
    slow_output.after_body_submit = [&] {
        trigger_preceded_body_completion = slow_output.triggers.size() == 1;
        slow_clock.supplied += 60ms;
    };
    INFO("slow body send failed");
    REQUIRE(slow_session->send(slow_frame, sampled_at, slow_clock));
    INFO("trigger did not use its original TTL before body I/O and expire during that I/O");
    REQUIRE((trigger_preceded_body_completion && slow_output.body_calls.size() == 1 &&
             slow_output.triggers.front().ttl_ms == 50 && slow_output.trigger_clears.size() == 1));
}
} // namespace
