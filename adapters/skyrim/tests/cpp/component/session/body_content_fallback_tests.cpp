#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <algorithm>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <input_next_effects.h>
#include <memory>
#include <skyrim_adapter/content_observation.hpp>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/model.hpp>
#include <vector>

namespace skyrim_adapter::tests::session {

namespace {

void check_classic_warning(unsigned stage,
    std::chrono::milliseconds duration,
    bool transformation) {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC;
    Observation current = idle();
    ContentObservation content{};
    content.beast = BeastForm::none;
    content.survival_enabled = !transformation;
    if (!transformation) {
        content.cold_stage = 2;
        content.hunger_stage = 2;
        content.exhaustion_stage = 2;
    }
    current.content = content;
    fixture.settle(current);

    if (transformation) {
        content.beast = BeastForm::werewolf;
    } else {
        content.cold_stage = stage;
    }
    current.content = content;
    fixture.advance(current, 20ms);
    const MonoTime onset = fixture.time;
    const auto& first = fixture.output.body_submissions.back().request;
    INFO("classic Survival/transformation onset keeps PCM silent and carries an energized bounded "
         "fallback");
    REQUIRE((first.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             fixture.output.body_submissions.back().samples.empty() && first.ttl_ms <= 200 &&
             first.fallback_rumble.low_frequency > 0.0F &&
             first.fallback_rumble.high_frequency > 0.0F));

    fixture.advance(current, 180ms);
    const auto& before_two_hundred = fixture.output.body_submissions.back().request;
    INFO("classic warning fallback renews before the 200ms lease boundary");
    REQUIRE((before_two_hundred.ttl_ms <= 200 &&
             (before_two_hundred.fallback_rumble.low_frequency > 0.0F ||
                 before_two_hundred.fallback_rumble.high_frequency > 0.0F)));
    fixture.advance(current, 20ms);
    const auto& at_two_hundred = fixture.output.body_submissions.back().request;
    INFO("classic warning remains energized through a timely renewal at 200ms");
    REQUIRE((
        at_two_hundred.ttl_ms <= 200 && (at_two_hundred.fallback_rumble.low_frequency > 0.0F ||
                                            at_two_hundred.fallback_rumble.high_frequency > 0.0F)));

    fixture.advance(current, duration - 200ms);
    const auto& expired = fixture.output.body_submissions.back().request;
    INFO("classic warning becomes neutral at its original finite authored expiry");
    REQUIRE((expired.pcm_command != INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE &&
             expired.fallback_rumble.low_frequency == 0.0F &&
             expired.fallback_rumble.high_frequency == 0.0F));
    const auto energized_at_expiry = std::count_if(fixture.output.body_submissions.begin(),
        fixture.output.body_submissions.end(),
        [](const BodySubmission& submission) {
            return submission.request.fallback_rumble.low_frequency > 0.0F ||
                   submission.request.fallback_rumble.high_frequency > 0.0F;
        });
    fixture.advance(current, 50ms);
    const auto& after_expiry = fixture.output.body_submissions.back().request;
    const auto energized_after_expiry = std::count_if(fixture.output.body_submissions.begin(),
        fixture.output.body_submissions.end(),
        [](const BodySubmission& submission) {
            return submission.request.fallback_rumble.low_frequency > 0.0F ||
                   submission.request.fallback_rumble.high_frequency > 0.0F;
        });
    INFO("expired warning stays neutral and does not replay after the fallback lease boundary");
    REQUIRE((after_expiry.fallback_rumble.low_frequency == 0.0F &&
             after_expiry.fallback_rumble.high_frequency == 0.0F &&
             energized_at_expiry == energized_after_expiry));
    INFO("coordinator warning session samples the exact authored expiry and post-expiry boundary");
    REQUIRE(fixture.time == onset + duration + 50ms);
}

} // namespace

TEST_CASE("skyrim.session.body_classic_cold_stage_3_warning_expires",
    "[component][skyrim][session]") {
    check_classic_warning(3, 260ms, false);
}

TEST_CASE("skyrim.session.body_classic_cold_stage_4_warning_expires",
    "[component][skyrim][session]") {
    check_classic_warning(4, 320ms, false);
}

TEST_CASE("skyrim.session.body_classic_cold_stage_5_warning_expires",
    "[component][skyrim][session]") {
    check_classic_warning(5, 380ms, false);
}

TEST_CASE("skyrim.session.body_classic_werewolf_warning_expires", "[component][skyrim][session]") {
    check_classic_warning(0, 300ms, true);
}

TEST_CASE("skyrim.session.body_content_audio_instance_change_clears_warning",
    "[component][skyrim][session]") {
    const auto barrier_owner = std::make_unique<Fixture>();
    auto& barrier = *barrier_owner;
    barrier.output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    barrier.output.capabilities.audio_instance_id = 71;
    barrier.output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC;
    Observation current = idle();
    ContentObservation content{};
    content.beast = BeastForm::none;
    content.survival_enabled = true;
    content.cold_stage = 2;
    content.hunger_stage = 2;
    content.exhaustion_stage = 2;
    current.content = content;
    barrier.settle(current);
    content.cold_stage = 3;
    current.content = content;
    barrier.advance(current, 20ms);
    INFO("barrier fixture starts with an energized Survival pulse");
    REQUIRE(barrier.output.body_submissions.back().request.fallback_rumble.low_frequency > 0.0F);
    barrier.output.capabilities.audio_instance_id = 72;
    barrier.time += 1ms;
    barrier.coordinator.tick(barrier.time);
    barrier.advance(current, 20ms);
    const auto& rebound = barrier.output.body_submissions.back().request;
    INFO("audio-instance barrier establishes a neutral classic-route content baseline without "
         "replay");
    REQUIRE((rebound.audio_instance_id == 72 && rebound.fallback_rumble.low_frequency == 0.0F &&
             rebound.fallback_rumble.high_frequency == 0.0F &&
             rebound.pcm_command != INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE));
}

TEST_CASE("skyrim.session.body_critical_health_heartbeat_has_gap_and_end",
    "[component][skyrim][session]") {
    const auto heartbeat_owner = std::make_unique<Fixture>();
    auto& heartbeat = *heartbeat_owner;
    auto critical = idle();
    critical.in_combat = true;
    critical.health_ratio = 0.10F;
    heartbeat.settle(critical);
    heartbeat.advance(critical, 1060ms);
    const auto& first_beat = heartbeat.output.body_submissions.back().request;
    INFO("critical-health heartbeat did not begin with its first fallback beat");
    REQUIRE(first_beat.fallback_rumble.low_frequency > 0.0F);
    heartbeat.advance(critical, 55ms);
    const auto& gap = heartbeat.output.body_submissions.back().request;
    INFO("heartbeat first fallback beat stretched into its authored gap");
    REQUIRE((gap.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             gap.fallback_rumble.low_frequency == 0.0F &&
             gap.fallback_rumble.high_frequency == 0.0F));
    heartbeat.advance(critical, 100ms);
    const auto& second_beat = heartbeat.output.body_submissions.back().request;
    INFO("heartbeat second fallback beat was omitted at its authored 155ms boundary");
    REQUIRE((second_beat.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             (second_beat.fallback_rumble.low_frequency > 0.0F ||
                 second_beat.fallback_rumble.high_frequency > 0.0F)));
    heartbeat.advance(critical, 45ms);
    const auto& heartbeat_end = heartbeat.output.body_submissions.back().request;
    INFO("heartbeat fallback ended silently after its authored 200ms window");
    REQUIRE((heartbeat_end.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP &&
             heartbeat.output.body_submissions.back().samples.empty() &&
             heartbeat_end.fallback_rumble.low_frequency == 0.0F &&
             heartbeat_end.fallback_rumble.high_frequency == 0.0F));
}

} // namespace skyrim_adapter::tests::session
