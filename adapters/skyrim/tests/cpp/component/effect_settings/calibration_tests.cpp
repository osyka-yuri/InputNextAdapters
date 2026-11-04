#include "support.hpp"

#include "effect_recipes.hpp"
#include "effect_tuning.hpp"
#include "test_support.hpp"
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <input_next_effects.h>
#include <inputnext_adapters/body_program.hpp>
#include <skyrim_adapter/body_waveform.hpp>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/model.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace skyrim_adapter::effect_settings_tests {
using skyrim_adapter::test::require_value;

namespace {
struct SignalMeasure {
    double energy{};
    float peak{};
    std::array<double, 2> channel_rms{};
};

struct TimbreMeasure {
    double centroid_hz{};
    double upper_band_fraction{};
};

struct VoiceOptions {
    float scale{1.0F};
    std::uint64_t seed{0xCA11B4A7U};
};

struct Window {
    std::int64_t delay_ms{};
    std::uint32_t length_ms{200};
};

BodyPlan plan_for(BodyCue cue, VoiceOptions options = {}) {
    cue.scale = options.scale;
    cue.stable_id = options.seed;
    BodyPlan plan{};
    if (cue.kind == BodyCueKind::melee_swing && cue.weapon != WeaponClass::two_handed) {
        plan.hand_transient[1] = cue;
    } else {
        plan.body_transient = cue;
    }
    return plan;
}

SignalMeasure measure(const BodyPlan& plan, Window window = {}) {
    const auto frames = static_cast<std::size_t>(window.length_ms) * (body_pcm_sample_rate / 1000U);
    REQUIRE(frames > 0);
    std::vector<float> pcm(frames * 2U);
    const auto rendered = build_body_pcm(plan, at(1000 + window.delay_ms), pcm);
    REQUIRE(rendered.frame_count == frames);
    SignalMeasure result{};
    std::array<double, 2> channel_energy{};
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        const float sample = pcm[index];
        const double squared = static_cast<double>(sample) * static_cast<double>(sample);
        result.energy += squared;
        channel_energy[index % 2U] += squared;
        result.peak = std::max(result.peak, std::abs(sample));
    }
    for (std::size_t channel = 0; channel < channel_energy.size(); ++channel) {
        result.channel_rms[channel] =
            std::sqrt(channel_energy[channel] / static_cast<double>(frames));
    }
    return result;
}

TimbreMeasure measure_timbre(const BodyPlan& plan) {
    // A fixed 200 ms window includes the whole default cue and its trailing silence. The helper
    // routes all test families to the right channel. These are digital 20-500 Hz descriptors.
    constexpr std::size_t frames = body_pcm_sample_rate / 5U;
    std::vector<float> pcm(frames * 2U);
    REQUIRE(build_body_pcm(plan, at(1000), pcm).frame_count == frames);
    double energy{};
    double weighted_energy{};
    double upper_energy{};
    for (unsigned frequency = 20; frequency <= 500; frequency += 5) {
        const double coefficient =
            2.0 * std::cos(2.0 * std::numbers::pi * static_cast<double>(frequency) /
                           static_cast<double>(body_pcm_sample_rate));
        double previous{};
        double previous_previous{};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const double current = static_cast<double>(pcm[(frame * 2U) + 1U]) +
                                   (coefficient * previous) - previous_previous;
            previous_previous = previous;
            previous = current;
        }
        const double power = std::max(0.0,
            (previous * previous) + (previous_previous * previous_previous) -
                (coefficient * previous * previous_previous));
        energy += power;
        weighted_energy += static_cast<double>(frequency) * power;
        if (frequency >= 150) {
            upper_energy += power;
        }
    }
    REQUIRE(energy > 0.0);
    return {.centroid_hz = weighted_energy / energy, .upper_band_fraction = upper_energy / energy};
}

