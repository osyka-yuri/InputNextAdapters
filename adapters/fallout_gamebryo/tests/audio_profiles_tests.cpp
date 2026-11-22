#include "policy_test_support.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/policy.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <inputnext_adapters/audio_haptics.hpp>
#include <inputnext_adapters/body_program.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace fallout_gamebryo::tests {
namespace {
using namespace std::chrono_literals;
using inputnext::adapters::AudioHapticBankStatus;
using inputnext::adapters::AudioHapticGame;

struct ProfileCase {
    BodyEffect effect;
    std::uint32_t duration_ms;
    std::optional<Event> event;
};

constexpr ProfileCase pistol_shot{.effect = BodyEffect::shot_pistol,
    .duration_ms = 90U,
    .event = Event{.weapon_class = WeaponClass::pistol}};
constexpr ProfileCase dry_fire{.effect = BodyEffect::dry_fire,
    .duration_ms = 75U,
    .event = Event{.kind = EventKind::dry_fire}};

constexpr auto profiles = std::to_array<ProfileCase>({
    pistol_shot,
    {.effect = BodyEffect::shot_rifle,
        .duration_ms = 105U,
        .event = Event{.weapon_class = WeaponClass::rifle}},
    {.effect = BodyEffect::shot_shotgun,
        .duration_ms = 125U,
        .event = Event{.weapon_class = WeaponClass::shotgun}},
    {.effect = BodyEffect::shot_heavy,
        .duration_ms = 115U,
        .event = Event{.weapon_class = WeaponClass::heavy}},
    {.effect = BodyEffect::shot_energy,
        .duration_ms = 98U,
        .event = Event{.weapon_class = WeaponClass::energy}},
    {.effect = BodyEffect::shot_launcher,
        .duration_ms = 145U,
        .event = Event{.weapon_class = WeaponClass::launcher}},
    {.effect = BodyEffect::reload_start, .duration_ms = 90U, .event = std::nullopt},
    {.effect = BodyEffect::reload_finish, .duration_ms = 90U, .event = std::nullopt},
    dry_fire,
    {.effect = BodyEffect::unarmed_swing,
        .duration_ms = 100U,
        .event = Event{.kind = EventKind::melee_swing, .weapon_class = WeaponClass::unarmed}},
    {.effect = BodyEffect::melee_swing,
        .duration_ms = 115U,
        .event = Event{.kind = EventKind::melee_swing, .weapon_class = WeaponClass::melee}},
    {.effect = BodyEffect::melee_hit,
        .duration_ms = 135U,
        .event = Event{.kind = EventKind::hit,
            .target = 0x99U,
            .weapon_class = WeaponClass::melee,
            .confirmed_damage = true}},
    {.effect = BodyEffect::block_impact,
        .duration_ms = 100U,
        .event = Event{.kind = EventKind::hit,
            .source = 0x99U,
            .target = player_id,
            .hit_outcome = HitOutcome::blocked}},
    {.effect = BodyEffect::explosion,
        .duration_ms = 160U,
        .event =
            Event{.kind = EventKind::explosion, .target = player_id, .confirmed_damage = true}},
    {.effect = BodyEffect::explosion_dealt,
        .duration_ms = 110U,
        .event =
            Event{.kind = EventKind::explosion_dealt, .target = 0x99U, .confirmed_damage = true}},
    {.effect = BodyEffect::throwing,
        .duration_ms = 150U,
        .event = Event{.kind = EventKind::thrown}},
    {.effect = BodyEffect::vats_select,
        .duration_ms = 75U,
        .event = Event{.mode = Mode::vats_selection, .kind = EventKind::vats_select}},
    {.effect = BodyEffect::vats_accept,
        .duration_ms = 100U,
        .event = Event{.mode = Mode::vats_selection, .kind = EventKind::vats_accept}},
    {.effect = BodyEffect::aid,
        .duration_ms = 135U,
        .event = Event{.mode = Mode::menu, .menu = Menu::inventory, .kind = EventKind::aid}},
    {.effect = BodyEffect::equip,
        .duration_ms = 90U,
        .event = Event{.mode = Mode::menu, .menu = Menu::inventory, .kind = EventKind::equip}},
    {.effect = BodyEffect::repair,
        .duration_ms = 100U,
        .event = Event{.mode = Mode::menu, .menu = Menu::repair, .kind = EventKind::repair}},
    {.effect = BodyEffect::pick_lock,
        .duration_ms = 110U,
        .event = Event{.mode = Mode::menu, .menu = Menu::lockpick, .kind = EventKind::pick_lock}},
    {.effect = BodyEffect::break_lockpick,
        .duration_ms = 80U,
        .event =
            Event{.mode = Mode::menu, .menu = Menu::lockpick, .kind = EventKind::break_lockpick}},
    {.effect = BodyEffect::terminal_success,
        .duration_ms = 92U,
        .event =
            Event{.mode = Mode::menu, .menu = Menu::terminal, .kind = EventKind::terminal_success}},
    {.effect = BodyEffect::terminal_failure,
        .duration_ms = 112U,
        .event =
            Event{.mode = Mode::menu, .menu = Menu::terminal, .kind = EventKind::terminal_failure}},
});

Settings settings_with_audio() {
    Settings settings{};
    constexpr auto game = AudioHapticGame::fallout_new_vegas;
    const auto builtin = inputnext::adapters::builtin_audio_haptic_bank(game);
    REQUIRE(builtin.status == AudioHapticBankStatus::loaded);
    REQUIRE(bind_audio_haptics(builtin.bank, settings, game) == profiles.size());
    return settings;
}

inputnext::adapters::BodyVoice emitted_voice(const ProfileCase& profile, const Settings& settings) {
    const auto policy = std::make_unique<Policy>(settings);
    auto observation = gameplay();
    if (profile.event) {
        auto event = *profile.event;
        if (event.mode != Mode::unavailable) {
            observation.mode = event.mode;
            observation.menu = event.menu;
        }
        event.at = observation.at;
        event.epoch = observation.epoch;
        event.frame = observation.frame;
        event.action = 100U + static_cast<std::uint64_t>(profile.effect);
        if (event.source == 0U) {
            event.source = observation.player;
        }
        event.weapon = observation.weapon.id;
        event.mode = observation.mode;
        const std::array events{event};
        return voice_for(policy->update(observation, events, observation.at), profile.effect);
    }

    static_cast<void>(policy->update(observation, {}, observation.at));
    ++observation.frame;
    observation.at += 10ms;
    observation.reloading = true;
    const auto started = policy->update(observation, {}, observation.at);
    if (profile.effect == BodyEffect::reload_start) {
        return voice_for(started, profile.effect);
    }
    ++observation.frame;
    observation.at += 10ms;
    observation.weapon.loaded_ammo = 6U;
    observation.reloading = false;
    return voice_for(policy->update(observation, {}, observation.at), profile.effect);
}
} // namespace

