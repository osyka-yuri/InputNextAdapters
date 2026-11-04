#include "test_support.hpp"
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/world_effects.hpp>

using skyrim_adapter::test::require_value;

namespace {
using namespace skyrim_adapter;
using namespace std::chrono_literals;

WorldObservation& world(Observation& obs) {
    return test::require_world(obs);
}

WorldLocation& location(Observation& obs) {
    return test::require_location(world(obs));
}

MonoTime at(std::int64_t milliseconds) {
    return MonoTime{} + std::chrono::milliseconds{milliseconds};
}

Observation observation(std::int64_t milliseconds) {
    Observation result{};
    result.sampled_at = at(milliseconds);
    result.player_id = 1;
    result.available = true;
    result.alive = true;
    result.focused = true;
    result.menus_clear = true;
    WorldObservation world_obs{};
    world_obs.stamina_ratio = 0.5F;
    world_obs.location = WorldLocation{
        .cell_id = 10,
        .worldspace_id = 20,
        .x = 0.0F,
        .y = 0.0F,
        .z = 0.0F,
    };
    result.world = world_obs;
    return result;
}

std::optional<WorldEffect> step(WorldEffects& effects, const Observation& value) {
    return effects.step(value, 1.0F, value.sampled_at);
}

std::optional<WorldEffect>
step(WorldEffects& effects, const Observation& value, const EffectSettings& settings) {
    return effects.step(value, 1.0F, value.sampled_at, settings);
}

std::optional<WorldEffect> step(WorldEffects& effects,
    const Observation& value,
    MonoTime now,
    const EffectSettings& settings) {
    return effects.step(value, 1.0F, now, settings);
}

WorldCueKind kind_of(const std::optional<WorldEffect>& effect) {
    return effect ? effect->body.world_kind : WorldCueKind::none;
}

TEST_CASE("skyrim.world_effects.baseline_edges_and_newer_samples",
    "[component][skyrim][world_effects]") {
    WorldEffects effects;
    Observation current = observation(100);
    INFO("first world sample establishes a silent baseline");
    CHECK(!step(effects, current).has_value());

    current = observation(120);
    world(current).swimming = true;
    INFO("entering water produces no body cue");
    CHECK(!step(effects, current).has_value());

    current.sampled_at = at(120);
    INFO("a repeated sample timestamp cannot replay an edge");
    CHECK(!step(effects, current).has_value());

    current.sampled_at = at(140);
    world(current).swimming = false;
    INFO("leaving water produces no body cue");
    CHECK(!step(effects, current).has_value());
}

TEST_CASE("skyrim.world_effects.stamina_hysteresis_and_admission",
    "[component][skyrim][world_effects]") {
    WorldEffects effects;
    Observation current = observation(0);
    world(current).stamina_ratio = 0.10F;
    INFO("low starting stamina is a silent baseline");
    CHECK(!step(effects, current).has_value());

    current = observation(20);
    world(current).stamina_ratio = 0.01F;
    current.in_combat = true;
    INFO("low stamina without a 12 percent rearm does not alert");
    CHECK(!step(effects, current).has_value());

    current = observation(40);
    world(current).stamina_ratio = 0.12F;
    INFO("stamina rearms at exactly 12 percent");
    CHECK(!step(effects, current).has_value());

    current = observation(60);
    world(current).stamina_ratio = 0.02F;
    current.in_combat = true;
    INFO("combat stamina at 2 percent emits one depletion cue");
    CHECK(kind_of(step(effects, current)) == WorldCueKind::stamina_depleted);

    current = observation(80);
    world(current).stamina_ratio = 0.0F;
    INFO("held depleted stamina does not repeat");
    CHECK(!step(effects, current).has_value());

    current = observation(100);
    world(current).stamina_ratio = 0.13F;
    INFO("stamina above the rearm threshold arms one new warning");
    CHECK(!step(effects, current).has_value());

    current = observation(120);
    world(current).stamina_ratio = 0.01F;
    INFO("depletion outside combat or sprint is silent and consumes the edge");
    CHECK(!step(effects, current).has_value());

    current = observation(140);
    current.in_combat = true;
    INFO("entering combat while still depleted does not replay the old edge");
    CHECK(!step(effects, current).has_value());

    current = observation(160);
    world(current).stamina_ratio = 0.12F;
    INFO("stamina can rearm after an out-of-combat depletion");
    CHECK(!step(effects, current).has_value());

    current = observation(180);
    world(current).stamina_ratio = 0.02F;
    world(current).sprinting = true;
    INFO("sprinting stamina depletion emits one cue");
    CHECK(kind_of(step(effects, current)) == WorldCueKind::stamina_depleted);
}

TEST_CASE("skyrim.world_effects.state_edges_and_priority", "[component][skyrim][world_effects]") {
    WorldEffects effects;
    Observation current = observation(0);
    INFO("state edge test starts with a silent baseline");
    CHECK(!step(effects, current).has_value());

    current = observation(20);
    world(current).staggered = true;
    const auto stagger = step(effects, current);
    INFO("stagger entry is sampled once");
    CHECK(kind_of(stagger) == WorldCueKind::stagger_entry);

    INFO("stagger remains below held and ordinary transient body priority");
    REQUIRE(stagger.has_value());
    CHECK(require_value(stagger).body.priority == 9);
    CHECK(require_value(stagger).rumble.priority == 9);

    current = observation(40);
    world(current).staggered = true;
    INFO("continued stagger does not repeat its entry cue");
    CHECK(!step(effects, current).has_value());

    current = observation(60);
    world(current).staggered = true;
    world(current).knocked_down = true;
    world(current).killmove = true;
    const auto selected = step(effects, current);
    INFO("actual knockdown wins over simultaneous stagger and killmove edges");
    REQUIRE(selected.has_value());
    const auto& knockdown_cue = require_value(selected);
    CHECK(knockdown_cue.body.world_kind == WorldCueKind::knockdown_entry);

    INFO("knockdown can override the ordinary combat body cue priority");
    CHECK(knockdown_cue.body.priority > 55);

    INFO("knockdown retains its authored override priority");
    CHECK(knockdown_cue.body.priority == 75);
    CHECK(knockdown_cue.rumble.priority == 75);

    WorldEffects killmove_effects;
    current = observation(0);
    (void)step(killmove_effects, current);
    current = observation(20);
    world(current).killmove = true;
    const auto killmove = step(killmove_effects, current);
    INFO("killmove remains below held and ordinary transient body priority");
    REQUIRE(killmove.has_value());
    const auto& killmove_cue = require_value(killmove);
    CHECK(killmove_cue.body.world_kind == WorldCueKind::killmove_entry);
    CHECK(killmove_cue.body.priority == 9);
    CHECK(killmove_cue.rumble.priority == 9);

    current = observation(80);
    world(current).staggered = true;
    world(current).knocked_down = true;
    world(current).killmove = true;
    INFO("sustained world states do not replay one-shot cues");
    CHECK(!step(effects, current).has_value());
}

TEST_CASE("skyrim.world_effects.disabled_world_profile_does_not_hide_enabled_candidate",
    "[component][skyrim][world_effects]") {
    WorldEffects effects;
    Observation current = observation(0);
    (void)step(effects, current);

    current = observation(20);
    world(current).staggered = true;
    world(current).knocked_down = true;
    const auto settings_storage = std::make_unique<EffectSettings>();
    EffectSettings& settings = *settings_storage;
    settings.body[static_cast<std::size_t>(EffectId::knockdown)].enabled = false;
    const auto selected = step(effects, current, settings);
    INFO("disabled knockdown profile leaves the simultaneous enabled stagger cue available");
    REQUIRE(selected.has_value());
    const auto& selected_cue = require_value(selected);
    CHECK(selected_cue.body.world_kind == WorldCueKind::stagger_entry);

    INFO("the surviving lower-priority world cue keeps its paired classic fallback");
    CHECK(selected_cue.rumble.duration_ms == selected_cue.body.duration_ms);

    effects.reset();
    current = observation(0);
    (void)step(effects, current);
    current = observation(20);
    world(current).staggered = true;
    world(current).knocked_down = true;
    const auto zero_gain_storage = std::make_unique<EffectSettings>();
    EffectSettings& zero_gain = *zero_gain_storage;
    EffectAdjustment& knockdown = zero_gain.body[static_cast<std::size_t>(EffectId::knockdown)];
    knockdown.hd_scale = 0.0F;
    knockdown.classic_scale = 0.0F;
    const auto selected_after_zero = step(effects, current, zero_gain);
    INFO("a zero-gain knockdown profile cannot displace a simultaneous enabled stagger candidate");
    REQUIRE(selected_after_zero.has_value());
    CHECK(require_value(selected_after_zero).body.world_kind == WorldCueKind::stagger_entry);
}

TEST_CASE("skyrim.world_effects.tuned_world_expiry_is_checked_before_priority",
    "[component][skyrim][world_effects]") {
    WorldEffects effects;
    Observation current = observation(1000);
    INFO("world expiry test establishes its timestamped baseline");
    CHECK(!step(effects, current).has_value());

    current = observation(1100);
    world(current).staggered = true;
    world(current).knocked_down = true;
    const auto settings_storage = std::make_unique<EffectSettings>();
    EffectSettings& settings = *settings_storage;
    settings.body[static_cast<std::size_t>(EffectId::knockdown)].duration_scale = 0.25F;
    settings.body[static_cast<std::size_t>(EffectId::stagger)].duration_scale = 2.0F;

    const auto selected = step(effects, current, at(1160), settings);
    INFO("expired tuned knockdown is removed before it can hide the still-active stagger edge");
    REQUIRE(selected.has_value());
    const auto& selected_cue = require_value(selected);
    CHECK(selected_cue.body.world_kind == WorldCueKind::stagger_entry);

    INFO("world arbitration returns the authored cue unchanged for one later policy tuning pass");
    CHECK(selected_cue.body.duration_ms == 105);
    CHECK(selected_cue.rumble.duration_ms == 105);

    INFO("the sampled edge remains within the existing world freshness window");
    CHECK(current.sampled_at <= at(1160));
    CHECK(at(1160) - current.sampled_at == 60ms);
}

TEST_CASE("skyrim.world_effects.landing_requires_continuous_measured_descent",
    "[component][skyrim][world_effects]") {
    WorldEffects effects;
    Observation current = observation(0);
    world(current).airborne = true;
    location(current).z = 200.0F;
    INFO("airborne sample starts a silent descent interval");
    CHECK(!step(effects, current).has_value());

    current = observation(50);
    world(current).airborne = true;
    location(current).z = 140.0F;
    INFO("intermediate airborne samples do not infer a landing");
    CHECK(!step(effects, current).has_value());

    current = observation(100);
    world(current).airborne = false;
    location(current).z = 120.0F;
    const auto landed = step(effects, current);
    INFO("a continuous measured descent of 80 units admits a landing cue");
    REQUIRE(landed.has_value());
    const auto& landing_cue = require_value(landed);
    CHECK(landing_cue.body.world_kind == WorldCueKind::landing);

    INFO("landing duration stays bounded from observed descent");
    CHECK(landing_cue.body.duration_ms >= 85);
    CHECK(landing_cue.body.duration_ms <= 155);

    INFO("landing stays below held and ordinary transient body priority");
    CHECK(landing_cue.body.priority == 9);
    CHECK(landing_cue.rumble.priority == 9);

    effects.reset();
    current = observation(0);
    world(current).airborne = true;
    location(current).z = 100.0F;
    (void)step(effects, current);
    current = observation(100);
    location(current).z = 40.0F;
    INFO("descent below 64 units does not infer a landing");
    CHECK(!step(effects, current).has_value());

    effects.reset();
    current = observation(0);
    world(current).airborne = true;
    location(current).z = 700.0F;
    (void)step(effects, current);
    current = observation(50);
    location(current).z = 100.0F;
    INFO("bounded sample displacement rejects a teleport");
    CHECK(!step(effects, current).has_value());

    current = observation(201);
    world(current).airborne = false;
    location(current).z = 0.0F;
    INFO("post-gap ground state becomes a fresh silent baseline");
    CHECK(!step(effects, current).has_value());

    effects.reset();
    current = observation(0);
    world(current).airborne = true;
    location(current).z = 200.0F;
    (void)step(effects, current);
    current = observation(50);
    world(current).airborne = true;
    world(current).swimming = true;
    location(current).z = 100.0F;
    (void)step(effects, current);
    current = observation(100);
    location(current).z = 0.0F;
    INFO("swimming cancels the landing interval without producing another cue");
    CHECK(kind_of(step(effects, current)) != WorldCueKind::landing);

    effects.reset();
    current = observation(0);
    world(current).airborne = true;
    location(current).z = 200.0F;
    (void)step(effects, current);
    current = observation(50);
    world(current).airborne = true;
    world(current).mounted = true;
    location(current).z = 100.0F;
    (void)step(effects, current);
    current = observation(100);
    location(current).z = 0.0F;
    INFO("mounting breaks the landing interval");
    CHECK(!step(effects, current).has_value());

    effects.reset();
    current = observation(0);
    world(current).airborne = true;
    location(current).z = 200.0F;
    (void)step(effects, current);
    current = observation(50);
    world(current).airborne = true;
    world(current).location.reset();
    (void)step(effects, current);
    current = observation(100);
    world(current).airborne = false;
    location(current).z = 0.0F;
    INFO("a missing position cancels fall tracking without inferring a later landing");
    CHECK(kind_of(step(effects, current)) != WorldCueKind::landing);
}

TEST_CASE("skyrim.world_effects.landing_uses_apex_for_same_level_jumps_and_bounded_strength",
    "[component][skyrim][world_effects]") {
    WorldEffects jump_effects;
    Observation current = observation(0);
    location(current).z = 0.0F;
    CHECK(!step(jump_effects, current).has_value());

    current = observation(20);
    world(current).airborne = true;
    location(current).z = 0.0F;
    CHECK(!step(jump_effects, current).has_value());
    current = observation(60);
    world(current).airborne = true;
    location(current).z = 180.0F;
    CHECK(!step(jump_effects, current).has_value());
    current = observation(100);
    location(current).z = 0.0F;
    const auto same_level_landing = step(jump_effects, current);
    INFO("a same-height landing measures descent from the highest airborne sample");
    REQUIRE(same_level_landing.has_value());
    CHECK(require_value(same_level_landing).body.world_kind == WorldCueKind::landing);
    CHECK(require_value(same_level_landing).body.duration_ms == 103U);
    CHECK(std::abs(require_value(same_level_landing).body.scale - 0.66651785F) < 1.0e-5F);

    const auto landing_for_descent = [](float descent) {
        WorldEffects effects;
        Observation sample = observation(0);
        world(sample).airborne = true;
        location(sample).z = descent;
        static_cast<void>(step(effects, sample));
        sample = observation(50);
        location(sample).z = 0.0F;
        return step(effects, sample);
    };

    const auto minimum = landing_for_descent(64.0F);
    const auto midpoint = landing_for_descent(288.0F);
    const auto maximum = landing_for_descent(512.0F);
    REQUIRE(minimum.has_value());
    REQUIRE(midpoint.has_value());
    REQUIRE(maximum.has_value());
    const auto& minimum_cue = require_value(minimum);
    const auto& midpoint_cue = require_value(midpoint);
    const auto& maximum_cue = require_value(maximum);
    CHECK(minimum_cue.body.duration_ms == 85U);
    CHECK(midpoint_cue.body.duration_ms == 120U);
    CHECK(maximum_cue.body.duration_ms == 155U);
    CHECK(minimum_cue.body.scale == 0.55F);
    CHECK(std::abs(midpoint_cue.body.scale - 0.775F) < 1.0e-6F);
    CHECK(maximum_cue.body.scale == 1.0F);
    CHECK(minimum_cue.body.scale < midpoint_cue.body.scale);
    CHECK(midpoint_cue.body.scale < maximum_cue.body.scale);
    CHECK(minimum_cue.rumble.high < midpoint_cue.rumble.high);
    CHECK(midpoint_cue.rumble.high < maximum_cue.rumble.high);
}

TEST_CASE("skyrim.world_effects.interior_landing_keeps_cell_continuity",
    "[component][skyrim][world_effects]") {
    WorldEffects effects;
    auto current = observation(0);
    world(current).airborne = true;
    location(current).worldspace_id = 0; // Interiors have a cell without an exterior worldspace.
    location(current).z = 180.0F;
    CHECK(!step(effects, current));
    current = observation(40);
    location(current).worldspace_id = 0;
    location(current).z = 0.0F;
    CHECK(kind_of(step(effects, current)) == WorldCueKind::landing);

    effects.reset();
    current = observation(100);
    world(current).airborne = true;
    location(current).worldspace_id = 0;
    location(current).z = 180.0F;
    CHECK(!step(effects, current));
    current = observation(140);
    location(current).worldspace_id = 0;
    location(current).cell_id = 11;
    CHECK(!step(effects, current));
}

TEST_CASE("skyrim.world_effects.missing_state_and_location_discontinuities",
    "[component][skyrim][world_effects]") {
    WorldEffects effects;
    Observation current = observation(0);
    (void)step(effects, current);
    current = observation(20);
    world(current).staggered = true;
    (void)step(effects, current);

    current = observation(40);
    current.world.reset();
    INFO("missing world observation clears history");
    CHECK(!step(effects, current).has_value());

    current = observation(60);
    INFO("the sample after missing world state is a silent baseline");
    CHECK(!step(effects, current).has_value());

    current = observation(80);
    world(current).location.reset();
    world(current).staggered = true;
    INFO("missing interior location does not suppress independent state edges");
    CHECK(kind_of(step(effects, current)) == WorldCueKind::stagger_entry);

    current = observation(120);
    world(current).location.reset();
    world(current).killmove = true;
    world(current).stamina_ratio = 0.01F;
    current.in_combat = true;
    INFO("killmove entry remains available while interior location is missing");
    CHECK(kind_of(step(effects, current)) == WorldCueKind::killmove_entry);

    current = observation(140);
    world(current).location.reset();
    world(current).stamina_ratio = 0.12F;
    INFO("stamina rearms without a position sample");
    CHECK(!step(effects, current).has_value());

    current = observation(160);
    world(current).location.reset();
    world(current).stamina_ratio = 0.02F;
    current.in_combat = true;
    INFO("stamina depletion remains available while interior location is missing");
    CHECK(kind_of(step(effects, current)) == WorldCueKind::stamina_depleted);

    current = observation(180);
    world(current).location.reset();
    INFO("missing location keeps the nonlanding state baseline continuous");
    CHECK(!step(effects, current).has_value());

    current = observation(200);
    (void)step(effects, current);

    current = observation(220);
    world(current).staggered = true;
    location(current).x = std::numeric_limits<float>::quiet_NaN();
    INFO("invalid paired position becomes a silent discontinuity baseline");
    CHECK(!step(effects, current).has_value());

    current = observation(240);
    world(current).staggered = true;
    INFO("invalid position cannot bridge into a state edge");
    CHECK(!step(effects, current).has_value());

    current = observation(260);
    world(current).killmove = true;
    INFO("state edges resume after finite position samples return");
    CHECK(kind_of(step(effects, current)) == WorldCueKind::killmove_entry);

    current = observation(280);
    current.player_id = 2;
    world(current).staggered = true;
    INFO("player identity change resets the baseline");
    CHECK(!step(effects, current).has_value());

    current = observation(300);
    location(current).cell_id = 11;
    INFO("cell teleport resets the state-edge baseline");
    CHECK(!step(effects, current).has_value());

    current = observation(320);
    world(current).staggered = true;
    location(current).cell_id = 11;
    INFO("state edges resume from the teleported location baseline");
    CHECK(kind_of(step(effects, current)) == WorldCueKind::stagger_entry);

    current = observation(340);
    world(current).staggered = true;
    location(current).cell_id = 11;
    INFO("stable location does not replay the same state edge");
    CHECK(!step(effects, current).has_value());

    effects.reset();
    current = observation(0);
    (void)step(effects, current);
    current = observation(20);
    world(current).staggered = true;
    (void)step(effects, current);
    Observation stale = observation(30);
    world(stale).staggered = true;
    INFO("stale world sample clears transition history");
    CHECK(!effects.step(stale, 1.0F, at(181)).has_value());

    current = observation(200);
    INFO("sample after stale data establishes a silent baseline");
    CHECK(!step(effects, current).has_value());

    current = observation(220);
    world(current).staggered = true;
    INFO("transition edges resume after the stale-data baseline");
    CHECK(kind_of(step(effects, current)) == WorldCueKind::stagger_entry);

    effects.reset();
    current = observation(0);
    (void)step(effects, current);
    current = observation(20);
    world(current).staggered = true;
    (void)step(effects, current);
    current = observation(10);
    INFO("out-of-order world sample is discarded and clears history");
    CHECK(!step(effects, current).has_value());

    current = observation(30);
    INFO("newer sample after a clock regression is a silent baseline");
    CHECK(!step(effects, current).has_value());

    current = observation(50);
    world(current).staggered = true;
    INFO("transition edges resume after monotonic samples return");
    CHECK(kind_of(step(effects, current)) == WorldCueKind::stagger_entry);
}

} // namespace