detail::ActionRecipe melee_action(WeaponClass weapon, bool contact, std::uint32_t flags = 0) {
    Action event =
        action(contact ? EventKind::hit : EventKind::weapon_swing, Hand::right, at(1000), 1);
    event.source_known = true;
    event.source_kind = EquipmentKind::weapon;
    event.weapon = weapon;
    event.outgoing = contact;
    event.event.flags = flags;
    return detail::action_recipe(event, observation(at(1000)), at(1000));
}

detail::ActionRecipe incoming_action() {
    Action event = action(EventKind::hit, Hand::unknown, at(1000), 2);
    event.incoming = true;
    return detail::action_recipe(event, observation(at(1000)), at(1000));
}

void check_delayed_tail(const BodyPlan& plan) {
    const auto baseline = measure(plan);
    REQUIRE(baseline.energy > 0.0);
    INFO("short contacts retain the authored energy fraction at the original event time");
    CHECK(measure(plan, {.delay_ms = 20}).energy >= baseline.energy * 0.20);
    CHECK(measure(plan, {.delay_ms = 40}).energy >= baseline.energy * 0.02);
}
} // namespace

TEST_CASE("skyrim.calibration.melee_digital_signatures_and_contact_hierarchy",
    "[component][skyrim][effect_settings][calibration]") {
    constexpr std::array weapons{WeaponClass::unarmed,
        WeaponClass::dagger,
        WeaponClass::sword,
        WeaponClass::axe,
        WeaponClass::mace,
        WeaponClass::two_handed};
    std::array<inputnext::adapters::WaveRecipe, weapons.size()> signatures{};
    for (std::size_t index = 0; index < weapons.size(); ++index) {
        CAPTURE(weapons[index]);
        const auto swing = melee_action(weapons[index], false);
        const auto contact = melee_action(weapons[index], true);
        const auto power =
            melee_action(weapons[index], true, static_cast<std::uint32_t>(HitFlag::power_attack));
        REQUIRE(swing.body.has_value());
        REQUIRE(contact.body.has_value());
        REQUIRE(power.body.has_value());
        const auto& swing_cue = require_value(swing.body);
        const auto& contact_cue = require_value(contact.body);
        const auto& power_cue = require_value(power.body);
        CHECK(swing_cue.duration_ms == swing.rumble.duration_ms);
        CHECK(contact_cue.duration_ms == contact.rumble.duration_ms);
        CHECK(power_cue.duration_ms >= contact_cue.duration_ms);
        CHECK(power_cue.duration_ms <= 120);
        CHECK(contact_cue.duration_ms >= swing_cue.duration_ms);

        const auto swing_plan = plan_for(swing_cue);
        const auto contact_plan = plan_for(contact_cue);
        INFO("confirmed contact carries more stereo energy than its family's preparatory swing");
        CHECK(measure(contact_plan).energy > measure(swing_plan).energy);
        check_delayed_tail(swing_plan);
        check_delayed_tail(contact_plan);

        const auto program = make_body_program(contact_plan);
        REQUIRE(program.voices[4].has_value());
        signatures[index] = require_value(program.voices[4]).wave;
        for (std::size_t other = 0; other < index; ++other) {
            CHECK(signatures[index] != signatures[other]);
        }
    }
    INFO("authored digital profiles differ under the same noise seed; perceived separation is "
         "unverified");
    // The rendered timbre checks below cover spectral groups without a total carrier ladder.
    CHECK(signatures[0].level < signatures[1].level);
    CHECK(signatures[3].release_ms > signatures[2].release_ms);
}