TEST_CASE("fallout_gamebryo.policy.embedded_profiles_render_both_channels_at_the_manifest_duration",
    "[fallout-gamebryo][policy][audio]") {
    const auto settings = settings_with_audio();
    for (const auto& profile : profiles) {
        CAPTURE(static_cast<std::size_t>(profile.effect));
        const auto voice = emitted_voice(profile, settings);
        CHECK(voice.duration_ms == profile.duration_ms);
        CHECK(voice.wave.audio_envelope.enabled);
        const auto [left_peak, right_peak] = pcm_peaks(voice);
        CHECK(left_peak > 0.001F);
        CHECK(right_peak > 0.001F);
    }
}

TEST_CASE("The baked dry-fire click retains energy after one late output tick",
    "[fallout-gamebryo][policy][audio]") {
    const auto settings = settings_with_audio();
    const auto click = emitted_voice(dry_fire, settings);
    const auto pistol = emitted_voice(pistol_shot, settings);
    const auto [left, right] = pcm_peaks(click, 20ms);
    const auto pistol_peaks = pcm_peaks(pistol);
    CHECK(left >= 0.18F);
    CHECK(right >= 0.18F);
    CHECK(left < pistol_peaks[0]);
    CHECK(right < pistol_peaks[1]);
}
} // namespace fallout_gamebryo::tests
