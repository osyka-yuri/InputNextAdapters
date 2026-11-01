#include "effect_recipes.hpp"
#include "support.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fallout4_adapter/effect_settings.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>

#include <input_next_effects.h>
#include <inputnext_adapters/body_program.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <span>
#include <vector>

namespace fallout4_adapter::policy_tests {

TEST_CASE("fallout4.policy.rapid_distinct_automatic_shots_are_bounded",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    const auto observation = gameplay_at(3006);
    std::array<PolicyEvent, 7> rapid{};
    for (std::size_t index = 0; index < rapid.size(); ++index) {
        rapid[index] =
            event(EventKind::shot, at(3000 + static_cast<std::int64_t>(index)), 100 + index);
    }
    const auto frame = update_on_heap(*policy, observation, rapid, at(3006));
    INFO("seven closely spaced automatic shots mix only the five supported body voices");
    CHECK(voice_count(*frame) == 5);

    std::array<std::int64_t, 5> starts{};
    std::size_t count{};
    for (const auto& voice : frame->body.voices) {
        if (voice) {
            starts[count++] = std::chrono::duration_cast<std::chrono::milliseconds>(
                voice->started_at - MonoTime{})
                                  .count();
        }
    }
    std::ranges::sort(starts);
    INFO("bounded arbitration retains the five newest same-priority shot events without cadence "
         "merging");
    CHECK(count == 5);
    CHECK(starts.front() == 3002);
    CHECK(starts.back() == 3006);

    INFO("overlap fallback remains finite and capped");
    REQUIRE(frame->rumble.has_value());
    const auto& overlap_rumble = checked_optional(frame->rumble);
    CHECK(overlap_rumble.ttl_ms <= effect_ttl_max_ms);
    CHECK(overlap_rumble.payload.low_frequency <= 0.78F);
    CHECK(overlap_rumble.payload.high_frequency <= 0.78F);

    const auto next = gameplay_at(3007);
    const auto refresh = update_on_heap(*policy, next, std::span<const PolicyEvent>{}, at(3007));
    INFO("short policy ticks preserve each active shot envelope rather than inventing a fixed "
         "cadence");
    CHECK(voice_count(*refresh) == 5);
}

TEST_CASE("fallout4.policy.voice_ids_survive_expiry_and_context_clears_until_full_reset",
    "[component][fallout4][policy][voice-mixer]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(10'000);
    observation.critical_count = 0;
    observation.landing = LandingObservation{.player_id = 1U,
        .cell_id = 2U,
        .x = 0.0F,
        .y = 0.0F,
        .z = 200.0F,
        .airborne = true};

    auto shot = event(EventKind::shot, at(10'000), 1);
    auto frame =
        update_on_heap(*policy, observation, std::span<const PolicyEvent>{&shot, 1}, at(10'000));
    const auto* voice = find_voice(*frame, detail::Cue::rifle_shot);
    REQUIRE(voice != nullptr);
    CHECK(voice->sequence == 1U);
    CHECK(voice->stable_id == 1U);

    observation.sampled_at = at(10'150);
    frame = update_on_heap(*policy, observation, {}, at(10'150));
    CHECK(cue_voice_count(*frame, detail::Cue::rifle_shot) == 0U);

    observation.weapon.identity = 43U;
    observation.sampled_at = at(10'160);
    shot = event(EventKind::shot, at(10'160), 2);
    shot.weapon_identity = observation.weapon.identity;
    frame =
        update_on_heap(*policy, observation, std::span<const PolicyEvent>{&shot, 1}, at(10'160));
    voice = find_voice(*frame, detail::Cue::rifle_shot);
    REQUIRE(voice != nullptr);
    CHECK(voice->sequence == 2U);
    CHECK(voice->stable_id == 2U);

    observation.mode = Mode::vats_playback;
    observation.sampled_at = at(10'170);
    shot = event(EventKind::shot, at(10'170), 3);
    shot.weapon_identity = observation.weapon.identity;
    frame =
        update_on_heap(*policy, observation, std::span<const PolicyEvent>{&shot, 1}, at(10'170));
    voice = find_voice(*frame, detail::Cue::rifle_shot);
    REQUIRE(voice != nullptr);
    CHECK(voice->sequence == 3U);
    CHECK(voice->stable_id == 3U);

    observation.mode = Mode::gameplay;
    observation.sampled_at = at(10'180);
    static_cast<void>(policy->update(observation, {}, at(10'180)));
    policy->reset_effects();

    observation.sampled_at = at(10'190);
    observation.health_ratio = 0.20F;
    observation.action_points_ratio = 0.10F;
    observation.critical_count = 1;
    observation.landing->z = 0.0F;
    observation.landing->airborne = false;
    frame = update_on_heap(*policy, observation, {}, at(10'190));
    CHECK(voice_count(*frame) == 0U);
    CHECK(cue_voice_count(*frame, detail::Cue::landing) == 0U);
    CHECK(cue_voice_count(*frame, detail::Cue::health_warning) == 0U);
    CHECK(cue_voice_count(*frame, detail::Cue::ap_warning) == 0U);
    CHECK(cue_voice_count(*frame, detail::Cue::critical_ready) == 0U);

    observation.sampled_at = at(10'200);
    shot = event(EventKind::shot, at(10'200), 4);
    shot.weapon_identity = observation.weapon.identity;
    frame =
        update_on_heap(*policy, observation, std::span<const PolicyEvent>{&shot, 1}, at(10'200));
    voice = find_voice(*frame, detail::Cue::rifle_shot);
    REQUIRE(voice != nullptr);
    CHECK(voice->sequence == 1U);
    CHECK(voice->stable_id == 1U);

    observation.sampled_at = at(10'210);
    observation.health_ratio = 0.80F;
    observation.action_points_ratio = 0.80F;
    observation.critical_count = 0;
    observation.landing->z = 200.0F;
    observation.landing->airborne = true;
    static_cast<void>(policy->update(observation, {}, at(10'210)));

    observation.sampled_at = at(10'220);
    observation.health_ratio = 0.20F;
    observation.action_points_ratio = 0.10F;
    observation.critical_count = 1;
    observation.landing->z = 0.0F;
    observation.landing->airborne = false;
    frame = update_on_heap(*policy, observation, {}, at(10'220));
    const auto* health = find_voice(*frame, detail::Cue::health_warning);
    const auto* action_points = find_voice(*frame, detail::Cue::ap_warning);
    const auto* critical = find_voice(*frame, detail::Cue::critical_ready);
    const auto* landing = find_voice(*frame, detail::Cue::landing);
    REQUIRE(health != nullptr);
    REQUIRE(action_points != nullptr);
    REQUIRE(critical != nullptr);
    REQUIRE(landing != nullptr);
    CHECK(health->sequence < action_points->sequence);
    CHECK(action_points->sequence < critical->sequence);
    CHECK(critical->sequence < landing->sequence);
}

TEST_CASE("fallout4.policy.effect_settings_tuning_and_held_resistance",
    "[component][fallout4][policy]") {
    const auto tuned = std::make_unique<PolicyConfig>();
    auto& body_settings = tuned->effects.body[static_cast<std::size_t>(BodyEffect::rotary_shot)];
    body_settings.hd_scale = 1.5F;
    body_settings.classic_scale = 0.5F;
    body_settings.duration_scale = 1.5F;
    body_settings.frequency_hz = 210.0F;
    auto& trigger_settings =
        tuned->effects.triggers[static_cast<std::size_t>(TriggerProfile::rotary)];
    trigger_settings.strength_scale = 2.0F;
    trigger_settings.resistance_start = 0.12F;
    trigger_settings.vibration_frequency_code = std::uint16_t{255};
    const auto customized = std::make_unique<Policy>(*tuned);
    auto observation = gameplay_at(13'950);
    observation.weapon.family = WeaponFamily::rotary;
    const auto shot = event(EventKind::shot, at(13'950), 750);
    const std::array events{shot};
    const auto fired = update_on_heap(*customized, observation, events, at(13'950));
    const auto* const rotary_body = find_voice(*fired, detail::Cue::rotary_shot);
    INFO("body controls scale complete lifetime and envelope timing while keeping expiry anchored "
         "to the shot");
    REQUIRE(rotary_body != nullptr);
    CHECK(rotary_body->hd_scale == 1.5F);
    CHECK(rotary_body->duration_scale == 1.5F);
    CHECK(rotary_body->duration_ms == 48);
    CHECK(rotary_body->wave.frequency == 210.0F);

    INFO("actual-shot trigger strength and device code are independent of held resistance and "
         "retain the shot deadline");
    REQUIRE(fired->right.has_value());
    const auto& fired_right = checked_optional(fired->right);
    CHECK(fired_right.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);
    CHECK(fired_right.payload.start == 0.22F);
    CHECK(fired_right.payload.frequency == 255.0F);
    CHECK(std::abs(fired_right.payload.strength - (0.32F * 0.65F * 2.0F)) < 0.0001F);
    CHECK(fired_right.ttl_ms == 24);

    observation.sampled_at = at(13'974);
    const auto held =
        update_on_heap(*customized, observation, std::span<const PolicyEvent>{}, at(13'974));
    INFO("the resistance-start override applies to held attack feedback after the actual-shot "
         "accent expires");
    REQUIRE(held->right.has_value());
    const auto& held_right = checked_optional(held->right);
    CHECK(held_right.payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK);
    CHECK(held_right.payload.start == 0.12F);
    CHECK(std::abs(held_right.payload.strength - (0.28F * 0.65F * 2.0F * 0.90F)) < 0.0001F);
}

TEST_CASE("fallout4.policy.effect_profile_toggles_and_output_independence",
    "[component][fallout4][policy]") {
    auto observation = gameplay_at(14'000);
    observation.weapon.family = WeaponFamily::rotary;
    auto shot = event(EventKind::shot, at(14'000), 751);

    const auto body_disabled = std::make_unique<PolicyConfig>();
    body_disabled->effects.body[static_cast<std::size_t>(BodyEffect::rotary_shot)].enabled = false;
    const auto silent_body = std::make_unique<Policy>(*body_disabled);
    const std::array body_disabled_shot{shot};
    const auto trigger_only = silent_body->update(observation, body_disabled_shot, at(14'000));
    INFO("disabling one body profile leaves its independent trigger profile active");
    CHECK(voice_count(trigger_only) == 0);
    CHECK(!trigger_only.rumble.has_value());
    REQUIRE(trigger_only.right.has_value());
    CHECK(
        checked_optional(trigger_only.right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);

    const auto trigger_disabled = std::make_unique<PolicyConfig>();
    trigger_disabled->effects.triggers[static_cast<std::size_t>(TriggerProfile::launcher)].enabled =
        false;
    const auto silent_launcher_trigger = std::make_unique<Policy>(*trigger_disabled);
    observation = gameplay_at(14'010);
    observation.weapon.family = WeaponFamily::launcher;
    shot = event(EventKind::shot, at(14'010), 752);
    const std::array trigger_disabled_shot{shot};
    const auto body_only =
        silent_launcher_trigger->update(observation, trigger_disabled_shot, at(14'010));
    observation.sampled_at = at(14'012);
    const auto body_only_ramp =
        silent_launcher_trigger->update(observation, std::span<const PolicyEvent>{}, at(14'012));
    INFO("disabling one trigger profile leaves its body impact active");
    CHECK(voice_count(body_only) == 1);
    CHECK(voice_count(body_only_ramp) == 1);
    CHECK(body_only_ramp.rumble.has_value());
    CHECK(!body_only_ramp.left.has_value());
    CHECK(!body_only_ramp.right.has_value());

    const auto global_body_disabled = std::make_unique<PolicyConfig>();
    global_body_disabled->effects.body_enabled = false;
    const auto no_body = std::make_unique<Policy>(*global_body_disabled);
    observation = gameplay_at(14'020);
    shot = event(EventKind::shot, at(14'020), 753);
    const std::array global_body_shot{shot};
    const auto triggers_only = no_body->update(observation, global_body_shot, at(14'020));
    INFO("the global body switch mutes body and classic fallback while retaining trigger feedback");
    CHECK(voice_count(triggers_only) == 0);
    CHECK(!triggers_only.rumble.has_value());
    CHECK(triggers_only.right.has_value());
}

TEST_CASE("fallout4.policy.effect_settings_malformed_values_fallback",
    "[component][fallout4][policy]") {
    const auto invalid = std::make_unique<PolicyConfig>();
    auto& invalid_body = invalid->effects.body[static_cast<std::size_t>(BodyEffect::rotary_shot)];
    invalid_body.hd_scale = std::numeric_limits<float>::quiet_NaN();
    invalid_body.classic_scale = std::numeric_limits<float>::infinity();
    invalid_body.duration_scale = std::numeric_limits<float>::quiet_NaN();
    invalid_body.frequency_hz = std::numeric_limits<float>::infinity();
    auto& invalid_trigger =
        invalid->effects.triggers[static_cast<std::size_t>(TriggerProfile::rotary)];
    invalid_trigger.strength_scale = std::numeric_limits<float>::quiet_NaN();
    invalid_trigger.resistance_start = std::numeric_limits<float>::infinity();
    invalid_trigger.vibration_frequency_code = std::uint16_t{0};
    const auto sanitized = std::make_unique<Policy>(*invalid);
    auto observation = gameplay_at(14'030);
    observation.weapon.family = WeaponFamily::rotary;
    const auto shot = event(EventKind::shot, at(14'030), 754);
    const std::array invalid_settings_shot{shot};
    const auto safe_defaults =
        update_on_heap(*sanitized, observation, invalid_settings_shot, at(14'030));
    const auto* const safe_voice = find_voice(*safe_defaults, detail::Cue::rotary_shot);
    INFO("malformed direct programmatic overrides safely fall back to recipe defaults");
    REQUIRE(safe_voice != nullptr);
    CHECK(safe_voice->hd_scale == 1.0F);
    CHECK(safe_voice->duration_scale == 1.0F);
    CHECK(safe_voice->wave.frequency == 70.0F);
    REQUIRE(safe_defaults->right.has_value());
    CHECK(checked_optional(safe_defaults->right).payload.frequency == 68.0F);
}

TEST_CASE("fallout4.policy.body_duration_scale_controls_complete_lifetime_from_event_time",
    "[component][fallout4][policy]") {
    constexpr std::int64_t base_ms = 15'000;
    const auto double_duration = std::make_unique<PolicyConfig>();
    double_duration->effects.body[static_cast<std::size_t>(BodyEffect::rifle_shot)].duration_scale =
        2.0F;
    const auto delayed_admission_owner = std::make_unique<Policy>(*double_duration);
    auto& delayed_admission = *delayed_admission_owner;
    const auto observation = gameplay_at(base_ms + 72);
    const auto shot = event(EventKind::shot, at(base_ms), 755);
    const std::array events{shot};
    const auto admitted = update_on_heap(delayed_admission, observation, events, at(base_ms + 72));
    const auto* const rifle_voice = find_voice(*admitted, detail::Cue::rifle_shot);
    INFO("scaled rifle impact admits after the unscaled expiry and keeps its original event "
         "timestamp");
    REQUIRE(rifle_voice != nullptr);
    CHECK(rifle_voice->started_at == at(base_ms));
    CHECK(rifle_voice->duration_ms == 128);

    INFO("classic fallback receives the scaled remaining lifetime rather than restarting at "
         "receipt");
    REQUIRE(admitted->rumble.has_value());
    CHECK(checked_optional(admitted->rumble).ttl_ms == 56);

    if (rifle_voice != nullptr) {
        inputnext::adapters::BodyProgram program{};
        program.voices[0] = *rifle_voice;
        std::vector<float> pcm(std::size_t{16} * 48U * 2U);
        static_cast<void>(inputnext::adapters::build_body_pcm(program, at(base_ms + 84), pcm));
        const double second_pulse_energy =
            std::accumulate(pcm.begin(), pcm.end(), 0.0, [](double total, float sample) {
                return total + std::abs(sample);
            });
        INFO("scaled rifle voice reaches its second pulse in rendered body PCM");
        CHECK(second_pulse_energy > 0.0);
    }

    const auto before_expiry = update_on_heap(delayed_admission,
        gameplay_at(base_ms + 127),
        std::span<const PolicyEvent>{},
        at(base_ms + 127));
    INFO("scaled rifle impact remains active until its adjusted deadline");
    CHECK(voice_count(*before_expiry) == 1);

    const auto expired = update_on_heap(delayed_admission,
        gameplay_at(base_ms + 128),
        std::span<const PolicyEvent>{},
        at(base_ms + 128));
    INFO("scaled rifle impact expires exactly at its event-anchored deadline without empty-tick "
         "extension");
    CHECK(voice_count(*expired) == 0);
    CHECK(!expired->rumble.has_value());

    const auto quarter_duration = std::make_unique<PolicyConfig>();
    quarter_duration->effects.body[static_cast<std::size_t>(BodyEffect::rotary_shot)]
        .duration_scale = 0.25F;
    const auto rotary_owner = std::make_unique<Policy>(*quarter_duration);
    auto& rotary = *rotary_owner;
    auto rotary_observation = gameplay_at(base_ms + 200);
    rotary_observation.weapon.family = WeaponFamily::rotary;
    const auto rotary_shot = event(EventKind::shot, at(base_ms + 200), 756);
    const std::array rotary_events{rotary_shot};
    const auto rotary_start =
        update_on_heap(rotary, rotary_observation, rotary_events, at(base_ms + 200));
    const auto* const rotary_voice = find_voice(*rotary_start, detail::Cue::rotary_shot);
    INFO("quarter-scaled rotary impact rounds to its eight-millisecond lifetime");
    REQUIRE(rotary_voice != nullptr);
    CHECK(rotary_voice->duration_ms == 8);

    const auto rotary_expired = update_on_heap(rotary,
        gameplay_at(base_ms + 208),
        std::span<const PolicyEvent>{},
        at(base_ms + 208));
    INFO("short rotary impact retires at its exact adjusted deadline");
    CHECK(voice_count(*rotary_expired) == 0);

    const auto delayed_rotary_owner = std::make_unique<Policy>(*quarter_duration);
    auto& delayed_rotary = *delayed_rotary_owner;
    rotary_observation = gameplay_at(base_ms + 208);
    rotary_observation.weapon.family = WeaponFamily::rotary;
    const std::array delayed_rotary_event{rotary_shot};
    const auto too_late =
        update_on_heap(delayed_rotary, rotary_observation, delayed_rotary_event, at(base_ms + 208));
    INFO("quarter-scaled rotary event arriving at expiry is rejected");
    CHECK(voice_count(*too_late) == 0);
}

TEST_CASE("fallout4.policy.expired_fresh_events_do_not_consume_voice_capacity",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    const auto observation = gameplay_at(2'000);
    std::array<PolicyEvent, 6> events{};
    for (std::size_t index = 0; index < 5; ++index) {
        events[index] = event(EventKind::hit_received, at(1'900), 300 + index);
    }
    events.back() = event(EventKind::shot, at(1'990), 305);

    const auto frame = policy->update(observation, events, at(2'000));
    const auto* shot_voice = find_voice(frame, detail::Cue::rifle_shot);
    INFO("expired but event-fresh hits are rejected before arbitration so a later live shot still "
         "gets output");
    REQUIRE(shot_voice != nullptr);
    CHECK(cue_voice_count(frame, detail::Cue::rifle_shot) == 1);
    CHECK(voice_count(frame) == 1);
    CHECK(frame.rumble.has_value());

    INFO("the live shot retains its trigger pulse after expired high-priority cues are discarded");
    REQUIRE(frame.right.has_value());
    CHECK(checked_optional(frame.right).payload.mode == INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION);

    INFO("the authored live voice preserves its original event timestamp");
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(shot_voice->started_at - MonoTime{})
              .count() == 1'990);
}

TEST_CASE("fallout4.policy.recipe_expiration_boundary_is_exact", "[component][fallout4][policy]") {
    const auto expired_policy = std::make_unique<Policy>();
    auto observation = gameplay_at(1'000);
    auto exact = event(EventKind::hit_received, at(930), 400);
    exact.blocked = true;
    const std::array exact_event{exact};
    const auto expired = expired_policy->update(observation, exact_event, at(1'000));
    INFO("a cue at exact recipe-expiration equality is rejected before occupying a voice");
    CHECK(voice_count(expired) == 0);
    CHECK(!expired.rumble.has_value());

    const auto boundary_policy = std::make_unique<Policy>();
    auto one_ms_live = event(EventKind::hit_received, at(931), 401);
    one_ms_live.blocked = true;
    const std::array live_event{one_ms_live};
    const auto live = boundary_policy->update(observation, live_event, at(1'000));
    const auto* const live_block = find_voice(live, detail::Cue::blocked_hit);
    INFO("a cue remains live immediately before its recipe endpoint and keeps its original event "
         "timestamp");
    CHECK(voice_count(live) == 1);
    REQUIRE(live_block != nullptr);
    CHECK(live_block->started_at == at(931));

    observation.sampled_at = at(1'001);
    const auto endpoint =
        boundary_policy->update(observation, std::span<const PolicyEvent>{}, at(1'001));
    INFO("an active cue expires exactly when elapsed time reaches its recipe duration");
    CHECK(voice_count(endpoint) == 0);
    CHECK(!endpoint.rumble.has_value());
}

} // namespace fallout4_adapter::policy_tests