TEST_CASE("skyrim.calibration.melee_timbre_components_keep_variant_ratios",
    "[component][skyrim][effect_settings][calibration]") {
    constexpr std::array weapons{WeaponClass::two_handed,
        WeaponClass::mace,
        WeaponClass::axe,
        WeaponClass::unarmed,
        WeaponClass::sword,
        WeaponClass::dagger};
    constexpr auto power = static_cast<std::uint32_t>(HitFlag::power_attack);
    constexpr auto blocked = static_cast<std::uint32_t>(HitFlag::blocked);
    constexpr auto bash = static_cast<std::uint32_t>(HitFlag::bash_attack);
    for (const auto flags : {0U, power, blocked, bash, power | blocked | bash}) {
        for (const bool contact : {false, true}) {
            for (const bool shield_block : {false, true}) {
                for (const auto weapon : weapons) {
                    CAPTURE(flags, contact, shield_block, weapon);
                    const auto action = melee_action(weapon, contact, flags);
                    REQUIRE(action.body.has_value());
                    auto cue = require_value(action.body);
                    cue.shield_block = shield_block;
                    const auto program = make_body_program(plan_for(cue));
                    const auto voice = std::ranges::find_if(program.voices,
                        [](const auto& candidate) { return candidate.has_value(); });
                    REQUIRE(voice != program.voices.end());
                    const float frequency = require_value(*voice).wave.frequency;
                    CHECK(frequency >= 20.0F);
                    CHECK(frequency <= 1000.0F);
                    const auto& wave = require_value(*voice).wave;
                    const auto reference = melee_action(weapon, contact);
                    REQUIRE(reference.body.has_value());
                    const auto reference_program =
                        make_body_program(plan_for(require_value(reference.body)));
                    const auto reference_voice = std::ranges::find_if(reference_program.voices,
                        [](const auto& candidate) { return candidate.has_value(); });
                    REQUIRE(reference_voice != reference_program.voices.end());
                    const auto& base = require_value(*reference_voice).wave;
                    INFO("power/block/bash preserve the relationship between body and resonance");
                    CHECK(std::abs((wave.second_frequency / frequency) -
                                   (base.second_frequency / base.frequency)) < 1.0e-5F);
                    CHECK(wave.second_mix == base.second_mix);
                }
            }
        }
    }
}

TEST_CASE("skyrim.calibration.melee_blunt_and_resonant_digital_timbres",
    "[component][skyrim][effect_settings][calibration]") {
    constexpr std::array blunt{WeaponClass::unarmed, WeaponClass::mace};
    constexpr std::array blades{WeaponClass::dagger, WeaponClass::sword, WeaponClass::axe};
    for (const bool contact : {false, true}) {
        for (std::uint64_t seed = 1; seed <= 4; ++seed) {
            double maximum_blunt_centroid{};
            std::array<double, blunt.size()> blunt_centroids{};
            std::size_t blunt_index{};
            for (const auto weapon : blunt) {
                CAPTURE(contact, seed, weapon);
                const auto action = melee_action(weapon, contact);
                REQUIRE(action.body.has_value());
                const auto plan = plan_for(require_value(action.body), {.seed = seed});
                const auto timbre = measure_timbre(plan);
                blunt_centroids[blunt_index++] = timbre.centroid_hz;
                CHECK(timbre.upper_band_fraction < 0.15);
                maximum_blunt_centroid = std::max(maximum_blunt_centroid, timbre.centroid_hz);
                CHECK(action.rumble.low > action.rumble.high);
            }
            double previous_centroid = 1000.0;
            float previous_body_mix{};
            float previous_decay = 1000.0F;
            for (const auto weapon : blades) {
                CAPTURE(contact, seed, weapon);
                const auto action = melee_action(weapon, contact);
                REQUIRE(action.body.has_value());
                const auto plan = plan_for(require_value(action.body), {.seed = seed});
                const auto timbre = measure_timbre(plan);
                const auto program = make_body_program(plan);
                const auto voice = std::ranges::find_if(program.voices,
                    [](const auto& candidate) { return candidate.has_value(); });
                REQUIRE(voice != program.voices.end());
                const auto& wave = require_value(*voice).wave;
                INFO("authored digital brightness budgets are not material-recognition scores");
                if (contact) {
                    CHECK(timbre.upper_band_fraction > 0.45);
                    CHECK(timbre.centroid_hz > maximum_blunt_centroid * 1.4);
                    CHECK(std::abs(timbre.centroid_hz - blunt_centroids[0]) >
                          std::abs(blunt_centroids[1] - blunt_centroids[0]));
                    CHECK(wave.second_frequency > 0.0F);
                    CHECK(wave.second_mix > previous_body_mix);
                } else {
                    INFO("a preparatory swing contains no impact-excited ringing component");
                    CHECK(timbre.upper_band_fraction < 0.15);
                    CHECK(wave.second_frequency == 0.0F);
                    CHECK(wave.second_mix == 0.0F);
                }
                CHECK(timbre.centroid_hz < previous_centroid);
                CHECK(wave.second_frequency < wave.frequency);
                CHECK(wave.decay_per_second < previous_decay);
                previous_centroid = timbre.centroid_hz;
                previous_body_mix = wave.second_mix;
                previous_decay = wave.decay_per_second;
            }
        }
    }
}

