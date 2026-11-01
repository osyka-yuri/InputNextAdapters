#include "effect_recipes.hpp"
#include "support.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>

#include <input_next_effects.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace fallout4_adapter::policy_tests {

TEST_CASE("fallout4.policy.automatic_shot_trigger_profiles_are_event_driven_and_bounded",
    "[component][fallout4][policy]") {
    struct AutomaticProfile {
        WeaponFamily family;
        detail::Cue body_cue;
        float start;
        float strength;
        float device_frequency;
        std::uint32_t duration_ms;
    };

    constexpr std::array profiles{
        AutomaticProfile{
            .family = WeaponFamily::pistol,
            .body_cue = detail::Cue::pistol_shot,
            .start = 0.33F,
            .strength = 0.36F,
            .device_frequency = 86.0F,
            .duration_ms = 36,
        },
        AutomaticProfile{
            .family = WeaponFamily::rifle,
            .body_cue = detail::Cue::rifle_shot,
            .start = 0.22F,
            .strength = 0.38F,
            .device_frequency = 62.0F,
            .duration_ms = 40,
        },
        AutomaticProfile{
            .family = WeaponFamily::shotgun,
            .body_cue = detail::Cue::shotgun_shot,
            .start = 0.33F,
            .strength = 0.42F,
            .device_frequency = 42.0F,
            .duration_ms = 48,
        },
        AutomaticProfile{
            .family = WeaponFamily::heavy,
            .body_cue = detail::Cue::heavy_shot,
            .start = 0.11F,
            .strength = 0.44F,
            .device_frequency = 38.0F,
            .duration_ms = 48,
        },
        AutomaticProfile{
            .family = WeaponFamily::energy,
            .body_cue = detail::Cue::energy_shot,
            .start = 0.22F,
            .strength = 0.34F,
            .device_frequency = 104.0F,
            .duration_ms = 32,
        },
        AutomaticProfile{
            .family = WeaponFamily::generic_firearm,
            .body_cue = detail::Cue::generic_shot,
            .start = 0.33F,
            .strength = 0.36F,
            .device_frequency = 70.0F,
            .duration_ms = 40,
        },
    };

    for (std::size_t index = 0; index < profiles.size(); ++index) {
        const auto& expected = profiles[index];
        const auto base_ms = 12'000 + static_cast<std::int64_t>(index * 100);
        const auto observation = std::make_unique<Observation>(gameplay_at(base_ms));
        observation->weapon.family = expected.family;
        observation->weapon.automatic = true;

        const auto policy = std::make_unique<Policy>();
        const auto idle =
            update_on_heap(*policy, *observation, std::span<const PolicyEvent>{}, at(base_ms));
        INFO("automatic held resistance alone does not invent shot feedback or body recoil");
        REQUIRE(idle->right.has_value());
        CHECK(checked_optional(idle->right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
        CHECK(!idle->rumble.has_value());
        CHECK(voice_count(*idle) == 0);

        auto actual_shot = event(EventKind::shot, at(base_ms), 600 + index);
        const std::array shot_event{actual_shot};
        const auto fired = update_on_heap(*policy, *observation, shot_event, at(base_ms));
        INFO("each firearm family gives a bounded, nonzero-level trigger accent only for its "
             "actual shot");
        REQUIRE(fired->right.has_value());
        const auto& fired_right = checked_optional(fired->right);
        CHECK(fired_right.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
        CHECK(std::abs(fired_right.payload.start - expected.start) < 0.0001F);
        CHECK(std::abs(fired_right.payload.strength - (expected.strength * 0.65F)) < 0.0001F);
        CHECK(std::abs(fired_right.payload.frequency - expected.device_frequency) < 0.0001F);
        CHECK(fired_right.ttl_ms == expected.duration_ms);
        CHECK(quantized_field(fired_right.payload.strength, 8) == 2);

        INFO("trigger accent preserves one independently authored body recoil for the actual shot");
        CHECK(cue_voice_count(*fired, expected.body_cue) == 1);
        CHECK(voice_count(*fired) == 1);

        observation->sampled_at = at(base_ms + 10);
        const auto refreshed =
            update_on_heap(*policy, *observation, std::span<const PolicyEvent>{}, at(base_ms + 10));
        INFO(
            "an output refresh uses only the original shot deadline and does not extend its pulse");
        REQUIRE(refreshed->right.has_value());
        CHECK(checked_optional(refreshed->right).payload.mode ==
              INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
        CHECK(checked_optional(refreshed->right).ttl_ms == expected.duration_ms - 10);

        const auto submillisecond_remainder = at(base_ms) +
                                              std::chrono::milliseconds{expected.duration_ms} -
                                              std::chrono::microseconds{500};
        observation->sampled_at = submillisecond_remainder;
        const auto almost_expired = update_on_heap(*policy,
            *observation,
            std::span<const PolicyEvent>{},
            submillisecond_remainder);
        INFO("integer-millisecond output rounding cannot carry the pulse past its original "
             "deadline");
        REQUIRE(almost_expired->right.has_value());
        CHECK(checked_optional(almost_expired->right).payload.mode !=
              INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);

        const auto expiry_ms = base_ms + static_cast<std::int64_t>(expected.duration_ms);
        observation->sampled_at = at(expiry_ms);
        const auto expired =
            update_on_heap(*policy, *observation, std::span<const PolicyEvent>{}, at(expiry_ms));
        INFO("the automatic trigger accent is absent exactly at its timestamp-relative expiry");
        REQUIRE(expired->right.has_value());
        CHECK(
            checked_optional(expired->right).payload.mode != INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);

        observation->sampled_at = at(expiry_ms + 1);
        actual_shot.arrived_at = at(base_ms);
        const std::array old_shot{actual_shot};
        const auto old_event = update_on_heap(*policy, *observation, old_shot, at(expiry_ms + 1));
        INFO("a replayed expired shot event cannot restart the trigger accent");
        REQUIRE(old_event->right.has_value());
        CHECK(checked_optional(old_event->right).payload.mode !=
              INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    }
}

TEST_CASE("fallout4.policy.firearm_shot_fallbacks_and_scale_capping",
    "[component][fallout4][policy]") {
    const auto semi_automatic = std::make_unique<Policy>();
    auto semi = gameplay_at(13'000);
    semi.weapon.automatic = false;
    const auto semi_shot = event(EventKind::shot, at(13'000), 700);
    const std::array semi_event{semi_shot};
    const auto semi_frame = update_on_heap(*semi_automatic, semi, semi_event, at(13'000));
    INFO("semi-automatic firearms use the common shot pulse");
    REQUIRE(semi_frame->right.has_value());
    const auto& semi_trigger = checked_optional(semi_frame->right);
    CHECK(semi_trigger.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(semi_trigger.payload.start == 0.0F);
    CHECK(std::abs(semi_trigger.payload.strength - (0.20F * 0.65F)) < 0.0001F);
    CHECK(semi_trigger.payload.frequency == 76.0F);
    CHECK(semi_trigger.ttl_ms == 48);

    const auto unknown_family = std::make_unique<Policy>();
    auto unknown = gameplay_at(13'010);
    unknown.weapon.family = WeaponFamily::none;
    const auto unknown_shot = event(EventKind::shot, at(13'010), 701);
    const std::array unknown_event{unknown_shot};
    const auto fallback = update_on_heap(*unknown_family, unknown, unknown_event, at(13'010));
    INFO("unknown firearm families use the actual-shot fallback");
    REQUIRE(fallback->right.has_value());
    const auto& fallback_right = checked_optional(fallback->right);
    CHECK(fallback_right.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(fallback_right.payload.start == 0.0F);
    CHECK(fallback_right.payload.frequency == 76.0F);

    const auto melee_fallback = std::make_unique<Policy>();
    auto melee = gameplay_at(13'020);
    melee.weapon.family = WeaponFamily::melee;
    const auto melee_shot = event(EventKind::shot, at(13'020), 702);
    const std::array melee_event{melee_shot};
    const auto melee_frame = update_on_heap(*melee_fallback, melee, melee_event, at(13'020));
    INFO("a confirmed shot with a melee-family observation also retains the old fallback");
    REQUIRE(melee_frame->right.has_value());
    const auto& melee_right = checked_optional(melee_frame->right);
    CHECK(melee_right.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(melee_right.payload.start == 0.0F);
    CHECK(std::abs(melee_right.payload.strength - (0.20F * 0.65F)) < 0.0001F);
    CHECK(melee_right.ttl_ms == 48);

    const auto maximum_scale_config = std::make_unique<PolicyConfig>();
    maximum_scale_config->enabled = true;
    maximum_scale_config->trigger_scale = 1.0F;
    maximum_scale_config->body_scale = 0.65F;
    maximum_scale_config->resource_cues = true;
    maximum_scale_config->ui_cues = false;
    const auto maximum_scale = std::make_unique<Policy>(*maximum_scale_config);
    auto heavy = gameplay_at(13'030);
    heavy.weapon.family = WeaponFamily::heavy;
    const auto heavy_shot = event(EventKind::shot, at(13'030), 703);
    const std::array heavy_event{heavy_shot};
    const auto maximum = update_on_heap(*maximum_scale, heavy, heavy_event, at(13'030));
    INFO("automatic shot trigger amplitude stays within its absolute cap at maximum user scale");
    REQUIRE(maximum->right.has_value());
    const auto& maximum_right = checked_optional(maximum->right);
    CHECK(maximum_right.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(maximum_right.payload.strength == 0.44F);
    CHECK(maximum_right.payload.strength <= 0.44F);
}

TEST_CASE("fallout4.policy.special_weapon_profiles_cover_actual_shots_and_held_triggers",
    "[component][fallout4][policy]") {
    struct Profile {
        WeaponFamily family;
        detail::Cue body_cue;
        float start;
        float strength;
        float device_code;
        std::uint32_t duration_ms;
        std::uint32_t body_duration_ms;
        float body_frequency;
        float body_level;
    };

    constexpr std::array profiles{
        Profile{
            .family = WeaponFamily::rotary,
            .body_cue = detail::Cue::rotary_shot,
            .start = 0.22F,
            .strength = 0.32F,
            .device_code = 68.0F,
            .duration_ms = 24,
            .body_duration_ms = 32,
            .body_frequency = 70.0F,
            .body_level = 0.28F,
        },
        Profile{
            .family = WeaponFamily::flame,
            .body_cue = detail::Cue::flame_shot,
            .start = 0.11F,
            .strength = 0.24F,
            .device_code = 32.0F,
            .duration_ms = 36,
            .body_duration_ms = 40,
            .body_frequency = 42.0F,
            .body_level = 0.22F,
        },
        Profile{
            .family = WeaponFamily::cryo,
            .body_cue = detail::Cue::cryo_shot,
            .start = 0.22F,
            .strength = 0.22F,
            .device_code = 110.0F,
            .duration_ms = 32,
            .body_duration_ms = 36,
            .body_frequency = 150.0F,
            .body_level = 0.18F,
        },
        Profile{
            .family = WeaponFamily::launcher,
            .body_cue = detail::Cue::launcher_shot,
            .start = 0.44F,
            .strength = 0.44F,
            .device_code = 30.0F,
            .duration_ms = 48,
            .body_duration_ms = 140,
            .body_frequency = 44.0F,
            .body_level = 0.72F,
        },
    };

    for (std::size_t index = 0; index < profiles.size(); ++index) {
        const auto& expected = profiles[index];
        const auto base_ms = 13'500 + static_cast<std::int64_t>(index * 100);
        auto observation = gameplay_at(base_ms);
        observation.weapon.family = expected.family;
        observation.weapon.automatic = false;
        const auto policy = std::make_unique<Policy>();

        const auto held = policy->update(observation, std::span<const PolicyEvent>{}, at(base_ms));
        INFO("each special weapon has its own held trigger resistance without fabricated firing "
             "feedback");
        REQUIRE(held.right.has_value());
        CHECK(checked_optional(held.right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
        CHECK(std::abs(checked_optional(held.right).payload.start -
                       detail::trigger_recipe_for(expected.family).resistance_start) < 0.0001F);
        CHECK(voice_count(held) == 0);
        CHECK(!held.rumble.has_value());

        const auto shot = event(EventKind::shot, at(base_ms), 720 + index);
        const std::array shot_event{shot};
        const auto fired = policy->update(observation, shot_event, at(base_ms));
        const auto* const body = find_voice(fired, expected.body_cue);
        INFO("special trigger accents use their authored bounded profile even for non-automatic "
             "weapons");
        REQUIRE(fired.right.has_value());
        const auto& fired_right = checked_optional(fired.right);
        CHECK(fired_right.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
        CHECK(std::abs(fired_right.payload.start - expected.start) < 0.0001F);
        CHECK(std::abs(fired_right.payload.strength - (expected.strength * 0.65F)) < 0.0001F);
        CHECK(std::abs(fired_right.payload.frequency - expected.device_code) < 0.0001F);
        CHECK(fired_right.ttl_ms == expected.duration_ms);

        INFO("each actual special shot adds its own distinct body texture and bounded decay");
        REQUIRE(body != nullptr);
        CHECK(body->duration_ms == expected.body_duration_ms);
        CHECK(std::abs(body->wave.frequency - expected.body_frequency) < 0.0001F);
        CHECK(std::abs(body->wave.level - expected.body_level) < 0.0001F);
        CHECK(body->priority >= 48);
        CHECK(voice_count(fired) == 1);
    }
}

TEST_CASE("fallout4.policy.repeated_automatic_shots_refresh_pulse_from_latest_event",
    "[component][fallout4][policy]") {
    constexpr std::int64_t base_ms = 15'000;
    constexpr std::int64_t second_shot_offset_ms = 20;
    constexpr std::uint32_t pulse_duration_ms = 40;
    const auto second_deadline_ms = base_ms + second_shot_offset_ms + pulse_duration_ms;
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(base_ms);

    const auto first_shot = event(EventKind::shot, at(base_ms), 900);
    const std::array first_event{first_shot};
    auto frame = update_on_heap(*policy, observation, first_event, at(base_ms));
    INFO("the first real automatic shot starts its trigger pulse and body recoil");
    REQUIRE(frame->right.has_value());
    CHECK(checked_optional(frame->right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(checked_optional(frame->right).ttl_ms == pulse_duration_ms);
    CHECK(cue_voice_count(*frame, detail::Cue::rifle_shot) == 1);

    observation.sampled_at = at(base_ms + second_shot_offset_ms);
    const auto second_shot = event(EventKind::shot, at(base_ms + second_shot_offset_ms), 901);
    const std::array second_event{second_shot};
    frame = update_on_heap(*policy, observation, second_event, at(base_ms + second_shot_offset_ms));
    INFO("a second real shot refreshes from its own timestamp and adds a separate body recoil "
         "voice");
    REQUIRE(frame->right.has_value());
    CHECK(checked_optional(frame->right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(checked_optional(frame->right).ttl_ms == pulse_duration_ms);
    CHECK(cue_voice_count(*frame, detail::Cue::rifle_shot) == 2);

    observation.sampled_at = at(base_ms + pulse_duration_ms);
    frame = update_on_heap(*policy,
        observation,
        std::span<const PolicyEvent>{},
        at(base_ms + pulse_duration_ms));
    INFO("the first pulse deadline leaves only the second shot's exact remaining time");
    REQUIRE(frame->right.has_value());
    CHECK(checked_optional(frame->right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(checked_optional(frame->right).ttl_ms == pulse_duration_ms - second_shot_offset_ms);
    CHECK(cue_voice_count(*frame, detail::Cue::rifle_shot) == 2);

    observation.sampled_at = at(second_deadline_ms);
    frame = update_on_heap(*policy,
        observation,
        std::span<const PolicyEvent>{},
        at(second_deadline_ms));
    INFO("the second shot pulse ends at its own original deadline");
    REQUIRE(frame->right.has_value());
    CHECK(checked_optional(frame->right).payload.mode != INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);

    observation.sampled_at = at(second_deadline_ms + 1);
    const auto third_shot = event(EventKind::shot, observation.sampled_at, 902);
    const std::array third_event{third_shot};
    frame = update_on_heap(*policy, observation, third_event, observation.sampled_at);
    INFO("a new real shot after full expiry starts a new trigger pulse and body recoil");
    REQUIRE(frame->right.has_value());
    CHECK(checked_optional(frame->right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(checked_optional(frame->right).ttl_ms == pulse_duration_ms);
    CHECK(cue_voice_count(*frame, detail::Cue::rifle_shot) == 3);
}

TEST_CASE("fallout4.policy.melee_profiles_and_block_mapping", "[component][fallout4][policy]") {
    constexpr std::array families{WeaponFamily::unarmed,
        WeaponFamily::melee,
        WeaponFamily::two_handed_melee};
    constexpr std::array starts{0.67F, 0.44F, 0.33F};
    constexpr std::array strengths{0.18F, 0.24F, 0.38F};

    for (std::size_t index = 0; index < families.size(); ++index) {
        const auto policy = std::make_unique<Policy>();
        auto observation = gameplay_at(7000 + (static_cast<std::int64_t>(index) * 2));
        observation.weapon.family = families[index];
        observation.weapon.automatic = true;
        observation.aiming = true;
        const auto attack =
            policy->update(observation, std::span<const PolicyEvent>{}, observation.sampled_at);
        INFO("unarmed and melee profiles use mapped attack resistance without inferring a swing or "
             "automatic scaling");
        REQUIRE(attack.right.has_value());
        const auto& attack_right = checked_optional(attack.right);
        CHECK(attack_right.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
        CHECK(std::abs(attack_right.payload.start - starts[index]) < 0.0001F);
        CHECK(std::abs(attack_right.payload.strength - (strengths[index] * 0.65F)) < 0.0001F);
        CHECK(!attack.left.has_value());
        CHECK(voice_count(attack) == 0);

        observation.sampled_at += std::chrono::milliseconds{1};
        observation.blocking = true;
        const auto block =
            policy->update(observation, std::span<const PolicyEvent>{}, observation.sampled_at);
        INFO("confirmed melee blocking releases attack resistance and uses the distinct mapped "
             "secondary trigger");
        CHECK(!block.right.has_value());
        REQUIRE(block.left.has_value());
        const auto& block_left = checked_optional(block.left);
        CHECK(block_left.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
        CHECK(std::abs(block_left.payload.start - 0.33F) < 0.0001F);
        CHECK(std::abs(block_left.payload.strength - (0.36F * 0.65F)) < 0.0001F);
        CHECK(quantized_field(block_left.payload.start, 9) == 3);
        CHECK(quantized_field(block_left.payload.strength, 8) == 2);
        CHECK(voice_count(block) == 0);
    }

    const auto same_mapping = std::make_unique<Policy>();
    auto same_side = gameplay_at(7010);
    same_side.weapon.family = WeaponFamily::melee;
    same_side.attack_trigger = TriggerSide::right;
    same_side.aim_trigger = TriggerSide::right;
    same_side.blocking = true;
    const auto no_distinct_side =
        same_mapping->update(same_side, std::span<const PolicyEvent>{}, at(7010));
    INFO("melee blocking stays neutral when SecondaryAttack does not map to a distinct trigger");
    CHECK(!no_distinct_side.right.has_value());
    CHECK(!no_distinct_side.left.has_value());

    const auto held_only = std::make_unique<Policy>();
    auto held_melee = gameplay_at(7020);
    held_melee.weapon.family = WeaponFamily::two_handed_melee;
    const auto no_contact = held_only->update(held_melee, std::span<const PolicyEvent>{}, at(7020));
    INFO("held melee selection alone never creates a body contact cue");
    CHECK(voice_count(no_contact) == 0);
}

TEST_CASE("fallout4.policy.default_profiles_survive_controller_quantization",
    "[component][fallout4][policy]") {
    struct ExpectedProfile {
        WeaponFamily family;
        float start;
        float strength;
        int zone;
        int force;
        bool ranged;
    };

    constexpr std::array profiles{
        ExpectedProfile{.family = WeaponFamily::pistol,
            .start = 0.67F,
            .strength = 0.24F,
            .zone = 6,
            .force = 1,
            .ranged = true},
        ExpectedProfile{.family = WeaponFamily::rifle,
            .start = 0.56F,
            .strength = 0.34F,
            .zone = 5,
            .force = 2,
            .ranged = true},
        ExpectedProfile{.family = WeaponFamily::shotgun,
            .start = 0.76F,
            .strength = 0.44F,
            .zone = 7,
            .force = 2,
            .ranged = true},
        ExpectedProfile{.family = WeaponFamily::heavy,
            .start = 0.44F,
            .strength = 0.46F,
            .zone = 4,
            .force = 2,
            .ranged = true},
        ExpectedProfile{.family = WeaponFamily::energy,
            .start = 0.33F,
            .strength = 0.24F,
            .zone = 3,
            .force = 1,
            .ranged = true},
        ExpectedProfile{
            .family = WeaponFamily::generic_firearm,
            .start = 0.62F,
            .strength = 0.24F,
            .zone = 6,
            .force = 1,
            .ranged = true,
        },
        ExpectedProfile{.family = WeaponFamily::unarmed,
            .start = 0.67F,
            .strength = 0.18F,
            .zone = 6,
            .force = 1,
            .ranged = false},
        ExpectedProfile{.family = WeaponFamily::melee,
            .start = 0.44F,
            .strength = 0.24F,
            .zone = 4,
            .force = 1,
            .ranged = false},
        ExpectedProfile{
            .family = WeaponFamily::two_handed_melee,
            .start = 0.33F,
            .strength = 0.38F,
            .zone = 3,
            .force = 2,
            .ranged = false,
        },
    };

    for (std::size_t index = 0; index < profiles.size(); ++index) {
        const auto& expected = profiles[index];
        for (const bool automatic : std::array{false, true}) {
            const auto policy = std::make_unique<Policy>();
            auto observation =
                gameplay_at(8000 + static_cast<std::int64_t>(index * 2) + (automatic ? 1 : 0));
            observation.weapon.family = expected.family;
            observation.weapon.automatic = automatic;
            const auto frame =
                policy->update(observation, std::span<const PolicyEvent>{}, observation.sampled_at);
            const float automatic_scale = expected.ranged && automatic ? 0.90F : 1.0F;
            const float scaled_strength = expected.strength * 0.65F * automatic_scale;

            INFO("named family keeps its authored semi and automatic held profile");
            REQUIRE(frame.right.has_value());
            const auto& frame_right = checked_optional(frame.right);
            CHECK(frame_right.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
            CHECK(std::abs(frame_right.payload.start - expected.start) < 0.0001F);
            CHECK(std::abs(frame_right.payload.strength - scaled_strength) < 0.0001F);

            INFO("default-scale held profile retains its intended discrete controller zone and "
                 "force level");
            CHECK(quantized_field(frame_right.payload.start, 9) == expected.zone);
            CHECK(quantized_field(frame_right.payload.strength, 8) == expected.force);

            INFO("held trigger resistance alone has no body impact cue");
            CHECK(voice_count(frame) == 0);
        }
    }

    const auto ranged_aim = std::make_unique<Policy>();
    auto aiming = gameplay_at(8050);
    aiming.weapon.family = WeaponFamily::pistol;
    aiming.aiming = true;
    const auto aim_frame = ranged_aim->update(aiming, std::span<const PolicyEvent>{}, at(8050));
    INFO("ranged aim keeps its distinct zone-eight level-one secondary profile");
    REQUIRE(aim_frame.left.has_value());
    const auto& aim_left = checked_optional(aim_frame.left);
    CHECK(std::abs(aim_left.payload.start - 0.84F) < 0.0001F);
    CHECK(std::abs(aim_left.payload.strength - (0.14F * 0.65F)) < 0.0001F);
    CHECK(quantized_field(aim_left.payload.start, 9) == 8);
    CHECK(quantized_field(aim_left.payload.strength, 8) == 1);

    const auto low_scale_config = std::make_unique<PolicyConfig>();
    low_scale_config->enabled = true;
    low_scale_config->trigger_scale = 0.01F;
    low_scale_config->body_scale = 0.65F;
    low_scale_config->resource_cues = true;
    low_scale_config->ui_cues = false;
    const auto low_scale = std::make_unique<Policy>(*low_scale_config);
    auto low = gameplay_at(8060);
    low.weapon.family = WeaponFamily::rifle;
    low.weapon.automatic = false;
    const auto low_frame = low_scale->update(low, std::span<const PolicyEvent>{}, at(8060));
    INFO("a low user scale may quantize to zero without a minimum-force clamp");
    REQUIRE(low_frame.right.has_value());
    CHECK(checked_optional(low_frame.right).payload.strength == 0.34F * 0.01F);
    CHECK(quantized_field(checked_optional(low_frame.right).payload.strength, 8) == 0);
}

} // namespace fallout4_adapter::policy_tests
