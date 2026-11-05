#include "policy/support/policy_test_support.hpp"
#include "test_support.hpp"
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::tests::policy {

namespace {

struct FootstepCase {
    EffectId profile;
    MovementGait gait;
    bool mounted;
    bool enabled_by_default;
    std::uint32_t duration_ms;
    int priority;
};

constexpr std::array footstep_cases{
    FootstepCase{
        .profile = EffectId::footstep_walk,
        .gait = MovementGait::walking,
        .mounted = false,
        .enabled_by_default = false,
        .duration_ms = 50,
        .priority = 2,
    },
    FootstepCase{
        .profile = EffectId::footstep_run,
        .gait = MovementGait::running,
        .mounted = false,
        .enabled_by_default = false,
        .duration_ms = 55,
        .priority = 2,
    },
    FootstepCase{
        .profile = EffectId::footstep_sprint,
        .gait = MovementGait::sprinting,
        .mounted = false,
        .enabled_by_default = true,
        .duration_ms = 60,
        .priority = 2,
    },
    FootstepCase{
        .profile = EffectId::footstep_sneak,
        .gait = MovementGait::sneaking,
        .mounted = false,
        .enabled_by_default = false,
        .duration_ms = 55,
        .priority = 2,
    },
    FootstepCase{
        .profile = EffectId::mounted_walk,
        .gait = MovementGait::walking,
        .mounted = true,
        .enabled_by_default = false,
        .duration_ms = 60,
        .priority = 4,
    },
    FootstepCase{
        .profile = EffectId::mounted_run,
        .gait = MovementGait::running,
        .mounted = true,
        .enabled_by_default = false,
        .duration_ms = 65,
        .priority = 4,
    },
    FootstepCase{
        .profile = EffectId::mounted_sprint,
        .gait = MovementGait::sprinting,
        .mounted = true,
        .enabled_by_default = true,
        .duration_ms = 70,
        .priority = 4,
    },
};

EffectFrame emit_footstep(const FootstepCase& item,
    MonoTime timestamp,
    std::uint64_t sequence,
    std::optional<bool> enabled_override = std::nullopt,
    float rumble_scale = 1.0F) {
    const auto config = std::make_unique<PolicyConfig>();
    config->rumble_scale = rumble_scale;
    if (enabled_override.has_value()) {
        config->effects.body[static_cast<std::size_t>(item.profile)].enabled = *enabled_override;
    }
    const auto policy = std::make_unique<Policy>(*config);
    Observation sample = world_observation(timestamp);
    world(sample).gait = item.gait;

    FormId actor = sample.player_id;
    if (item.mounted) {
        world(sample).mounted = true;
        world(sample).mount_id = 88;
        world(sample).mount_gait = item.gait;
        actor = world(sample).mount_id;
    }

    Action event = action(EventKind::footstep, Hand::unknown, timestamp, sequence);
    event.attributed_player_id = sample.player_id;
    event.event.actor_id = actor;
    event.event.source_id = 0;
    event.attributed_mount_id = item.mounted ? actor : 0;
    event.gait = item.gait;
    event.mount_attributed = item.mounted;
    const std::array<Action, 1> events{event};
    return policy->step(sample, events, timestamp);
}

void check_step_output(const EffectFrame& frame, const FootstepCase& item) {
    INFO("enabled player and mount profiles produce a body cue");
    REQUIRE(frame.body.body_transient.has_value());
    const BodyCue& cue = require_value(frame.body.body_transient);
    INFO("enabled step profile produces a footstep cue");
    CHECK(cue.kind == BodyCueKind::footstep);
    INFO("enabled step profile preserves player or mount attribution");
    CHECK(cue.mounted == item.mounted);
    INFO("enabled step profile preserves the event gait");
    CHECK(cue.gait == item.gait);
    INFO("enabled step profile preserves its authored duration");
    CHECK(cue.duration_ms == item.duration_ms);
    INFO("enabled step profile preserves its authored priority");
    CHECK(cue.priority == item.priority);
}

} // namespace

TEST_CASE("skyrim.policy.footsteps_default_profiles", "[component][skyrim][policy]") {
    const auto policy = std::make_unique<Policy>();
    Observation walking = world_observation(at(7400));
    world(walking).gait = MovementGait::walking;
    const EffectFrame no_event = policy->step(walking, {}, at(7400));
    INFO("observed gait alone never synthesizes a footstep pulse");
    CHECK(!no_event.body.body_transient.has_value());
    CHECK(!no_event.rumble.has_value());

    std::int64_t sample_time = 7410;
    std::uint64_t sequence = 401;
    const auto defaults = std::make_unique<PolicyConfig>();
    for (const FootstepCase& item : footstep_cases) {
        INFO("policy defaults match each footstep profile's declared default");
        CHECK(defaults->effects.body[static_cast<std::size_t>(item.profile)].enabled ==
              item.enabled_by_default);
        const EffectFrame frame = emit_footstep(item, at(sample_time++), sequence++);
        if (item.enabled_by_default) {
            check_step_output(frame, item);
        } else {
            INFO("walk, run, sneak, and mounted walk/run stay silent with built-in defaults");
            CHECK(!frame.body.body_transient.has_value());
            CHECK(!frame.rumble.has_value());
        }
    }
}

TEST_CASE("skyrim.policy.footsteps_enabled_profiles", "[component][skyrim][policy]") {
    std::int64_t sample_time = 7500;
    std::uint64_t sequence = 420;
    for (const FootstepCase& item : footstep_cases) {
        const EffectFrame frame = emit_footstep(item, at(sample_time++), sequence++, true);
        check_step_output(frame, item);
    }
}

TEST_CASE("skyrim.policy.footsteps_player_classic_fallback", "[component][skyrim][policy]") {
    const EffectFrame frame = emit_footstep(footstep_cases[0], at(7600), 430, true);
    INFO("an enabled player step uses body output without occupying a hand position");
    CHECK(frame.body.body_transient.has_value());
    CHECK(!frame.left.has_value());
    CHECK(!frame.right.has_value());
    INFO("footstep classic fallback stays within the package's quiet envelope cap");
    REQUIRE(frame.rumble.has_value());
    CHECK(require_value(frame.rumble).payload.low_frequency <= 0.10F);
    CHECK(require_value(frame.rumble).payload.high_frequency <= 0.12F);
}

TEST_CASE("skyrim.policy.footsteps_mount_source_guard", "[component][skyrim][policy]") {
    const auto policy = std::make_unique<Policy>();
    Observation changed = world_observation(at(7700));
    world(changed).mounted = true;
    world(changed).mount_id = 88;
    world(changed).mount_gait = MovementGait::running;
    Action stale_mount_step = action(EventKind::footstep, Hand::unknown, at(7700), 431);
    stale_mount_step.attributed_player_id = changed.player_id;
    stale_mount_step.event.actor_id = 88;
    stale_mount_step.event.source_id = 0;
    stale_mount_step.attributed_mount_id = 88;
    stale_mount_step.gait = MovementGait::running;
    stale_mount_step.mount_attributed = true;
    world(changed).mount_id = 89;
    const std::array<Action, 1> stale_mount_events{stale_mount_step};
    const EffectFrame changed_mount = policy->step(changed, stale_mount_events, at(7700));
    INFO("a mount step is discarded when the current mount changed after capture");
    CHECK(!changed_mount.body.body_transient.has_value());
    CHECK(!changed_mount.rumble.has_value());
}

TEST_CASE("skyrim.policy.footsteps_combat_priority", "[component][skyrim][policy]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.body[static_cast<std::size_t>(EffectId::footstep_walk)].enabled = true;
    const auto combat_policy = std::make_unique<Policy>(*config);
    Observation const combat = world_observation(at(7800));
    Action player_step = action(EventKind::footstep, Hand::unknown, at(7800), 432);
    player_step.attributed_player_id = combat.player_id;
    player_step.event.actor_id = combat.player_id;
    player_step.event.source_id = 0;
    player_step.gait = MovementGait::walking;
    Action incoming = action(EventKind::hit, Hand::unknown, at(7800), 433);
    incoming.incoming = true;
    const std::array<Action, 2> overlap{player_step, incoming};
    const EffectFrame impact = combat_policy->step(combat, overlap, at(7800));
    INFO("combat impact replaces an overlapping player step");
    REQUIRE(impact.body.body_transient.has_value());
    CHECK(require_value(impact.body.body_transient).kind == BodyCueKind::incoming_hit);
    CHECK(require_value(impact.body.body_transient).priority > 4);
}

TEST_CASE("skyrim.policy.footsteps_gain_scaling", "[component][skyrim][policy]") {
    const EffectFrame zero_scale = emit_footstep(footstep_cases[0], at(7900), 434, true, 0.0F);
    INFO("zero rumble scale silences body and classic footstep output");
    REQUIRE(zero_scale.body.body_transient.has_value());
    CHECK(require_value(zero_scale.body.body_transient).scale == 0.0F);
    CHECK(
        (!zero_scale.rumble.has_value() || (zero_scale.rumble->payload.low_frequency == 0.0F &&
                                               zero_scale.rumble->payload.high_frequency == 0.0F)));

    for (const FootstepCase& item : {footstep_cases[0], footstep_cases[5]}) {
        const EffectFrame full = emit_footstep(item, at(8000), 435, true, 1.0F);
        const EffectFrame half = emit_footstep(item, at(8000), 435, true, 0.5F);
        INFO("player and mount classic output scales once while the HD cue keeps its single scale");
        REQUIRE(full.rumble.has_value());
        REQUIRE(half.rumble.has_value());
        CHECK(std::abs((require_value(half.rumble).payload.low_frequency * 2.0F) -
                       require_value(full.rumble).payload.low_frequency) < 0.000001F);
        CHECK(std::abs((require_value(half.rumble).payload.high_frequency * 2.0F) -
                       require_value(full.rumble).payload.high_frequency) < 0.000001F);
        REQUIRE(full.body.body_transient.has_value());
        REQUIRE(half.body.body_transient.has_value());
        CHECK(require_value(full.body.body_transient).scale == 1.0F);
        CHECK(require_value(half.body.body_transient).scale == 0.5F);
    }
}

} // namespace skyrim_adapter::tests::policy
