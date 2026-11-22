#include "policy_test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/policy.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace fallout_gamebryo::tests {
using namespace std::chrono_literals;

namespace {
Event incoming_hit(const Observation& observation,
    std::uint64_t action,
    HitOutcome outcome,
    bool confirmed_damage = true) noexcept {
    return Event{.at = observation.at,
        .epoch = observation.epoch,
        .frame = observation.frame,
        .action = action,
        .mode = observation.mode,
        .kind = EventKind::hit,
        .source = 0x99U,
        .target = observation.player,
        .weapon_class = WeaponClass::rifle,
        .hit_outcome = outcome,
        .confirmed_damage = confirmed_damage};
}

Event outgoing_hit(const Observation& observation,
    std::uint64_t action,
    HitOutcome outcome) noexcept {
    return Event{.at = observation.at,
        .epoch = observation.epoch,
        .frame = observation.frame,
        .action = action,
        .mode = observation.mode,
        .kind = EventKind::hit,
        .source = observation.player,
        .target = 0x99U,
        .weapon_class = WeaponClass::rifle,
        .hit_outcome = outcome,
        .confirmed_damage = true};
}
} // namespace

TEST_CASE("Outgoing hit outcomes select one distinct contact effect",
    "[fallout-gamebryo][combat]") {
    constexpr std::array cases{
        std::pair{HitOutcome::ordinary, BodyEffect::hit_dealt},
        std::pair{HitOutcome::critical, BodyEffect::critical_hit},
        std::pair{HitOutcome::sneak_critical, BodyEffect::sneak_critical},
    };

    for (const auto& [outcome, expected] : cases) {
        const auto settings = std::make_unique<Settings>();
        const auto policy = std::make_unique<Policy>(*settings);
        const auto observation = gameplay();
        const auto event = outgoing_hit(observation, 1U, outcome);
        const std::array events{event};
        const auto frame = policy->update(observation, events, observation.at);

        CHECK(voice_count(frame) == 1U);
        CHECK(has_voice(frame, expected));
    }
}

TEST_CASE("Synthesized combat profiles use distinct wave recipes", "[fallout-gamebryo][combat]") {
    constexpr std::array effects{BodyEffect::critical_hit,
        BodyEffect::sneak_critical,
        BodyEffect::crippled_hit,
        BodyEffect::stagger,
        BodyEffect::knockdown};

    std::array<inputnext::adapters::WaveRecipe, effects.size()> recipes{};
    for (std::size_t current{}; current < effects.size(); ++current) {
        const auto settings = std::make_unique<Settings>();
        const auto policy = std::make_unique<Policy>(*settings);
        auto observation = gameplay();
        inputnext::adapters::OutputFrame frame{};
        switch (effects[current]) {
        case BodyEffect::critical_hit:
        case BodyEffect::sneak_critical: {
            const auto outcome = effects[current] == BodyEffect::critical_hit
                                     ? HitOutcome::critical
                                     : HitOutcome::sneak_critical;
            const auto event = outgoing_hit(observation, 10U, outcome);
            const std::array events{event};
            frame = policy->update(observation, events, observation.at);
            break;
        }
        case BodyEffect::crippled_hit: {
            const auto event = incoming_hit(observation, 11U, HitOutcome::crippled);
            const std::array events{event};
            frame = policy->update(observation, events, observation.at);
            break;
        }
        case BodyEffect::stagger:
        case BodyEffect::knockdown: {
            observation.reaction = PlayerReaction::steady;
            static_cast<void>(policy->update(observation, {}, observation.at));
            observation.at += 16ms;
            ++observation.frame;
            observation.reaction = effects[current] == BodyEffect::stagger
                                       ? PlayerReaction::staggered
                                       : PlayerReaction::knocked_down;
            frame = policy->update(observation, {}, observation.at);
            break;
        }
        default:
            continue;
        }

        const auto voice = voice_for(frame, effects[current]);
        recipes[current] = voice.wave;
        CHECK(voice.duration_ms > 0U);
        CHECK_FALSE(voice.wave.audio_envelope.enabled);
        for (std::size_t previous{}; previous < current; ++previous) {
            CHECK(recipes[current] != recipes[previous]);
        }
    }
}

TEST_CASE("Incoming damage distinguishes ordinary and crippling hits",
    "[fallout-gamebryo][combat]") {
    constexpr std::array cases{
        std::pair{HitOutcome::ordinary, BodyEffect::hit_taken},
        std::pair{HitOutcome::crippled, BodyEffect::crippled_hit},
    };

    for (const auto& [outcome, expected] : cases) {
        const Settings settings{};
        const auto policy = std::make_unique<Policy>(settings);
        const auto observation = gameplay();
        const auto event = incoming_hit(observation, 2U, outcome);
        const std::array events{event};
        const auto frame = policy->update(observation, events, observation.at);

        CHECK(voice_count(frame) == 1U);
        CHECK(has_voice(frame, expected));
    }
}

