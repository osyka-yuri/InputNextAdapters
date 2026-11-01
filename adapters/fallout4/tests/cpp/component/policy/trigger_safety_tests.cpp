#include "effect_recipes.hpp"
#include "support.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>

#include <input_next_effects.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace fallout4_adapter::policy_tests {

TEST_CASE("fallout4.policy.shot_trigger_pulse_is_cleared_by_safety_state",
    "[component][fallout4][policy]") {
    struct SuppressedState {
        void (*apply)(Observation&);
    };

    constexpr std::array suppressed_states{
        SuppressedState{[](Observation& value) { value.reloading = true; }},
        SuppressedState{[](Observation& value) { value.weapon.ready = false; }},
        SuppressedState{[](Observation& value) { value.blocking = true; }},
        SuppressedState{[](Observation& value) { value.weapon.drawn = false; }},
        SuppressedState{[](Observation& value) { value.attack_trigger = TriggerSide::none; }},
        SuppressedState{[](Observation& value) { value.weapon.identity = 43; }},
    };
    for (std::size_t index = 0; index < suppressed_states.size(); ++index) {
        const auto base_ms = 14'000 + static_cast<std::int64_t>(index * 100);
        const auto policy = std::make_unique<Policy>();
        const auto observation = std::make_unique<Observation>(gameplay_at(base_ms));
        const auto shot = event(EventKind::shot, at(base_ms), 800 + index);
        const std::array event_list{shot};
        const auto fired = update_on_heap(*policy, *observation, event_list, at(base_ms));
        INFO("the safety-gate case begins with a confirmed automatic shot");
        REQUIRE((fired->right.has_value() &&
                 fired->right->payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION));

        observation->sampled_at = at(base_ms + 10);
        suppressed_states[index].apply(*observation);
        const auto suppressed =
            update_on_heap(*policy, *observation, std::span<const PolicyEvent>{}, at(base_ms + 10));
        INFO("observed reload, not-ready, blocking, sheathed, unmapped, or weapon-swap state "
             "clears the old pulse");
        CHECK((!suppressed->left.has_value() ||
               suppressed->left->payload.mode != INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION));
        CHECK((!suppressed->right.has_value() ||
               suppressed->right->payload.mode != INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION));

        observation->sampled_at = at(base_ms + 20);
        observation->reloading = false;
        observation->weapon.ready = true;
        observation->blocking = false;
        observation->weapon.drawn = true;
        observation->attack_trigger = TriggerSide::right;
        observation->weapon.identity = 42;
        const auto recovered =
            update_on_heap(*policy, *observation, std::span<const PolicyEvent>{}, at(base_ms + 20));
        INFO("restoring a valid observation does not resume a pulse that crossed a safety gate");
        REQUIRE((recovered->right.has_value() &&
                 recovered->right->payload.mode != INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION));
    }
}

TEST_CASE("fallout4.policy.trigger_mapping_and_policy_disabled", "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(5000);
    observation.attack_trigger = TriggerSide::none;
    observation.aim_trigger = TriggerSide::none;
    const auto unknown_mapping =
        policy->update(observation, std::span<const PolicyEvent>{}, at(5000));
    INFO("unknown gamepad trigger mapping leaves both adaptive triggers neutral");
    CHECK(!unknown_mapping.left.has_value());
    CHECK(!unknown_mapping.right.has_value());

    observation.sampled_at = at(5010);
    observation.weapon.family = WeaponFamily::melee;
    observation.attack_trigger = TriggerSide::right;
    const auto melee = policy->update(observation, std::span<const PolicyEvent>{}, at(5010));
    INFO("a ready melee weapon receives its mapped attack resistance profile");
    REQUIRE((melee.right.has_value() &&
             melee.right->payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK));

    const auto disabled_config = std::make_unique<PolicyConfig>();
    disabled_config->enabled = false;
    disabled_config->trigger_scale = 0.65F;
    disabled_config->body_scale = 0.65F;
    disabled_config->resource_cues = true;
    disabled_config->ui_cues = true;
    const auto disabled = std::make_unique<Policy>(*disabled_config);
    observation = gameplay_at(5020);
    observation.reloading = true;
    observation.power_armor = true;
    const auto shot = event(EventKind::shot, at(5020), 51);
    const std::array events{shot};
    const auto off = disabled->update(observation, events, at(5020));
    INFO("disabled policy emits an empty frame for every channel");
    CHECK(!off.left.has_value());
    CHECK(!off.right.has_value());
    CHECK(!off.rumble.has_value());
    CHECK(voice_count(off) == 0);
}

TEST_CASE("fallout4.policy.weapon_readiness_and_shot_pulse_suppression",
    "[component][fallout4][policy]") {
    const auto firing = std::make_unique<Policy>();
    auto observation = gameplay_at(5025);
    observation.weapon.ready = false;
    const auto shot = event(EventKind::shot, at(5025), 52);
    const std::array firing_event{shot};
    const auto firing_pulse = firing->update(observation, firing_event, at(5025));
    INFO("a confirmed shot in a not-ready tick keeps body recoil but does not drive the trigger");
    CHECK(!firing_pulse.left.has_value());
    CHECK(!firing_pulse.right.has_value());
    CHECK(cue_voice_count(firing_pulse, detail::Cue::rifle_shot) == 1);

    observation.sampled_at = at(5030);
    observation.weapon.ready = true;
    const auto recovered_shot =
        firing->update(observation, std::span<const PolicyEvent>{}, at(5030));
    INFO("readiness recovery restores held resistance without resurrecting the suppressed shot "
         "pulse");
    REQUIRE((recovered_shot.right.has_value() &&
             recovered_shot.right->payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK));
}

