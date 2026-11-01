#include "effect_recipes.hpp"
#include "support.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fallout4_adapter/effect_settings.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

namespace fallout4_adapter::policy_tests {

TEST_CASE("fallout4.policy.footstep_profiles_are_opt_in_and_keep_event_provenance",
    "[component][fallout4][policy]") {
    const auto observation = std::make_unique<Observation>(gameplay_at(14'500));
    auto ordinary_step = event(EventKind::footstep, at(14'500), 760);
    ordinary_step.power_armor = false;
    ordinary_step.sprinting = false;
    const std::array ordinary_event{ordinary_step};
    const auto defaults = std::make_unique<Policy>();
    const auto silent = update_on_heap(*defaults, *observation, ordinary_event, at(14'500));
    INFO("ordinary footsteps are silent by default");
    CHECK(voice_count(*silent) == 0);
    CHECK(!silent->rumble.has_value());

    observation->sampled_at = at(14'501);
    auto sprint_step = event(EventKind::footstep, at(14'501), 761);
    sprint_step.sprinting = true;
    const std::array sprint_event{sprint_step};
    const auto sprint = update_on_heap(*defaults, *observation, sprint_event, at(14'501));
    const auto* const sprint_voice = find_voice(*sprint, detail::Cue::sprint_step);
    INFO("sprinting retains a subtle default-on contact cue distinct from walking");
    REQUIRE(sprint_voice != nullptr);
    CHECK(sprint_voice->wave.frequency == 80.0F);
    CHECK(sprint_voice->wave.level == 0.16F);
    CHECK(sprint_voice->duration_ms == 56);
    CHECK(sprint_voice->priority == 9);
    CHECK(cue_voice_count(*sprint, detail::Cue::ordinary_step) == 0);

    const auto settings = std::make_unique<PolicyConfig>();
    settings->effects.body[static_cast<std::size_t>(BodyEffect::ordinary_step)].enabled = true;
    const auto enabled = std::make_unique<Policy>(*settings);
    observation->sampled_at = at(14'500);
    const auto ordinary = update_on_heap(*enabled, *observation, ordinary_event, at(14'500));
    const auto* const ordinary_voice = find_voice(*ordinary, detail::Cue::ordinary_step);
    INFO("an opted-in ordinary animation event uses the quiet bilateral walking profile");
    REQUIRE(ordinary_voice != nullptr);
    CHECK(ordinary_voice->wave.frequency == 92.0F);
    CHECK(ordinary_voice->duration_ms == 64);
    CHECK(ordinary_voice->priority == 8);
    CHECK(cue_voice_count(*ordinary, detail::Cue::footstep) == 0);

    observation->sampled_at = at(14'560);
    auto armor_step = event(EventKind::footstep, at(14'560), 762);
    armor_step.power_armor = true;
    const std::array armor_event{armor_step};
    const auto armor = update_on_heap(*enabled, *observation, armor_event, at(14'560));
    INFO("the copied power-armor state chooses its own existing step recipe");
    CHECK(cue_voice_count(*armor, detail::Cue::footstep) == 1);
    INFO("the earlier quiet tail keeps its own timestamp while the new armor contact begins");
    const auto* const ordinary_tail = find_voice(*armor, detail::Cue::ordinary_step);
    REQUIRE(ordinary_tail != nullptr);
    CHECK(ordinary_tail->started_at == at(14'500));

    observation->sampled_at = at(14'564);
    const auto after_tail = update_on_heap(*enabled, *observation, {}, at(14'564));
    INFO("the earlier ordinary tail expires from its original time without removing armor");
    CHECK(cue_voice_count(*after_tail, detail::Cue::ordinary_step) == 0);
    CHECK(cue_voice_count(*after_tail, detail::Cue::footstep) == 1);

    observation->sampled_at = at(14'570);
    observation->mode = Mode::vats_playback;
    const auto reset =
        update_on_heap(*enabled, *observation, std::span<const PolicyEvent>{}, at(14'570));
    INFO("leaving gameplay clears active step cues through the existing mode reset");
    CHECK(voice_count(*reset) == 0);
}

TEST_CASE("fallout4.policy.resource_baseline_hysteresis_and_critical_count",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(4000);
    observation.health_ratio = 0.10F;
    observation.action_points_ratio = 0.50F;
    observation.critical_count = std::uint8_t{1};
    auto frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(4000));
    INFO("initial low resources and pre-existing critical charge establish a quiet baseline");
    CHECK(voice_count(*frame) == 0);

    observation.sampled_at = at(4010);
    observation.health_ratio = std::numeric_limits<float>::quiet_NaN();
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(4010));
    INFO("invalid resource data clears its baseline without fabricating a warning");
    CHECK(voice_count(*frame) == 0);

    observation.sampled_at = at(4020);
    observation.health_ratio = 0.12F;
    observation.critical_count = std::uint8_t{0};
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(4020));
    INFO("the first valid sample after invalidation re-establishes baseline without a cue");
    CHECK(voice_count(*frame) == 0);

    observation.sampled_at = at(4030);
    observation.health_ratio = 0.70F;
    observation.critical_count = std::uint8_t{1};
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(4030));
    INFO("critical-ready cue fires only on a real zero-to-positive count transition");
    CHECK(voice_count(*frame) == 1);

    observation.sampled_at = at(4040);
    observation.health_ratio = 0.24F;
    observation.critical_count = std::uint8_t{2};
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(4040));
    INFO("low-health cue fires on a downward threshold crossing and coexists within the five-voice "
         "cap");
    CHECK(voice_count(*frame) == 2);

    observation.sampled_at = at(4105);
    observation.health_ratio = 0.30F;
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(4105));
    INFO("existing resource cues expire on their authored finite lifetimes");
    CHECK(voice_count(*frame) == 0);

    observation.sampled_at = at(4115);
    observation.health_ratio = 0.24F;
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(4115));
    INFO("health warning does not chatter until the resource crosses its higher re-arm threshold");
    CHECK(voice_count(*frame) == 0);
}

TEST_CASE("fallout4.policy.reload_start_transition_only", "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(6000);
    observation.reloading = false;
    auto frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(6000));
    INFO("initial reload state establishes a quiet baseline");
    CHECK(voice_count(*frame) == 0);

    observation.sampled_at = at(6010);
    observation.reloading = true;
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(6010));
    const auto reload_sequence = cue_sequence(*frame, detail::Cue::reload_start);
    INFO("a fresh gameplay false-to-true reload edge emits one short body accent");
    CHECK(cue_voice_count(*frame, detail::Cue::reload_start) == 1);
    CHECK(reload_sequence != 0);

    observation.sampled_at = at(6011);
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(6011));
    INFO("reload accents provide a bounded classic fallback");
    REQUIRE(frame->rumble.has_value());
    CHECK(checked_optional(frame->rumble).ttl_ms <= 100);

    observation.sampled_at = at(6020);
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(6020));
    INFO("holding the reload state does not repeat or restart its accent");
    CHECK(cue_voice_count(*frame, detail::Cue::reload_start) == 1);
    CHECK(cue_sequence(*frame, detail::Cue::reload_start) == reload_sequence);

    observation.sampled_at = at(6100);
    observation.reloading = false;
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(6100));
    INFO("reload completion and cancellation do not emit a second cue");
    CHECK(voice_count(*frame) == 0);

    observation.sampled_at = at(6110);
    observation.reloading = true;
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(6110));
    INFO("a later genuine reload start can emit after the prior reload state ended");
    CHECK(cue_voice_count(*frame, detail::Cue::reload_start) == 1);

    const auto unidentified = std::make_unique<Policy>();
    observation = gameplay_at(6200);
    observation.weapon.identity = 0;
    observation.reloading = false;
    static_cast<void>(unidentified->update(observation, std::span<const PolicyEvent>{}, at(6200)));
    observation.sampled_at = at(6210);
    observation.reloading = true;
    const auto no_identity =
        update_on_heap(*unidentified, observation, std::span<const PolicyEvent>{}, at(6210));
    INFO("reload start is silent unless both observations identify the same nonzero weapon "
         "instance");
    CHECK(cue_voice_count(*no_identity, detail::Cue::reload_start) == 0);
}

TEST_CASE("fallout4.policy.mechanic_baselines_across_weapon_and_vats_changes",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(7000);
    observation.reloading = false;
    observation.power_armor = false;
    static_cast<void>(policy->update(observation, std::span<const PolicyEvent>{}, at(7000)));

    observation.sampled_at = at(7010);
    observation.weapon.identity = 43;
    observation.reloading = true;
    observation.power_armor = true;
    auto frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(7010));
    INFO("a weapon swap baselines simultaneous reload and power-armor changes without cues");
    CHECK(cue_voice_count(*frame, detail::Cue::reload_start) == 0);
    CHECK(cue_voice_count(*frame, detail::Cue::power_armor_enter) == 0);

    observation.sampled_at = at(7020);
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(7020));
    INFO("the first stable sample after a weapon swap remains quiet");
    CHECK(voice_count(*frame) == 0);

    observation.sampled_at = at(7030);
    observation.mode = Mode::vats_selection;
    observation.reloading = false;
    observation.power_armor = false;
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(7030));
    INFO("power-armor and reload transitions never cue during VATS selection");
    CHECK(cue_voice_count(*frame, detail::Cue::power_armor_exit) == 0);
    CHECK(cue_voice_count(*frame, detail::Cue::reload_start) == 0);

    observation.sampled_at = at(7040);
    observation.mode = Mode::gameplay;
    observation.reloading = true;
    observation.power_armor = true;
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(7040));
    INFO("returning to gameplay establishes fresh quiet mechanic baselines");
    CHECK(voice_count(*frame) == 0);

    observation.sampled_at = at(7050);
    observation.reloading = false;
    observation.power_armor = false;
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(7050));
    INFO("only a new gameplay power-armor edge cues after return; reload completion stays silent");
    CHECK(cue_voice_count(*frame, detail::Cue::power_armor_exit) == 1);
    CHECK(cue_voice_count(*frame, detail::Cue::reload_start) == 0);

    observation.sampled_at = at(7060);
    observation.reloading = true;
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(7060));
    INFO("reload start resumes after a stable same-weapon gameplay baseline");
    CHECK(cue_voice_count(*frame, detail::Cue::reload_start) == 1);
}

TEST_CASE("fallout4.policy.power_armor_edges_resets_and_stale_samples",
    "[component][fallout4][policy]") {
    const auto policy = std::make_unique<Policy>();
    auto observation = gameplay_at(8000);
    observation.power_armor = true;
    observation.reloading = true;
    auto frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(8000));
    INFO("first gameplay observation with power armor and reload is quiet");
    CHECK(voice_count(*frame) == 0);

    observation.sampled_at = at(8010);
    observation.power_armor = false;
    observation.reloading = false;
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(8010));
    INFO("a confirmed power-armor exit has its distinct cue without a reload-completion cue");
    CHECK(cue_voice_count(*frame, detail::Cue::power_armor_exit) == 1);
    CHECK(cue_voice_count(*frame, detail::Cue::reload_start) == 0);

    observation.sampled_at = at(8020);
    observation.power_armor = true;
    observation.reloading = true;
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(8020));
    INFO("power-armor entry and reload start are separate short gameplay accents");
    CHECK(cue_voice_count(*frame, detail::Cue::power_armor_enter) == 1);
    CHECK(cue_voice_count(*frame, detail::Cue::reload_start) == 1);

    policy->reset_effects();
    observation.sampled_at = at(8030);
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(8030));
    INFO("reset_effects clears mechanic baselines without replay");
    CHECK(voice_count(*frame) == 0);

    observation.sampled_at = at(7800);
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(8040));
    INFO("a stale observation clears mechanic voices and baselines");
    CHECK(voice_count(*frame) == 0);

    observation.sampled_at = at(8041);
    frame = update_on_heap(*policy, observation, std::span<const PolicyEvent>{}, at(8041));
    INFO("fresh state after staleness is a quiet baseline, not a historical cue");
    CHECK(voice_count(*frame) == 0);

    const auto ui_config = std::make_unique<PolicyConfig>();
    ui_config->enabled = true;
    ui_config->trigger_scale = 0.65F;
    ui_config->body_scale = 0.65F;
    ui_config->resource_cues = true;
    ui_config->ui_cues = true;
    const auto ui_enabled = std::make_unique<Policy>(*ui_config);
    observation = gameplay_at(8050);
    observation.power_armor = false;
    observation.reloading = false;
    static_cast<void>(ui_enabled->update(observation, std::span<const PolicyEvent>{}, at(8050)));
    ui_enabled->reset_effects();
    observation.sampled_at = at(8060);
    observation.mode = Mode::vats_selection;
    observation.power_armor = true;
    observation.reloading = true;
    frame = update_on_heap(*ui_enabled, observation, std::span<const PolicyEvent>{}, at(8060));
    INFO("reset_effects retains the optional mode baseline but clears mechanic-transition "
         "baselines");
    CHECK(cue_voice_count(*frame, detail::Cue::vats_enter) == 1);
    CHECK(cue_voice_count(*frame, detail::Cue::power_armor_enter) == 0);
    CHECK(cue_voice_count(*frame, detail::Cue::reload_start) == 0);
}

TEST_CASE("fallout4.policy.mechanic_cues_respect_scale_and_priority",
    "[component][fallout4][policy]") {
    const auto muted_config = std::make_unique<PolicyConfig>();
    muted_config->enabled = true;
    muted_config->trigger_scale = 0.65F;
    muted_config->body_scale = 0.0F;
    muted_config->resource_cues = true;
    muted_config->ui_cues = false;
    const auto muted = std::make_unique<Policy>(*muted_config);
    auto observation = gameplay_at(9000);
    observation.power_armor = false;
    observation.reloading = false;
    static_cast<void>(muted->update(observation, std::span<const PolicyEvent>{}, at(9000)));
    observation.sampled_at = at(9010);
    observation.power_armor = true;
    observation.reloading = true;
    auto frame = update_on_heap(*muted, observation, std::span<const PolicyEvent>{}, at(9010));
    INFO("body scale zero mutes mechanic cues on HD and classic output");
    CHECK(!frame->rumble.has_value());
    CHECK(voice_count(*frame) == 0);

    const auto busy = std::make_unique<Policy>();
    observation = gameplay_at(9100);
    observation.reloading = false;
    observation.power_armor = false;
    static_cast<void>(busy->update(observation, std::span<const PolicyEvent>{}, at(9100)));

    observation.sampled_at = at(9110);
    std::array<PolicyEvent, 5> shots{};
    for (std::size_t index = 0; index < shots.size(); ++index) {
        shots[index] = event(EventKind::shot, at(9110), 100 + index);
    }
    frame = update_on_heap(*busy, observation, shots, at(9110));
    INFO("five active confirmed shots occupy the bounded voice mixer");
    CHECK(voice_count(*frame) == 5);

    observation.sampled_at = at(9111);
    observation.reloading = true;
    observation.power_armor = true;
    frame = update_on_heap(*busy, observation, std::span<const PolicyEvent>{}, at(9111));
    INFO("low-priority mechanic accents never displace active shot voices");
    CHECK(voice_count(*frame) == 5);
    CHECK(cue_voice_count(*frame, detail::Cue::reload_start) == 0);
    CHECK(cue_voice_count(*frame, detail::Cue::power_armor_enter) == 0);

    for (const auto& voice : frame->body.voices) {
        INFO("bounded voice arbitration preserves higher-priority shot recipes over mechanic cues");
        REQUIRE(voice.has_value());
        CHECK(checked_optional(voice).source_id <=
              static_cast<std::uint32_t>(detail::Cue::generic_shot) + 1U);
    }
}

} // namespace fallout4_adapter::policy_tests