TEST_CASE("Only a verified blocked incoming hit can cue without health damage",
    "[fallout-gamebryo][combat]") {
    Settings settings{};
    settings.led.health_enabled = false;
    settings.led.stealth_enabled = false;
    settings.led.radiation_enabled = false;
    const auto observation = gameplay();
    const auto blocked = incoming_hit(observation, 3U, HitOutcome::blocked, false);
    const std::array blocked_events{blocked};
    const auto blocked_policy = std::make_unique<Policy>(settings);
    const auto blocked_frame = blocked_policy->update(observation, blocked_events, observation.at);
    CHECK(voice_count(blocked_frame) == 1U);
    CHECK(has_voice(blocked_frame, BodyEffect::block_impact));
    CHECK_FALSE(blocked_frame.leds);

    const auto ordinary = incoming_hit(observation, 4U, HitOutcome::ordinary, false);
    const std::array ordinary_events{ordinary};
    const auto ordinary_policy = std::make_unique<Policy>(settings);
    CHECK(voice_count(ordinary_policy->update(observation, ordinary_events, observation.at)) == 0U);

    auto unknown_block = blocked;
    unknown_block.action = 5U;
    unknown_block.source = 0U;
    const std::array unknown_block_events{unknown_block};
    const auto unknown_block_policy = std::make_unique<Policy>(settings);
    CHECK(
        voice_count(
            unknown_block_policy->update(observation, unknown_block_events, observation.at)) == 0U);

    auto self_block = blocked;
    self_block.action = 6U;
    self_block.source = observation.player;
    const std::array self_block_events{self_block};
    const auto self_block_policy = std::make_unique<Policy>(settings);
    CHECK(voice_count(self_block_policy->update(observation, self_block_events, observation.at)) ==
          0U);
}

TEST_CASE("Player explosions require confirmed damage to another target",
    "[fallout-gamebryo][combat]") {
    const Settings settings{};
    const auto observation = gameplay();
    const auto explosion = Event{.at = observation.at,
        .epoch = observation.epoch,
        .frame = observation.frame,
        .action = 7U,
        .mode = observation.mode,
        .kind = EventKind::explosion_dealt,
        .source = observation.player,
        .target = 0x99U,
        .confirmed_damage = true};
    const std::array events{explosion};
    const auto policy = std::make_unique<Policy>(settings);
    const auto frame = policy->update(observation, events, observation.at);
    CHECK(voice_count(frame) == 1U);
    CHECK(has_voice(frame, BodyEffect::explosion_dealt));

    auto invalid = explosion;
    invalid.action = 8U;
    SECTION("unknown target") {
        invalid.target = 0U;
    }
    SECTION("player is the target") {
        invalid.target = observation.player;
    }
    SECTION("source is not the player") {
        invalid.source = 0x99U;
    }
    SECTION("damage is unconfirmed") {
        invalid.confirmed_damage = false;
    }
    const std::array invalid_events{invalid};
    const auto invalid_policy = std::make_unique<Policy>(settings);
    CHECK(voice_count(invalid_policy->update(observation, invalid_events, observation.at)) == 0U);
}

TEST_CASE("Reaction voices keep their identity and expire until a new transition",
    "[fallout-gamebryo][combat]") {
    const auto settings = std::make_unique<Settings>();
    const auto policy = std::make_unique<Policy>(*settings);
    auto observation = gameplay();
    observation.reaction = PlayerReaction::steady;
    CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);

    const auto advance = [&](PlayerReaction reaction, std::chrono::milliseconds elapsed) {
        observation.at += elapsed;
        ++observation.frame;
        observation.reaction = reaction;
        return policy->update(observation, {}, observation.at);
    };

    const auto staggered = std::make_unique<inputnext::adapters::OutputFrame>(
        advance(PlayerReaction::staggered, 16ms));
    CHECK(voice_count(*staggered) == 1U);
    CHECK(has_voice(*staggered, BodyEffect::stagger));
    const auto initial_stagger = voice_for(*staggered, BodyEffect::stagger);
    CHECK(initial_stagger.started_at == observation.at);
    CHECK(initial_stagger.stable_id != 0U);
    CHECK(initial_stagger.sequence != 0U);
    CHECK(initial_stagger.source_id == static_cast<std::uint32_t>(BodyEffect::stagger) + 1U);

    const auto continuing_stagger = std::make_unique<inputnext::adapters::OutputFrame>(
        advance(PlayerReaction::staggered, 50ms));
    CHECK(effect_voice_count(*continuing_stagger, BodyEffect::stagger) == 1U);
    const auto same_stagger = voice_for(*continuing_stagger, BodyEffect::stagger);
    CHECK(same_stagger.started_at == initial_stagger.started_at);
    CHECK(same_stagger.stable_id == initial_stagger.stable_id);
    CHECK(same_stagger.sequence == initial_stagger.sequence);

    const auto expired_stagger = std::make_unique<inputnext::adapters::OutputFrame>(
        advance(PlayerReaction::staggered, 67ms));
    CHECK(voice_count(*expired_stagger) == 0U);

    const auto knocked_down = std::make_unique<inputnext::adapters::OutputFrame>(
        advance(PlayerReaction::knocked_down, 16ms));
    CHECK(voice_count(*knocked_down) == 1U);
    CHECK(has_voice(*knocked_down, BodyEffect::knockdown));
    const auto initial_knockdown = voice_for(*knocked_down, BodyEffect::knockdown);
    CHECK(initial_knockdown.started_at == observation.at);
    CHECK(initial_knockdown.stable_id != 0U);
    CHECK(initial_knockdown.sequence > initial_stagger.sequence);
    CHECK(initial_knockdown.source_id == static_cast<std::uint32_t>(BodyEffect::knockdown) + 1U);

    const auto continuing_knockdown = std::make_unique<inputnext::adapters::OutputFrame>(
        advance(PlayerReaction::knocked_down, 100ms));
    CHECK(effect_voice_count(*continuing_knockdown, BodyEffect::knockdown) == 1U);
    const auto same_knockdown = voice_for(*continuing_knockdown, BodyEffect::knockdown);
    CHECK(same_knockdown.started_at == initial_knockdown.started_at);
    CHECK(same_knockdown.stable_id == initial_knockdown.stable_id);
    CHECK(same_knockdown.sequence == initial_knockdown.sequence);

    const auto expired_knockdown = std::make_unique<inputnext::adapters::OutputFrame>(
        advance(PlayerReaction::knocked_down, 51ms));
    CHECK(voice_count(*expired_knockdown) == 0U);

    CHECK(voice_count(advance(PlayerReaction::steady, 16ms)) == 0U);
    const auto recovered_stagger = std::make_unique<inputnext::adapters::OutputFrame>(
        advance(PlayerReaction::staggered, 16ms));
    CHECK(voice_count(*recovered_stagger) == 1U);
    const auto new_stagger = voice_for(*recovered_stagger, BodyEffect::stagger);
    CHECK(new_stagger.started_at == observation.at);
    CHECK(new_stagger.started_at > initial_stagger.started_at);
    CHECK(new_stagger.stable_id != initial_stagger.stable_id);
    CHECK(new_stagger.sequence > initial_knockdown.sequence);
}

