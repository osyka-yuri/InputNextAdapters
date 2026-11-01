#include "effect_recipes.hpp"
#include "support.hpp"

#include <fallout4_adapter/effect_settings.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace fallout4_adapter::policy_tests {
namespace {
Observation position(MonoTime time, float height, bool airborne) {
    auto value = gameplay_at(0);
    value.sampled_at = time;
    value.landing = LandingObservation{.player_id = 1U,
        .cell_id = 2U,
        .x = 0.0F,
        .y = 0.0F,
        .z = height,
        .airborne = airborne};
    return value;
}

struct LandingResult {
    bool emitted{};
    std::size_t voice_count{};
    float scale{};
    std::uint32_t duration_ms{};
    bool rumble{};
};

LandingResult land_from(float apex, const PolicyConfig* config = nullptr) {
    const auto policy =
        config == nullptr ? std::make_unique<Policy>() : std::make_unique<Policy>(*config);
    auto value = position(at(1000), apex, true);
    static_cast<void>(policy->update(value, {}, value.sampled_at));
    std::int64_t time = 1040;
    for (std::int64_t descent = 400; static_cast<float>(descent) < apex; descent += 400) {
        const float height = apex - static_cast<float>(descent);
        value = position(at(time), height, true);
        static_cast<void>(policy->update(value, {}, value.sampled_at));
        time += 40;
    }
    value = position(at(time), 0.0F, false);
    const auto frame = policy->update(value, {}, value.sampled_at);
    const auto* voice = find_voice(frame, detail::Cue::landing);
    return LandingResult{.emitted = voice != nullptr,
        .voice_count = voice_count(frame),
        .scale = voice == nullptr ? 0.0F : voice->scale,
        .duration_ms = voice == nullptr ? 0U : voice->duration_ms,
        .rumble = frame.rumble.has_value()};
}
} // namespace

TEST_CASE("fallout4.policy.same_level_jump_uses_apex_and_emits_once",
    "[component][fallout4][policy][landing]") {
    for (const bool armor : std::array{false, true}) {
        CAPTURE(armor);
        const auto policy = std::make_unique<Policy>();
        for (const auto& sample : std::array{position(at(1000), 0.0F, false),
                 position(at(1040), 20.0F, true),
                 position(at(1080), 180.0F, true),
                 position(at(1120), 100.0F, true)}) {
            auto value = sample;
            value.power_armor = armor;
            const auto frame = policy->update(value, {}, value.sampled_at);
            CHECK(cue_voice_count(frame, detail::Cue::landing) == 0U);
        }
        auto grounded = position(at(1160), 0.0F, false);
        grounded.power_armor = armor;
        const auto frame = policy->update(grounded, {}, grounded.sampled_at);
        const auto* voice = find_voice(frame, detail::Cue::landing);
        REQUIRE(voice != nullptr);
        CHECK(voice->duration_ms == 98U);
        CHECK(std::abs(voice->scale - (0.65F * 0.66651785F)) < 1.0e-6F);
        CHECK(voice->started_at == grounded.sampled_at);
        const auto sequence = voice->sequence;
        const auto duplicate = policy->update(grounded, {}, grounded.sampled_at);
        CHECK(cue_sequence(duplicate, detail::Cue::landing) == sequence);
        grounded.sampled_at = at(1200);
        const auto held = policy->update(grounded, {}, grounded.sampled_at);
        CHECK(cue_voice_count(held, detail::Cue::landing) == 1U);
        CHECK(cue_sequence(held, detail::Cue::landing) == sequence);
        grounded.sampled_at = at(1300);
        CHECK(cue_voice_count(policy->update(grounded, {}, grounded.sampled_at),
                  detail::Cue::landing) == 0U);
    }
}

TEST_CASE("fallout4.policy.landing_strength_and_duration_follow_measured_height",
    "[component][fallout4][policy][landing]") {
    const auto low = land_from(64.0F);
    const auto medium = land_from(288.0F);
    const auto high = land_from(512.0F);
    const auto saturated = land_from(900.0F);
    REQUIRE(low.emitted);
    REQUIRE(medium.emitted);
    REQUIRE(high.emitted);
    REQUIRE(saturated.emitted);
    CHECK(low.scale < medium.scale);
    CHECK(medium.scale < high.scale);
    CHECK(high.scale == saturated.scale);
    CHECK(low.duration_ms == 80U);
    CHECK(medium.duration_ms == 115U);
    CHECK(high.duration_ms == 150U);
    CHECK(saturated.duration_ms == 150U);
    CHECK(land_from(63.0F).voice_count == 0U);

    const auto config = std::make_unique<PolicyConfig>();
    auto& adjustment = config->effects.body[static_cast<std::size_t>(BodyEffect::landing)];
    adjustment.duration_scale = 2.0F;
    const auto scaled = land_from(512.0F, config.get());
    REQUIRE(scaled.emitted);
    CHECK(scaled.duration_ms == 300U);
    adjustment.enabled = false;
    const auto disabled = land_from(512.0F, config.get());
    CHECK(disabled.voice_count == 0U);
    CHECK(!disabled.rumble);
}

TEST_CASE("fallout4.policy.landing_discards_discontinuous_or_unavailable_physics",
    "[component][fallout4][policy][landing]") {
    for (int scenario = 0; scenario < 8; ++scenario) {
        CAPTURE(scenario);
        const auto policy = std::make_unique<Policy>();
        auto value = position(at(1000), 180.0F, true);
        static_cast<void>(policy->update(value, {}, value.sampled_at));
        auto discontinuity = position(at(1040), 100.0F, true);
        auto physics = checked_optional(discontinuity.landing);
        switch (scenario) {
        case 0:
            discontinuity.landing.reset(); // Missing controller, swimming, or unsupported state.
            break;
        case 1:
            physics.z = std::numeric_limits<float>::quiet_NaN();
            break;
        case 2:
            discontinuity.mode = Mode::menu;
            break;
        case 3:
            discontinuity.mode = Mode::vats_playback;
            break;
        case 4:
            physics.player_id = 3U;
            break;
        case 5:
            physics.cell_id = 3U;
            break;
        case 6:
            physics.x = 600.0F;
            break;
        default:
            discontinuity.sampled_at = at(900); // A clock regression invalidates tracking.
            break;
        }
        if (scenario != 0) {
            discontinuity.landing = physics;
        }
        const auto interruption =
            update_on_heap(*policy, discontinuity, {}, discontinuity.sampled_at);
        CHECK(cue_voice_count(*interruption, detail::Cue::landing) == 0U);
        value = position(at(1080), 0.0F, false);
        CHECK(cue_voice_count(policy->update(value, {}, value.sampled_at), detail::Cue::landing) ==
              0U);
    }
    const auto policy = std::make_unique<Policy>();
    auto sample = position(at(1000), 180.0F, true);
    static_cast<void>(policy->update(sample, {}, sample.sampled_at));
    sample = position(at(1151), 0.0F, false);
    CHECK(
        cue_voice_count(policy->update(sample, {}, sample.sampled_at), detail::Cue::landing) == 0U);

    sample = position(at(1200), 180.0F, true);
    static_cast<void>(policy->update(sample, {}, sample.sampled_at));
    sample = position(at(1240), 0.0F, false);
    CHECK(voice_count(policy->update(sample, {}, at(1391))) == 0U);
}
} // namespace fallout4_adapter::policy_tests
