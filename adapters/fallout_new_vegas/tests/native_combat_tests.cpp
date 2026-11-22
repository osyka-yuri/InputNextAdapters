#include "event_reducer.hpp"
#include "native_combat.hpp"
#include "nvse_sdk.hpp"
#include "output_session/fake_output.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/session.hpp>
#include <fallout_gamebryo/settings.hpp>
#include <input_next_effects.h>
#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <nvse/GameProcess.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>

namespace {
using namespace std::chrono_literals;
using fallout_gamebryo::ConfigStatus;
using fallout_gamebryo::Event;
using fallout_gamebryo::EventKind;
using fallout_gamebryo::HitOutcome;
using fallout_gamebryo::Mode;
using fallout_gamebryo::MonoTime;
using fallout_gamebryo::Observation;
using fallout_gamebryo::PlayerReaction;
using fallout_gamebryo::Session;
using fallout_gamebryo::Settings;
using fallout_gamebryo::WeaponClass;
using fallout_new_vegas::HitForms;
using fallout_new_vegas::NativeSignal;
using fallout_new_vegas::read_hit_signal;
using fallout_new_vegas::read_player_reaction;
using fallout_new_vegas::SignalTranslator;
using inputnext::adapters::TickClock;
using inputnext::test::output_session::FakeOutput;

constexpr std::uint32_t player = 0x14U;
constexpr std::uint32_t npc = 0x201U;
constexpr std::uint32_t blocking = 0x1U;
constexpr std::uint32_t exploding = 0x2000U;
constexpr auto crippling = static_cast<std::uint32_t>(ActorHitData::kHitData_CripplePart);
constexpr auto critical = static_cast<std::uint32_t>(ActorHitData::kHitData_IsCritical);
constexpr auto sneak_critical =
    static_cast<std::uint32_t>(ActorHitData::kHitData_IsSneakAttackCritical);
constexpr HitForms incoming_melee{.source = npc,
    .target = player,
    .weapon = 0x42U,
    .weapon_class = WeaponClass::melee};

struct HitFixture {
    std::max_align_t attacker{};
    std::max_align_t weapon{};
    std::max_align_t first_contact{};
    std::max_align_t second_contact{};

    // The decoder uses the fixture addresses only as identities.
    ActorHitData hit{};

    HitFixture() noexcept {
        hit.source = reinterpret_cast<Actor*>(&attacker);
        hit.target = nullptr;
        hit.healthDmg = 10.0F;
        hit.weapon = reinterpret_cast<TESObjectWEAP*>(&weapon);
    }
};

[[nodiscard]] NativeSignal
require_hit(const ActorHitData& hit, const HitForms& forms, bool incoming) {
    const auto signal = read_hit_signal(hit, forms, incoming);
    if (!signal) {
        throw std::runtime_error{"Expected a native hit sample"};
    }
    return *signal;
}

TEST_CASE("The native boundary decodes xNVSE hit flags", "[new-vegas][native-combat]") {
    HitFixture fixture;
    auto& hit = fixture.hit;
    constexpr HitForms outgoing{.source = player,
        .target = npc,
        .weapon = 0x42U,
        .weapon_class = WeaponClass::rifle};
    CHECK(require_hit(hit, outgoing, false).hit_outcome == HitOutcome::ordinary);
    hit.flags = critical;
    CHECK(require_hit(hit, outgoing, false).hit_outcome == HitOutcome::critical);
    hit.flags |= sneak_critical;
    CHECK(require_hit(hit, outgoing, false).hit_outcome == HitOutcome::sneak_critical);
    hit.flags = crippling | blocking;
    CHECK(require_hit(hit, incoming_melee, true).hit_outcome == HitOutcome::crippled);
    hit.healthDmg = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(read_hit_signal(hit, incoming_melee, true));
}

TEST_CASE("Block feedback requires a positively identified melee contact",
    "[new-vegas][native-combat]") {
    HitFixture fixture;
    auto& hit = fixture.hit;
    auto forms = incoming_melee;
    hit.flags = blocking;
    auto expected = HitOutcome::ordinary;
    SECTION("a weapon melee block at zero damage") {
        hit.healthDmg = 0.0F;
        expected = HitOutcome::blocked;
    }
    SECTION("Fists use the attacker as a contact identity") {
        forms.weapon = 0x000001F4U;
        forms.weapon_class = WeaponClass::unarmed;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        hit.projectile = reinterpret_cast<Projectile*>(hit.source);
        expected = HitOutcome::blocked;
    }
    SECTION("no hit weapon") {
        forms.weapon = 0U;
        forms.weapon_class = WeaponClass::none;
        hit.weapon = nullptr;
    }
    SECTION("a ranged weapon") {
        forms.weapon_class = WeaponClass::rifle;
    }
    SECTION("another unarmed weapon with an ambiguous contact identity") {
        forms.weapon_class = WeaponClass::unarmed;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        hit.projectile = reinterpret_cast<Projectile*>(hit.source);
    }
    SECTION("a projectile token on a nominal melee hit") {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        hit.projectile = reinterpret_cast<Projectile*>(&fixture.first_contact);
    }
    SECTION("an explosion") {
        hit.flags |= exploding;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        hit.explosion = nullptr;
    }
    SECTION("no validated block flag") {
        hit.flags = 0U;
    }
    SECTION("the player is both attacker and target") {
        forms.source = player;
    }
    const auto signal = require_hit(hit, forms, true);
    CHECK(signal.hit_outcome == expected);
    CHECK(signal.confirmed_damage == (hit.healthDmg > 0.0F));
    if (expected == HitOutcome::blocked) {
        CHECK(signal.hit_reference == 0U);
    }
}

TEST_CASE("Distinct explosion identities survive nearby native hit samples",
    "[new-vegas][native-combat]") {
    HitFixture fixture;
    auto& hit = fixture.hit;
    hit.flags = exploding;
    HitForms forms{.source = player, .target = npc};
    SignalTranslator translator;
    translator.reset(1U);

    const auto capture = [&](std::max_align_t& identity, MonoTime sampled_at) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        hit.explosion = reinterpret_cast<Explosion*>(&identity);
        auto signal = require_hit(hit, forms, false);
        signal.at = sampled_at;
        signal.epoch = 1U;
        signal.frame = 2U;
        signal.mode = Mode::gameplay;
        return translator.translate(signal, player);
    };
    const auto first = capture(fixture.first_contact, MonoTime{} + 40ms).value_or(Event{});
    REQUIRE(first.kind == EventKind::explosion_dealt);
    ++forms.target;
    CHECK_FALSE(capture(fixture.first_contact, MonoTime{} + 41ms));
    const auto second = capture(fixture.second_contact, MonoTime{} + 42ms).value_or(Event{});
    REQUIRE(second.kind == EventKind::explosion_dealt);
    CHECK(second.action != first.action);
    CHECK(second.at == first.at + 2ms);
}