TEST_CASE("Disabling one reaction cue preserves confirmed health LED feedback",
    "[fallout-gamebryo][combat]") {
    Settings settings{};
    ConfigError error{};
    CHECK(
        parse_config("[Body.Stagger]\nEnabled=false\n", settings, &error) == ConfigStatus::loaded);
    CHECK_FALSE(settings.body[static_cast<std::size_t>(BodyEffect::stagger)].enabled);

    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.health = inputnext::adapters::LedHealthSample{.current = 40.0F, .maximum = 100.0F};
    observation.reaction = PlayerReaction::steady;
    static_cast<void>(policy->update(observation, {}, observation.at));

    observation.at += 16ms;
    ++observation.frame;
    observation.health = inputnext::adapters::LedHealthSample{.current = 30.0F, .maximum = 100.0F};
    observation.reaction = PlayerReaction::staggered;
    const auto hit = incoming_hit(observation, 12U, HitOutcome::ordinary);
    const std::array events{hit};
    const auto frame = policy->update(observation, events, observation.at);

    CHECK_FALSE(has_voice(frame, BodyEffect::stagger));
    CHECK(has_voice(frame, BodyEffect::hit_taken));
    const auto led = frame.leds.value_or(inputnext::adapters::LedIntent{});
    CHECK(led.source_at == observation.at);
    CHECK(led.ttl_ms > 0U);
}

TEST_CASE("Reactions are re-established after a stale observation without a catch-up cue",
    "[fallout-gamebryo][combat]") {
    const Settings settings{};
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.reaction = PlayerReaction::steady;
    static_cast<void>(policy->update(observation, {}, observation.at));

    observation.at += snapshot_timeout + 1ms;
    ++observation.frame;
    observation.reaction = PlayerReaction::staggered;
    CHECK(voice_count(policy->update(observation, {}, observation.at)) == 0U);

    observation.at += 16ms;
    ++observation.frame;
    observation.reaction = PlayerReaction::knocked_down;
    const auto knocked_down = policy->update(observation, {}, observation.at);
    CHECK(voice_count(knocked_down) == 1U);
    CHECK(has_voice(knocked_down, BodyEffect::knockdown));
}

TEST_CASE("Reaction priority preserves the contact cue and selects the stronger response",
    "[fallout-gamebryo][combat]") {
    const Settings settings{};
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    observation.reaction = PlayerReaction::steady;
    static_cast<void>(policy->update(observation, {}, observation.at));

    observation.at += 16ms;
    ++observation.frame;
    observation.reaction = PlayerReaction::staggered;
    const auto hit = incoming_hit(observation, 9U, HitOutcome::ordinary);
    const std::array events{hit};
    const auto frame = policy->update(observation, events, observation.at);

    CHECK(voice_count(frame) == 2U);
    CHECK(has_voice(frame, BodyEffect::stagger));
    CHECK(has_voice(frame, BodyEffect::hit_taken));
    const auto rumble = frame.rumble.value_or(inputnext::adapters::RumbleIntent{});
    CHECK(rumble.payload.low_frequency > settings.body_scale * 0.62F * hit.strength);
}
} // namespace fallout_gamebryo::tests