TEST_CASE("skyrim.calibration.footsteps_keep_quiet_delayed_tails",
    "[component][skyrim][effect_settings][calibration]") {
    const auto incoming = incoming_action();
    REQUIRE(incoming.body.has_value());
    const auto impact = measure(plan_for(require_value(incoming.body)));
    for (const bool mounted : {false, true}) {
        for (const auto gait : {MovementGait::walking,
                 MovementGait::running,
                 MovementGait::sprinting,
                 MovementGait::sneaking}) {
            if (mounted && gait == MovementGait::sneaking) {
                continue;
            }
            CAPTURE(mounted, gait);
            const auto tuning = detail::footstep_tuning(gait, mounted);
            BodyCue cue{};
            cue.kind = BodyCueKind::footstep;
            cue.gait = gait;
            cue.mounted = mounted;
            cue.duration_ms = tuning.duration_ms;
            cue.started_at = at(1000);
            const auto plan = plan_for(cue);
            check_delayed_tail(plan);
            INFO("longer foot contact tails keep a lower peak than combat damage");
            CHECK(measure(plan).peak < impact.peak * 0.5F);
            CHECK(tuning.fallback_low < incoming.rumble.low * 0.5F);
            CHECK(tuning.fallback_high < incoming.rumble.high * 0.5F);
        }
    }
}

TEST_CASE("skyrim.calibration.quiet_steps_have_absolute_tail_rms",
    "[component][skyrim][effect_settings][calibration]") {
    // Authored digital budget: .01 FS plus 50% headroom, not a perceptual detection threshold.
    constexpr double minimum_tail_rms = 0.015;
    for (const bool mounted : {false, true}) {
        for (const auto gait : {MovementGait::walking,
                 MovementGait::running,
                 MovementGait::sprinting,
                 MovementGait::sneaking}) {
            if (mounted && gait == MovementGait::sneaking) {
                continue;
            }
            const auto tuning = detail::footstep_tuning(gait, mounted);
            BodyCue cue{};
            cue.kind = BodyCueKind::footstep;
            cue.gait = gait;
            cue.mounted = mounted;
            cue.duration_ms = tuning.duration_ms;
            cue.started_at = at(1000);
            for (const float scale : {1.0F, 0.5F}) {
                for (std::uint64_t seed = 1; seed <= 16; ++seed) {
                    CAPTURE(mounted, gait, scale, seed);
                    const auto delivered = measure(plan_for(cue, {.scale = scale, .seed = seed}),
                        {.delay_ms = 40, .length_ms = 20});
                    INFO("RMS uses all frames in the fixed 40-60 ms window, including silence; "
                         "each actuator channel is checked separately");
                    CHECK(delivered.channel_rms[0] >= minimum_tail_rms * scale);
                    CHECK(delivered.channel_rms[1] >= minimum_tail_rms * scale);
                }
            }
        }
    }
}

