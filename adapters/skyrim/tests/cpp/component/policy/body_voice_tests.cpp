#include "policy/support/policy_test_support.hpp"
#include "test_support.hpp"
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <skyrim_adapter/body_waveform.hpp>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::tests::policy {

namespace {

constexpr std::array k_shout_levels{ShoutLevel::one_word,
    ShoutLevel::two_words,
    ShoutLevel::three_words};

EffectFrame emit_voice(Policy& policy, ShoutLevel level, std::int64_t timestamp) {
    Observation const sample = observation(at(timestamp));
    Action event = action(EventKind::voice_fire, Hand::voice, at(timestamp), 0);
    event.event.shout_level = level;
    const std::array<Action, 1> events{event};
    return policy.step(sample, events, at(timestamp));
}

EffectFrame emit_voice(ShoutLevel level, std::int64_t timestamp) {
    const auto policy = std::make_unique<Policy>();
    return emit_voice(*policy, level, timestamp);
}

EffectFrame emit_voice(ShoutLevel level, const PolicyConfig& config, std::int64_t timestamp) {
    const auto policy = std::make_unique<Policy>(config);
    return emit_voice(*policy, level, timestamp);
}

} // namespace

TEST_CASE("skyrim.policy.body_voice_shout_levels_progress", "[component][skyrim][policy]") {
    std::array<EffectFrame, k_shout_levels.size()> defaults{};
    for (std::size_t index = 0; index < k_shout_levels.size(); ++index) {
        defaults[index] =
            emit_voice(k_shout_levels[index], 9000 + (static_cast<std::int64_t>(index) * 100));
        INFO("shout body cue missing");
        REQUIRE(defaults[index].body.body_transient.has_value());
        INFO("voice-fire copies the captured shout level into the body cue");
        CHECK(require_value(defaults[index].body.body_transient).shout_level ==
              k_shout_levels[index]);
        INFO("shout levels use classic and body output without adding trigger output");
        REQUIRE(defaults[index].rumble.has_value());
        CHECK(!defaults[index].left.has_value());
        CHECK(!defaults[index].right.has_value());
    }

    REQUIRE(defaults[0].body.body_transient.has_value());
    REQUIRE(defaults[1].body.body_transient.has_value());
    REQUIRE(defaults[2].body.body_transient.has_value());
    const BodyCue& one_word = require_value(defaults[0].body.body_transient);
    const BodyCue& two_words = require_value(defaults[1].body.body_transient);
    const BodyCue& three_words = require_value(defaults[2].body.body_transient);

    INFO("one-word classic cue missing");
    REQUIRE(defaults[0].rumble.has_value());
    INFO("two-word classic cue missing");
    REQUIRE(defaults[1].rumble.has_value());
    INFO("three-word classic cue missing");
    REQUIRE(defaults[2].rumble.has_value());
    const auto& one_rumble = require_value(defaults[0].rumble);
    const auto& two_rumble = require_value(defaults[1].rumble);
    const auto& three_rumble = require_value(defaults[2].rumble);

    INFO("one, two, and three-word shouts grow in duration and classic strength");
    CHECK(one_word.duration_ms < two_words.duration_ms);
    CHECK(two_words.duration_ms < three_words.duration_ms);
    CHECK(one_rumble.payload.low_frequency < two_rumble.payload.low_frequency);
    CHECK(two_rumble.payload.low_frequency < three_rumble.payload.low_frequency);
    CHECK(one_rumble.payload.high_frequency < two_rumble.payload.high_frequency);
    CHECK(two_rumble.payload.high_frequency < three_rumble.payload.high_frequency);
}

TEST_CASE("skyrim.policy.body_voice_unknown_level_fallback", "[component][skyrim][policy]") {
    const EffectFrame unknown = emit_voice(ShoutLevel::unknown, 9400);
    const EffectFrame invalid = emit_voice(static_cast<ShoutLevel>(0xFFU), 9400);
    REQUIRE(unknown.body.body_transient.has_value());
    REQUIRE(invalid.body.body_transient.has_value());
    const BodyCue& unknown_cue = require_value(unknown.body.body_transient);
    const BodyCue& invalid_cue = require_value(invalid.body.body_transient);

    INFO("unknown or invalid shout capture preserves the established voice effect exactly");
    CHECK(unknown_cue.duration_ms == 160);
    CHECK(unknown_cue.shout_level == ShoutLevel::unknown);
    CHECK(invalid_cue.duration_ms == unknown_cue.duration_ms);
    CHECK(invalid_cue.shout_level == ShoutLevel::unknown);
    CHECK(same_body_plan_identity(unknown.body, invalid.body));
    REQUIRE(unknown.rumble.has_value());
    REQUIRE(invalid.rumble.has_value());
    const auto& unknown_rumble = require_value(unknown.rumble);
    const auto& invalid_rumble = require_value(invalid.rumble);
    CHECK(unknown_rumble.payload.low_frequency == 0.42F);
    CHECK(unknown_rumble.payload.high_frequency == 0.22F);
    CHECK(unknown_rumble.ttl_ms == 100);
    CHECK(unknown_rumble.payload.low_frequency == invalid_rumble.payload.low_frequency);
    CHECK(unknown_rumble.payload.high_frequency == invalid_rumble.payload.high_frequency);
    CHECK(unknown_rumble.ttl_ms == invalid_rumble.ttl_ms);
}

TEST_CASE("skyrim.policy.body_voice_tuning_all_levels", "[component][skyrim][policy]") {
    std::array<EffectFrame, k_shout_levels.size()> defaults{};
    for (std::size_t index = 0; index < k_shout_levels.size(); ++index) {
        defaults[index] =
            emit_voice(k_shout_levels[index], 9000 + (static_cast<std::int64_t>(index) * 100));
    }

    const auto tuned_config = std::make_unique<PolicyConfig>();
    EffectAdjustment& voice_tuning =
        tuned_config->effects.body[static_cast<std::size_t>(EffectId::voice)];
    voice_tuning.hd_scale = 1.5F;
    voice_tuning.classic_scale = 0.5F;
    voice_tuning.duration_scale = 0.5F;
    voice_tuning.frequency_hz = 230.0F;
    for (std::size_t index = 0; index < k_shout_levels.size(); ++index) {
        const EffectFrame tuned = emit_voice(k_shout_levels[index],
            *tuned_config,
            9500 + (static_cast<std::int64_t>(index) * 100));
        INFO("tuned shout body cue missing");
        REQUIRE(tuned.body.body_transient.has_value());
        const BodyCue& cue = require_value(tuned.body.body_transient);
        REQUIRE(defaults[index].body.body_transient.has_value());
        const BodyCue& default_cue = require_value(defaults[index].body.body_transient);
        INFO("default classic cue missing");
        REQUIRE(defaults[index].rumble.has_value());
        const auto& default_rumble = require_value(defaults[index].rumble);
        INFO("tuned classic cue missing");
        REQUIRE(tuned.rumble.has_value());
        const auto& tuned_rumble = require_value(tuned.rumble);

        BodyPlan plan{};
        plan.body_transient = cue;
        const auto program = make_body_program(plan);
        INFO("tuned shout waveform missing");
        REQUIRE(program.voices[4].has_value());
        const auto& body_voice = require_value(program.voices[4]);

        INFO("the existing Body.Voice duration, amplitude, and frequency controls apply to every "
             "known level");
        CHECK(cue.duration_ms * 2U == default_cue.duration_ms);
        CHECK(cue.hd_scale == 1.5F);
        CHECK(cue.classic_scale == 0.5F);
        CHECK(cue.frequency_hz == 230.0F);
        CHECK(body_voice.hd_scale == 1.5F);
        CHECK(body_voice.duration_scale == 0.5F);
        CHECK(body_voice.wave.frequency == 230.0F);

        INFO(
            "Body.Voice classic level and duration scales apply equally to each known shout level");
        CHECK(std::abs(tuned_rumble.payload.low_frequency -
                       (default_rumble.payload.low_frequency * 0.5F)) < 0.0001F);
        CHECK(std::abs(tuned_rumble.payload.high_frequency -
                       (default_rumble.payload.high_frequency * 0.5F)) < 0.0001F);
        CHECK(tuned_rumble.ttl_ms * 2U == default_rumble.ttl_ms);
    }
}

TEST_CASE("skyrim.policy.body_voice_disabled_all_levels", "[component][skyrim][policy]") {
    const auto muted_config = std::make_unique<PolicyConfig>();
    muted_config->effects.body[static_cast<std::size_t>(EffectId::voice)].enabled = false;
    for (std::size_t index = 0; index < k_shout_levels.size(); ++index) {
        const EffectFrame muted = emit_voice(k_shout_levels[index],
            *muted_config,
            9800 + (static_cast<std::int64_t>(index) * 100));
        INFO("the existing Body.Voice enable setting mutes every known shout level");
        CHECK(!muted.body.body_transient.has_value());
        CHECK(!muted.rumble.has_value());
        CHECK(!muted.left.has_value());
        CHECK(!muted.right.has_value());
    }
}

TEST_CASE("skyrim.policy.body_voice_level_dedup", "[component][skyrim][policy]") {
    const auto same_time_policy = std::make_unique<Policy>();
    Observation const same_time_observation = observation(at(9900));
    Action first_level = action(EventKind::voice_fire, Hand::voice, at(9900), 0);
    first_level.event.shout_level = ShoutLevel::one_word;
    Action second_level = first_level;
    second_level.event.shout_level = ShoutLevel::two_words;
    const std::array<Action, 2> same_time_levels{first_level, second_level};
    const EffectFrame distinct_events =
        same_time_policy->step(same_time_observation, same_time_levels, at(9900));
    INFO("shout level participates in fallback event deduplication when native sequence is "
         "unavailable");
    REQUIRE(distinct_events.body.body_transient.has_value());
    CHECK(require_value(distinct_events.body.body_transient).shout_level == ShoutLevel::two_words);
}

TEST_CASE("skyrim.policy.body_voice_cast_silent", "[component][skyrim][policy]") {
    const auto cast_policy = std::make_unique<Policy>();
    Action const cast = action(EventKind::voice_cast, Hand::voice, at(10010), 1);
    const std::array<Action, 1> cast_events{cast};
    const EffectFrame cast_frame =
        cast_policy->step(observation(at(10010)), cast_events, at(10010));
    INFO("voice-cast notifications remain silent and do not create trigger output");
    CHECK(!cast_frame.left.has_value());
    CHECK(!cast_frame.right.has_value());
    CHECK(!cast_frame.rumble.has_value());
    CHECK(!cast_frame.body.body_transient.has_value());
}

} // namespace skyrim_adapter::tests::policy