TEST_CASE("fallout4.policy.body_and_trigger_scale_independence", "[component][fallout4][policy]") {
    const auto no_body_settings = std::make_unique<PolicyConfig>();
    no_body_settings->enabled = true;
    no_body_settings->trigger_scale = 0.65F;
    no_body_settings->body_scale = 0.0F;
    no_body_settings->resource_cues = true;
    no_body_settings->ui_cues = false;
    const auto no_body = std::make_unique<Policy>(*no_body_settings);
    auto observation = gameplay_at(5020);
    const auto shot = event(EventKind::shot, at(5020), 51);
    const std::array events{shot};
    const auto trigger_only = no_body->update(observation, events, at(5020));
    INFO("independent body and trigger scales allow an intentional trigger-only profile");
    CHECK(trigger_only.right.has_value());
    CHECK(!trigger_only.left.has_value());
    CHECK(!trigger_only.rumble.has_value());
    CHECK(voice_count(trigger_only) == 0);

    const auto body_only_settings = std::make_unique<PolicyConfig>();
    body_only_settings->enabled = true;
    body_only_settings->trigger_scale = 0.0F;
    body_only_settings->body_scale = 0.65F;
    body_only_settings->resource_cues = true;
    body_only_settings->ui_cues = false;
    const auto body_only = std::make_unique<Policy>(*body_only_settings);
    observation = gameplay_at(5020);
    const auto body_only_frame = body_only->update(observation, events, at(5020));
    INFO("independent body and trigger scales allow an intentional body-only profile");
    CHECK(!body_only_frame.left.has_value());
    CHECK(!body_only_frame.right.has_value());
    CHECK(voice_count(body_only_frame) == 1);
}

TEST_CASE("fallout4.policy.unknown_game_mode_clears_outputs", "[component][fallout4][policy]") {
    const auto mode_guard = std::make_unique<Policy>();
    auto observation = gameplay_at(5030);
    const auto shot = event(EventKind::shot, at(5030), 52);
    const std::array valid_event{shot};
    const auto valid_mode = mode_guard->update(observation, valid_event, at(5030));
    INFO("valid gameplay shot produces a trigger pulse before the unknown-mode barrier");
    REQUIRE((valid_mode.right.has_value() &&
             valid_mode.right->payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION));

    INFO("valid gameplay shot produces body recoil before the unknown-mode barrier");
    CHECK(cue_voice_count(valid_mode, detail::Cue::rifle_shot) == 1);

    observation.sampled_at = at(5031);
    // Mode has fixed underlying type std::uint8_t; 255 is intentionally cast to test resilience against unknown runtime modes.
    observation.mode = static_cast<Mode>(255);
    const auto invalid_mode =
        mode_guard->update(observation, std::span<const PolicyEvent>{}, at(5031));
    INFO("an unknown game mode clears every output channel");
    CHECK(!invalid_mode.left.has_value());
    CHECK(!invalid_mode.right.has_value());
    CHECK(!invalid_mode.rumble.has_value());
    CHECK(voice_count(invalid_mode) == 0);
}