TEST_CASE("skyrim.calibration.heartbeat_is_a_quiet_background_warning",
    "[component][skyrim][effect_settings][calibration]") {
    const auto incoming = incoming_action();
    REQUIRE(incoming.body.has_value());
    const auto impact = measure(plan_for(require_value(incoming.body)));
    BodyCue heartbeat{};
    heartbeat.kind = BodyCueKind::critical_health;
    heartbeat.duration_ms = 200;
    heartbeat.started_at = at(1000);
    const auto warning = measure(plan_for(heartbeat));
    CHECK(warning.energy > 0.0);
    CHECK(warning.peak <= impact.peak * 0.40F);
    CHECK(warning.energy <= impact.energy * 0.20);
}

TEST_CASE("skyrim.calibration.default_trigger_profiles_reach_authored_device_levels",
    "[component][skyrim][effect_settings][calibration]") {
    std::array<input_next_effects_trigger_v1, static_cast<std::size_t>(TriggerId::count)>
        payloads{};
    const auto save = [&payloads](TriggerId id, const detail::TriggerRecipe& recipe) {
        REQUIRE(recipe.present);
        payloads[static_cast<std::size_t>(id)] = recipe.payload;
    };
    Observation sample = observation(at(1000));
    sample.hands[0].equipped_id = 42;
    sample.hands[0].equipment = EquipmentKind::bow;
    sample.hands[0].weapon = WeaponClass::bow;
    sample.bow = BowPhase::draw;
    save(TriggerId::bow_draw, detail::bow_draw_trigger(sample, true));
    sample.blocking = true;
    sample.hands[0].equipment = EquipmentKind::weapon;
    save(TriggerId::weapon_block, detail::block_trigger(sample));
    sample.shield_blocking = true;
    sample.hands[0].equipment = EquipmentKind::shield;
    save(TriggerId::shield_block, detail::block_trigger(sample));
    Action hit = action(EventKind::hit, Hand::unknown, at(1000), 3);
    hit.incoming = true;
    hit.event.flags = static_cast<std::uint32_t>(HitFlag::blocked);
    save(TriggerId::shield_hit, detail::action_recipe(hit, sample, at(1000)).left);

    HandObservation hand{};
    hand.caster_source_id = 43;
    hand.spell = SpellKind::fire;
    hand.cast_mode = CastMode::fire_and_forget;
    hand.cast_phase = CastPhase::charging;
    save(TriggerId::magic_charge, detail::held_magic_recipe(hand).trigger);
    hand.cast_mode = CastMode::concentration;
    hand.cast_phase = CastPhase::casting;
    constexpr std::array spells{SpellKind::fire,
        SpellKind::frost,
        SpellKind::shock,
        SpellKind::healing,
        SpellKind::ward,
        SpellKind::generic};
    for (const auto spell : spells) {
        hand.spell = spell;
        save(detail::trigger_id_for_spell(spell), detail::held_magic_recipe(hand).trigger);
    }

    // The pinned DualSense encoder maps normalized strength to eight nonzero force steps.
    constexpr std::array<long, static_cast<std::size_t>(TriggerId::count)>
        expected_levels{2, 2, 3, 5, 3, 2, 3, 3, 1, 3, 2};
    for (std::size_t index = 0; index < payloads.size(); ++index) {
        CAPTURE(trigger_descriptors[index].section);
        auto payload = payloads[index];
        REQUIRE(detail::tune_trigger_payload(payload, trigger_descriptors[index].id, {}));
        CHECK(std::lround(static_cast<double>(payload.start) * 9.0) == 1);
        CHECK(std::lround(static_cast<double>(payload.strength) * 8.0) == expected_levels[index]);
        INFO("half of the global trigger strength keeps every default profile active");
        CHECK(std::lround(static_cast<double>(payload.strength) * 0.5 * 8.0) > 0);
    }
}
} // namespace skyrim_adapter::effect_settings_tests
