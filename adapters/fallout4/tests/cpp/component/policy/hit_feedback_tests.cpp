#include "effect_recipes.hpp"
#include "support.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/policy.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace fallout4_adapter::policy_tests {

namespace {
void check_critical_hits_keep_authored_priority(Policy& policy,
    const Observation& observation,
    const std::array<PolicyEvent, 5>& critical_hits) {
    const auto critical_frame = policy.update(observation, critical_hits, at(10'000));
    INFO("five simultaneous outgoing critical hits occupy the bounded mixer");
    CHECK(voice_count(critical_frame) == 5);
    CHECK(cue_voice_count(critical_frame, detail::Cue::critical_hit) == 5);

    for (const auto& voice : critical_frame.body.voices) {
        INFO("outgoing criticals retain their authored priority of 90");
        REQUIRE(voice.has_value());
        CHECK(checked_optional(voice).priority == 90);
    }
}

void check_incoming_blocked_hit_keeps_soft_recipe(Policy& policy, Observation& observation) {
    auto blocked = event(EventKind::hit_received, at(10'001), 210);
    blocked.blocked = true;
    blocked.magnitude = 0.0F;
    observation.sampled_at = at(10'001);
    const std::array blocked_event{blocked};
    const auto after_block = policy.update(observation, blocked_event, at(10'001));
    const auto* const blocked_voice = find_voice(after_block, detail::Cue::blocked_hit);
    const auto blocked_recipe = detail::recipe_for(detail::Cue::blocked_hit);
    INFO("incoming blocked damage wins arbitration while retaining its soft recipe and mild "
         "intensity");
    REQUIRE(blocked_voice != nullptr);
    CHECK(blocked_voice->priority == 100);
    CHECK(blocked_voice->wave.level == blocked_recipe.wave.level);
    CHECK(std::abs(blocked_voice->scale - (0.65F * 0.62F)) < 0.0001F);
    CHECK(blocked_recipe.priority == 68);
}

void check_incoming_cripple_survives_critical_stacking(Policy& policy, Observation& observation) {
    auto crippled = event(EventKind::hit_received, at(10'002), 211);
    crippled.crippled_limb = true;
    crippled.magnitude = 0.0F;
    observation.sampled_at = at(10'002);
    const std::array crippled_event{crippled};
    const auto after_cripple = policy.update(observation, crippled_event, at(10'002));
    const auto* retained_blocked_voice = find_voice(after_cripple, detail::Cue::blocked_hit);
    const auto* const crippled_voice = find_voice(after_cripple, detail::Cue::crippled_hit);
    const auto blocked_recipe = detail::recipe_for(detail::Cue::blocked_hit);
    const auto crippled_recipe = detail::recipe_for(detail::Cue::crippled_hit);
    INFO("the earlier incoming blocked cue remains active beside later crippling damage");
    REQUIRE(retained_blocked_voice != nullptr);
    CHECK(retained_blocked_voice->priority == 100);
    CHECK(retained_blocked_voice->wave.level == blocked_recipe.wave.level);

    INFO("incoming crippled damage also survives critical stacking without changing its authored "
         "waveform");
    REQUIRE(crippled_voice != nullptr);
    CHECK(crippled_voice->priority == 100);
    CHECK(crippled_voice->wave.level == crippled_recipe.wave.level);
    CHECK(std::abs(crippled_voice->scale - (0.65F * 0.62F)) < 0.0001F);
    CHECK(crippled_recipe.priority == 86);
    CHECK(cue_voice_count(after_cripple, detail::Cue::critical_hit) == 3);
}

void check_outgoing_cripple_keeps_authored_priority() {
    Policy policy{};
    const auto observation = gameplay_at(10'100);
    auto outgoing_cripple = event(EventKind::hit_dealt, at(10'100), 212);
    outgoing_cripple.crippled_limb = true;
    const std::array outgoing_event{outgoing_cripple};
    const auto outgoing = policy.update(observation, outgoing_event, at(10'100));
    const auto* const outgoing_voice = find_voice(outgoing, detail::Cue::crippled_hit);
    INFO("outgoing cripple cues keep the existing authored priority");
    REQUIRE(outgoing_voice != nullptr);
    CHECK(outgoing_voice->priority == 86);
    CHECK(outgoing_voice->priority == detail::recipe_for(detail::Cue::crippled_hit).priority);
}

struct PrecedenceCase {
    bool blocked;
    bool critical;
    bool bash;
    bool power_attack;
    bool explosion;
    bool crippled;
    bool melee;
    detail::Cue expected;
    int priority;
};

constexpr std::array precedence_cases{
    PrecedenceCase{
        .blocked = true,
        .critical = true,
        .bash = true,
        .power_attack = true,
        .explosion = true,
        .crippled = true,
        .melee = true,
        .expected = detail::Cue::blocked_hit,
        .priority = 68,
    },
    PrecedenceCase{
        .blocked = false,
        .critical = true,
        .bash = true,
        .power_attack = true,
        .explosion = true,
        .crippled = true,
        .melee = true,
        .expected = detail::Cue::critical_hit,
        .priority = 90,
    },
    PrecedenceCase{
        .blocked = false,
        .critical = false,
        .bash = true,
        .power_attack = true,
        .explosion = true,
        .crippled = true,
        .melee = true,
        .expected = detail::Cue::bash,
        .priority = 66,
    },
    PrecedenceCase{
        .blocked = false,
        .critical = false,
        .bash = false,
        .power_attack = true,
        .explosion = true,
        .crippled = true,
        .melee = true,
        .expected = detail::Cue::power_attack,
        .priority = 68,
    },
    PrecedenceCase{
        .blocked = false,
        .critical = false,
        .bash = false,
        .power_attack = false,
        .explosion = true,
        .crippled = true,
        .melee = true,
        .expected = detail::Cue::explosive_hit,
        .priority = 70,
    },
    PrecedenceCase{
        .blocked = false,
        .critical = false,
        .bash = false,
        .power_attack = false,
        .explosion = false,
        .crippled = true,
        .melee = true,
        .expected = detail::Cue::crippled_hit,
        .priority = 86,
    },
    PrecedenceCase{
        .blocked = false,
        .critical = false,
        .bash = false,
        .power_attack = false,
        .explosion = false,
        .crippled = false,
        .melee = true,
        .expected = detail::Cue::melee_hit,
        .priority = 70,
    },
    PrecedenceCase{
        .blocked = false,
        .critical = false,
        .bash = false,
        .power_attack = false,
        .explosion = false,
        .crippled = false,
        .melee = false,
        .expected = detail::Cue::outgoing_hit,
        .priority = 70,
    },
};

void check_outgoing_precedence_case(const PrecedenceCase& test_case, std::size_t index) {
    const std::int64_t milliseconds = 10'000 + static_cast<std::int64_t>(index);
    Policy policy{};
    auto observation = gameplay_at(milliseconds);
    observation.weapon.family = WeaponFamily::pistol;
    auto outgoing = event(EventKind::hit_dealt, at(milliseconds), 300 + index);
    outgoing.blocked = test_case.blocked;
    outgoing.critical = test_case.critical;
    outgoing.bash = test_case.bash;
    outgoing.power_attack = test_case.power_attack;
    outgoing.explosion = test_case.explosion;
    outgoing.crippled_limb = test_case.crippled;
    outgoing.melee = test_case.melee;
    const std::array events{outgoing};
    const auto frame = policy.update(observation, events, at(milliseconds));
    const auto* const voice = find_voice(frame, test_case.expected);
    INFO("outgoing hit uses the specified variant precedence and role priority");
    REQUIRE(voice != nullptr);
    CHECK(voice->priority == test_case.priority);
    CHECK(voice_count(frame) == 1);
}

void check_flagged_melee_contact_uses_supplied_flag() {
    Policy policy{};
    auto observation = gameplay_at(10'100);
    observation.weapon.family = WeaponFamily::pistol;
    auto contact = event(EventKind::hit_dealt, at(10'100), 400);
    contact.melee = true;
    const std::array confirmed{contact};
    const auto confirmed_frame = policy.update(observation, confirmed, at(10'100));
    INFO("the supplied melee-contact flag selects the melee cue independently of the currently "
         "equipped family");
    CHECK(cue_voice_count(confirmed_frame, detail::Cue::melee_hit) == 1);
}

void check_equipped_family_does_not_infer_melee_contact() {
    Policy policy{};
    auto observation = gameplay_at(10'110);
    observation.weapon.family = WeaponFamily::two_handed_melee;
    const auto contact = event(EventKind::hit_dealt, at(10'110), 401);
    const std::array unconfirmed{contact};
    const auto unconfirmed_frame = policy.update(observation, unconfirmed, at(10'110));
    INFO("equipped melee family alone never infers confirmed melee contact");
    CHECK(cue_voice_count(unconfirmed_frame, detail::Cue::outgoing_hit) == 1);
    CHECK(cue_voice_count(unconfirmed_frame, detail::Cue::melee_hit) == 0);
}
} // namespace

TEST_CASE("fallout4.policy.incoming_damage_role_priority_preserves_recipe_art",
    "[component][fallout4][policy]") {
    {
        Policy policy{};
        auto observation = gameplay_at(10'000);
        std::array<PolicyEvent, 5> critical_hits{};
        for (std::size_t index = 0; index < critical_hits.size(); ++index) {
            critical_hits[index] = event(EventKind::hit_dealt, at(10'000), 200 + index);
            critical_hits[index].critical = true;
        }

        check_critical_hits_keep_authored_priority(policy, observation, critical_hits);
        check_incoming_blocked_hit_keeps_soft_recipe(policy, observation);
        check_incoming_cripple_survives_critical_stacking(policy, observation);
    }
    check_outgoing_cripple_keeps_authored_priority();
}

TEST_CASE("fallout4.policy.outgoing_hit_precedence_and_melee_flag",
    "[component][fallout4][policy]") {
    for (std::size_t index = 0; index < precedence_cases.size(); ++index) {
        check_outgoing_precedence_case(precedence_cases[index], index);
    }
    check_flagged_melee_contact_uses_supplied_flag();
    check_equipped_family_does_not_infer_melee_contact();
}

TEST_CASE("fallout4.policy.event_magnitude_scales_contact_and_invalid_values_are_rejected",
    "[component][fallout4][policy]") {
    const auto contact_scale = [](float magnitude, std::int64_t milliseconds) {
        Policy policy{};
        const auto observation = gameplay_at(milliseconds);
        auto hit =
            event(EventKind::hit_dealt, at(milliseconds), static_cast<std::uint64_t>(milliseconds));
        hit.magnitude = magnitude;
        const std::array events{hit};
        const auto frame = policy.update(observation, events, at(milliseconds));
        const auto* const voice = find_voice(frame, detail::Cue::outgoing_hit);
        return voice ? voice->scale : -1.0F;
    };

    const float zero = contact_scale(0.0F, 11'000);
    const float medium = contact_scale(0.5F, 11'010);
    const float full = contact_scale(1.0F, 11'020);
    INFO("finite contact magnitude changes intensity monotonically while preserving the "
         "zero-damage impact floor");
    CHECK(zero >= 0.0F);
    CHECK(zero < medium);
    CHECK(medium < full);

    constexpr std::array invalid{std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -0.01F,
        1.01F};
    for (std::size_t index = 0; index < invalid.size(); ++index) {
        Policy policy{};
        const auto milliseconds = 11'100 + static_cast<std::int64_t>(index);
        const auto observation = gameplay_at(milliseconds);
        auto hit = event(EventKind::hit_dealt, at(milliseconds), 500 + index);
        hit.magnitude = invalid[index];
        const std::array events{hit};
        const auto frame = policy.update(observation, events, at(milliseconds));
        INFO("non-finite or out-of-range contact magnitude is rejected");
        CHECK(voice_count(frame) == 0);
    }
}

} // namespace fallout4_adapter::policy_tests