TEST_CASE("Native reaction states separate knockdown, stagger and recovery",
    "[new-vegas][native-combat]") {
    CHECK(read_player_reaction({.knocked_state = 0U, .animation_action = -1}) ==
          PlayerReaction::steady);
    CHECK(read_player_reaction({.knocked_state = 0U,
              .animation_action = HighProcess::kAnimAction_Stagger}) == PlayerReaction::staggered);
    for (const auto state : std::array<std::uint8_t, 3>{1U, 2U, 3U}) {
        CHECK(read_player_reaction(
                  {.knocked_state = state, .animation_action = HighProcess::kAnimAction_Stagger}) ==
              PlayerReaction::knocked_down);
    }
    for (const auto state : std::array<std::uint8_t, 4>{4U, 5U, 6U, 255U}) {
        CHECK_FALSE(read_player_reaction(
            {.knocked_state = state, .animation_action = HighProcess::kAnimAction_Stagger}));
    }
}

void tick(Session& session, const Observation& observation, std::span<const Event> events = {}) {
    session.publish(observation, events);
    session.tick(TickClock{.supplied = observation.at});
}

TEST_CASE("INI disables combat vibration and the lightbar independently through delivery",
    "[new-vegas][native-combat][output]") {
    std::string_view config;
    bool expect_body{};
    bool expect_led{};
    SECTION("disabled cripple cue") {
        config = "[Body.CrippledHit]\nEnabled=false\n";
        expect_led = true;
    }
    SECTION("disabled lightbar") {
        config = "[LED]\nEnabled=false\n";
        expect_body = true;
    }
    Settings settings{};
    REQUIRE(fallout_gamebryo::parse_config(config, settings) == ConfigStatus::loaded);
    FakeOutput output;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_LIGHTBAR;
    const auto session = std::make_unique<Session>(output, settings);
    Observation observation{.at = MonoTime{} + 20ms,
        .epoch = 1U,
        .frame = 1U,
        .player = player,
        .mode = Mode::gameplay,
        .alive = true,
        .focused = true,
        .health = inputnext::adapters::LedHealthSample{.current = 100.0F, .maximum = 100.0F}};
    tick(*session, observation);
    // The first tick binds the controller. Supply its own health baseline before damage.
    observation.at += 20ms;
    ++observation.frame;
    tick(*session, observation);
    output.body_calls.clear();
    output.leds.clear();

    observation.at += 20ms;
    ++observation.frame;
    observation.health->current = 90.0F;
    HitFixture fixture;
    auto& hit = fixture.hit;
    hit.flags = crippling;
    auto signal = require_hit(hit, incoming_melee, true);
    signal.at = observation.at;
    signal.epoch = observation.epoch;
    signal.frame = observation.frame;
    signal.mode = observation.mode;
    const auto translator = std::make_unique<SignalTranslator>();
    translator->reset(observation.epoch);
    const auto event = translator->translate(signal, player).value_or(Event{});
    REQUIRE(event.kind == EventKind::hit);
    tick(*session, observation, std::array{event});

    const bool body_applied = std::ranges::any_of(output.body_calls, [](const auto& call) {
        return call.request.pcm.frame_count != 0U ||
               call.request.fallback_rumble.low_frequency > 0.0F ||
               call.request.fallback_rumble.high_frequency > 0.0F;
    });
    CHECK(body_applied == expect_body);
    CHECK(output.leds.empty() == !expect_led);
    if (expect_led && !output.leds.empty()) {
        CHECK(output.leds.back().leds.lightbar_rgb != 0U);
    }
}
} // namespace