TEST_CASE("fallout4.policy.trigger_gating_scenarios", "[component][fallout4][policy]") {
    const auto make_trigger_candidate = [] {
        auto obs = gameplay_at(6000);
        obs.weapon.drawn = true;
        obs.weapon.ready = true;
        obs.weapon.identity = 42;
        obs.weapon.family = WeaponFamily::rifle;
        obs.attack_trigger = TriggerSide::right;
        obs.reloading = false;
        obs.blocking = false;
        return obs;
    };

    {
        const auto policy = std::make_unique<Policy>();
        const auto baseline = update_on_heap(*policy,
            make_trigger_candidate(),
            std::span<const PolicyEvent>{},
            at(6000));
        INFO("baseline gameplay observation admits trigger feedback");
        CHECK(baseline->right.has_value());
    }

    // Non-gameplay modes suppress trigger feedback.
    {
        const auto policy = std::make_unique<Policy>();
        auto vats_obs = make_trigger_candidate();
        vats_obs.mode = Mode::vats_selection;
        const auto vats_frame =
            update_on_heap(*policy, vats_obs, std::span<const PolicyEvent>{}, at(6000));
        INFO("mode != gameplay suppresses trigger feedback");
        CHECK(!vats_frame->left.has_value());
        CHECK(!vats_frame->right.has_value());

        const auto gameplay_obs = make_trigger_candidate();
        const auto gameplay_frame =
            update_on_heap(*policy, gameplay_obs, std::span<const PolicyEvent>{}, at(6001));
        INFO("returning to gameplay admits trigger feedback");
        CHECK(gameplay_frame->right.has_value());
    }

    // Unequipped weapon (zero identity) suppresses trigger feedback.
    {
        const auto policy = std::make_unique<Policy>();
        auto no_identity_obs = make_trigger_candidate();
        no_identity_obs.weapon.identity = 0;
        const auto no_identity_frame =
            update_on_heap(*policy, no_identity_obs, std::span<const PolicyEvent>{}, at(6000));
        INFO("weapon.identity == 0 suppresses trigger feedback");
        CHECK(!no_identity_frame->left.has_value());
        CHECK(!no_identity_frame->right.has_value());

        auto valid_obs = make_trigger_candidate();
        valid_obs.weapon.identity = 42;
        const auto valid_frame =
            update_on_heap(*policy, valid_obs, std::span<const PolicyEvent>{}, at(6001));
        INFO("valid weapon identity admits trigger feedback");
        CHECK(valid_frame->right.has_value());
    }

    // Sheathed weapon suppresses trigger feedback.
    {
        const auto policy = std::make_unique<Policy>();
        auto sheathed_obs = make_trigger_candidate();
        sheathed_obs.weapon.drawn = false;
        const auto sheathed_frame =
            update_on_heap(*policy, sheathed_obs, std::span<const PolicyEvent>{}, at(6000));
        INFO("weapon.drawn == false suppresses trigger feedback");
        CHECK(!sheathed_frame->left.has_value());
        CHECK(!sheathed_frame->right.has_value());

        auto drawn_obs = make_trigger_candidate();
        drawn_obs.weapon.drawn = true;
        const auto drawn_frame =
            update_on_heap(*policy, drawn_obs, std::span<const PolicyEvent>{}, at(6001));
        INFO("drawn weapon admits trigger feedback");
        CHECK(drawn_frame->right.has_value());
    }

    // Unknown weapon family stays neutral even when the synthetic readiness bit is true.
    {
        const auto policy = std::make_unique<Policy>();
        auto unknown_obs = make_trigger_candidate();
        unknown_obs.weapon.family = WeaponFamily::none;
        const auto unknown_frame =
            update_on_heap(*policy, unknown_obs, std::span<const PolicyEvent>{}, at(6000));
        INFO("unsupported weapon family suppresses trigger feedback");
        CHECK(!unknown_frame->left.has_value());
        CHECK(!unknown_frame->right.has_value());

        const auto confirmed_shot = event(EventKind::shot, at(6000), 53);
        const std::array actual_shot{confirmed_shot};
        const auto shot_frame = update_on_heap(*policy, unknown_obs, actual_shot, at(6000));
        INFO("a confirmed actual-shot pulse remains available independently of an unsupported held "
             "profile");
        REQUIRE((shot_frame->right.has_value() &&
                 shot_frame->right->payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION));

        auto rifle_obs = make_trigger_candidate();
        rifle_obs.weapon.family = WeaponFamily::rifle;
        const auto rifle_frame =
            update_on_heap(*policy, rifle_obs, std::span<const PolicyEvent>{}, at(6001));
        INFO("supported ranged weapon family admits trigger feedback");
        CHECK(rifle_frame->right.has_value());
    }
}

TEST_CASE("fallout4.policy.held_profiles_require_fresh_ready_drawn_identity_and_no_reload",
    "[component][fallout4][policy]") {
    const auto make_melee_candidate = [] {
        auto observation = gameplay_at(9000);
        observation.weapon.family = WeaponFamily::melee;
        return observation;
    };
    const auto stays_neutral = [](Observation observation, std::int64_t at_ms) {
        const auto policy = std::make_unique<Policy>();
        const auto frame = policy->update(observation, std::span<const PolicyEvent>{}, at(at_ms));
        return !frame.left.has_value() && !frame.right.has_value();
    };

    auto observation = make_melee_candidate();
    observation.weapon.ready = false;
    INFO("not-ready melee state has no held trigger profile");
    CHECK(stays_neutral(observation, 9000));

    observation = make_melee_candidate();
    observation.reloading = true;
    INFO("reload suppresses all held trigger profiles");
    CHECK(stays_neutral(observation, 9010));

    observation = make_melee_candidate();
    observation.weapon.drawn = false;
    INFO("sheathed melee state has no held trigger profile");
    CHECK(stays_neutral(observation, 9020));

    observation = make_melee_candidate();
    observation.weapon.identity = 0;
    INFO("unidentified melee state has no held trigger profile");
    CHECK(stays_neutral(observation, 9030));

    observation = make_melee_candidate();
    observation.weapon.family = static_cast<WeaponFamily>(255);
    INFO("unrecognized synthetic-ready weapon family stays neutral");
    CHECK(stays_neutral(observation, 9040));

    observation = make_melee_candidate();
    observation.mode = Mode::vats_playback;
    INFO("VATS playback releases melee held profiles");
    CHECK(stays_neutral(observation, 9050));

    observation = make_melee_candidate();
    observation.sampled_at = at(8800);
    INFO("stale melee observation releases held profiles");
    CHECK(stays_neutral(observation, 9060));
}

} // namespace fallout4_adapter::policy_tests
