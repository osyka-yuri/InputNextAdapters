#include "support.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>

#include <input_next_effects.h>

#include <array>
#include <cstddef>
#include <memory>
#include <span>

namespace fallout4_adapter::policy_tests {

namespace {
void check_confirmed_gameplay_shot(Policy& policy, Observation& observation) {
    observation.aiming = true;
    const auto shot = event(EventKind::shot, at(1000), 1);
    const std::array events{shot};
    const auto fired = policy.update(observation, events, at(1004));

    INFO("a confirmed live shot gives a short pulse on the mapped attack trigger");
    REQUIRE(fired.right.has_value());
    const auto& fired_right = checked_optional(fired.right);
    CHECK(fired_right.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);

    INFO("shot trigger pulse stays mild and finite");
    CHECK(fired_right.payload.strength <= 0.26F);
    CHECK(fired_right.ttl_ms <= 48);

    INFO("aim resistance follows the mapped aim trigger independently");
    REQUIRE(fired.left.has_value());
    CHECK(checked_optional(fired.left).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);

    INFO("a real shot emits one HD body voice and an explicit classic fallback");
    CHECK(voice_count(fired) == 1);
    CHECK(fired.rumble.has_value());
}

void check_vats_entry_clears_prior_combat(Policy& policy, Observation& observation) {
    observation.sampled_at = at(1010);
    observation.mode = Mode::vats_selection;
    const auto no_queue_effect =
        policy.update(observation, std::span<const PolicyEvent>{}, at(1010));
    INFO("entering VATS selection clears prior combat state and keeps both triggers neutral");
    CHECK(!no_queue_effect.left.has_value());
    CHECK(!no_queue_effect.right.has_value());
    CHECK(voice_count(no_queue_effect) == 0);
}

void check_vats_selection_ignores_planned_shots(Policy& policy, Observation& observation) {
    const auto queued_but_not_executed = event(EventKind::shot, at(1020), 2);
    observation.sampled_at = at(1020);
    const std::array queued{queued_but_not_executed};
    const auto selection = policy.update(observation, queued, at(1020));
    INFO("VATS target selection never treats a planned action as a fired shot");
    CHECK(!selection.left.has_value());
    CHECK(!selection.right.has_value());
    CHECK(voice_count(selection) == 0);
}

void check_incoming_hit_during_vats_selection(Policy& policy, Observation& observation) {
    const auto incoming = event(EventKind::hit_received, at(1030), 3);
    observation.sampled_at = at(1030);
    const std::array incoming_event{incoming};
    const auto selected_hit = policy.update(observation, incoming_event, at(1030));
    INFO("an actual incoming hit may be felt during real-time VATS selection");
    CHECK(!selected_hit.left.has_value());
    CHECK(!selected_hit.right.has_value());
    CHECK(voice_count(selected_hit) == 1);
}

void check_vats_playback_transition_is_neutral(Policy& policy, Observation& observation) {
    observation.sampled_at = at(1040);
    observation.mode = Mode::vats_playback;
    const auto playback_idle = policy.update(observation, std::span<const PolicyEvent>{}, at(1040));
    INFO("VATS playback begins without replaying selection or queued recoil");
    CHECK(!playback_idle.left.has_value());
    CHECK(!playback_idle.right.has_value());
    CHECK(voice_count(playback_idle) == 0);
}

void check_confirmed_vats_playback_shot(Policy& policy, Observation& observation) {
    const auto actual_playback_shot = event(EventKind::shot, at(1050), 4);
    observation.sampled_at = at(1050);
    const std::array actual{actual_playback_shot};
    const auto playback = policy.update(observation, actual, at(1050));
    INFO("VATS playback admits the native confirmed shot to body haptics while triggers stay free");
    CHECK(!playback.left.has_value());
    CHECK(!playback.right.has_value());
    CHECK(voice_count(playback) == 1);
}
} // namespace

TEST_CASE("fallout4.policy.gameplay_and_vats_policy", "[component][fallout4][policy]") {
    Policy policy{};
    auto observation = gameplay_at(1004);

    check_confirmed_gameplay_shot(policy, observation);
    check_vats_entry_clears_prior_combat(policy, observation);
    check_vats_selection_ignores_planned_shots(policy, observation);
    check_incoming_hit_during_vats_selection(policy, observation);
    check_vats_playback_transition_is_neutral(policy, observation);
    check_confirmed_vats_playback_shot(policy, observation);
}

namespace {
void prime_weapon_identity(Policy& policy, Observation& observation) {
    observation.weapon.identity = 41;
    static_cast<void>(policy.update(observation, std::span<const PolicyEvent>{}, at(1990)));
}

void check_weapon_swap_rejects_previous_weapon_shot(Policy& policy, Observation& observation) {
    auto old_shot = event(EventKind::shot, at(1990), 11);
    old_shot.weapon_identity = 41;
    observation.weapon.identity = 42;
    const std::array stale_weapon{old_shot};
    const auto swapped = policy.update(observation, stale_weapon, at(2000));
    INFO("a shot attributed to a previous weapon instance is rejected after an equip change");
    CHECK(voice_count(swapped) == 0);
    CHECK((!swapped.right.has_value() ||
           swapped.right->payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK));
}

void check_future_event_is_rejected(Policy& policy, const Observation& observation) {
    const auto future = event(EventKind::shot, at(2001), 12);
    const std::array future_event{future};
    const auto future_rejected = policy.update(observation, future_event, at(2000));
    INFO("an event timestamped after policy time cannot create output");
    CHECK(voice_count(future_rejected) == 0);
}

void check_stale_event_is_rejected(Policy& policy, const Observation& observation) {
    const auto too_old = event(EventKind::shot, at(1849), 13);
    const std::array old_event{too_old};
    const auto old_rejected = policy.update(observation, old_event, at(2000));
    INFO("events older than the 150 ms real-time freshness window are discarded");
    CHECK(voice_count(old_rejected) == 0);
}

void check_stale_observation_clears_output(Policy& policy, Observation& observation) {
    observation.sampled_at = at(1800);
    const auto stale_observation =
        policy.update(observation, std::span<const PolicyEvent>{}, at(2000));
    INFO("stale observation clears all output instead of holding an old trigger profile");
    CHECK(voice_count(stale_observation) == 0);
    CHECK(!stale_observation.left.has_value());
    CHECK(!stale_observation.right.has_value());
}

void check_future_observation_fails_closed(Policy& policy, Observation& observation) {
    observation.sampled_at = at(2001);
    const auto future_observation =
        policy.update(observation, std::span<const PolicyEvent>{}, at(2000));
    INFO("future observation timestamps fail closed");
    CHECK(voice_count(future_observation) == 0);
    CHECK(!future_observation.right.has_value());
}

void check_freshness_and_weapon_swap() {
    Policy policy{};
    auto observation = gameplay_at(1990);
    prime_weapon_identity(policy, observation);

    observation = gameplay_at(2000);
    check_weapon_swap_rejects_previous_weapon_shot(policy, observation);
    check_future_event_is_rejected(policy, observation);
    check_stale_event_is_rejected(policy, observation);
    check_stale_observation_clears_output(policy, observation);
    check_future_observation_fails_closed(policy, observation);
}
} // namespace

TEST_CASE("fallout4.policy.freshness_and_weapon_swap", "[component][fallout4][policy]") {
    check_freshness_and_weapon_swap();
}

TEST_CASE("fallout4.policy.default_constructor_matches_default_config_construction",
    "[component][fallout4][policy]") {
    const auto policy_a = std::make_unique<Policy>();
    const auto default_config = std::make_unique<PolicyConfig>();
    const auto policy_b = std::make_unique<Policy>(*default_config);

    auto obs = gameplay_at(1000);
    obs.aiming = true;
    obs.health_ratio = 0.75F;
    auto shot = event(EventKind::shot, at(1000), 1);
    shot.weapon_identity = 99U;
    const std::array events{shot};

    const auto frame_a = policy_a->update(obs, events, at(1004));
    const auto frame_b = policy_b->update(obs, events, at(1004));

    INFO("trigger modes and parameters match between both constructor paths");
    CHECK(frame_a.left.has_value() == frame_b.left.has_value());
    if (frame_a.left.has_value() && frame_b.left.has_value()) {
        CHECK(checked_optional(frame_a.left).payload.mode ==
              checked_optional(frame_b.left).payload.mode);
        CHECK(checked_optional(frame_a.left).payload.strength ==
              checked_optional(frame_b.left).payload.strength);
    }

    CHECK(frame_a.right.has_value() == frame_b.right.has_value());
    if (frame_a.right.has_value() && frame_b.right.has_value()) {
        CHECK(checked_optional(frame_a.right).payload.mode ==
              checked_optional(frame_b.right).payload.mode);
        CHECK(checked_optional(frame_a.right).payload.strength ==
              checked_optional(frame_b.right).payload.strength);
    }

    INFO("voice count and rumble output match between both constructor paths");
    CHECK(voice_count(frame_a) == voice_count(frame_b));
    for (std::size_t i = 0; i < frame_a.body.voices.size(); ++i) {
        const auto& voice_a = frame_a.body.voices[i];
        const auto& voice_b = frame_b.body.voices[i];
        CHECK(voice_a.has_value() == voice_b.has_value());
        if (voice_a.has_value() && voice_b.has_value()) {
            CHECK(voice_a->source_id == voice_b->source_id);
            CHECK(voice_a->priority == voice_b->priority);
            CHECK(voice_a->scale == voice_b->scale);
            CHECK(voice_a->hd_scale == voice_b->hd_scale);
            CHECK(voice_a->duration_scale == voice_b->duration_scale);
            CHECK(voice_a->wave == voice_b->wave);
        }
    }
    CHECK(frame_a.rumble.has_value() == frame_b.rumble.has_value());
    if (frame_a.rumble.has_value() && frame_b.rumble.has_value()) {
        CHECK(checked_optional(frame_a.rumble).payload.low_frequency ==
              checked_optional(frame_b.rumble).payload.low_frequency);
        CHECK(checked_optional(frame_a.rumble).payload.high_frequency ==
              checked_optional(frame_b.rumble).payload.high_frequency);
    }

    INFO("led intent matches between both constructor paths");
    CHECK(frame_a.leds.has_value() == frame_b.leds.has_value());
    if (frame_a.leds.has_value() && frame_b.leds.has_value()) {
        CHECK(checked_optional(frame_a.leds).payload.lightbar_rgb ==
              checked_optional(frame_b.leds).payload.lightbar_rgb);
    }
}

} // namespace fallout4_adapter::policy_tests
