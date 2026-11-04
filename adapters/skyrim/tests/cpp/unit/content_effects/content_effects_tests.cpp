#include "skyrim_adapter/content_effects.hpp"
#include "skyrim_adapter/content_observation.hpp"
#include "skyrim_adapter/model.hpp"
#include "support/test_runner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>

namespace {
using namespace skyrim_adapter;

int failures{};

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

MonoTime at(std::int64_t milliseconds) {
    return MonoTime{} + std::chrono::milliseconds{milliseconds};
}

std::array<std::optional<ContentPulse>, 4> step(ContentEffects& effects,
    const ContentObservation& observation,
    std::int64_t milliseconds,
    FormId player = 1) {
    return effects.step(observation, player, at(milliseconds), at(milliseconds));
}

void check_quiet(const std::array<std::optional<ContentPulse>, 4>& pulses,
    std::string_view message) {
    check(std::ranges::none_of(pulses, [](const auto& pulse) { return pulse.has_value(); }),
        message);
}

void check_pulse_shape(const ContentPulse& pulse) {
    check(pulse.priority >= 7 && pulse.priority <= 9,
        "content cue priority stays below held feedback");
    check(pulse.duration_ms > 0 && pulse.duration_ms <= 400,
        "content cue duration stays finite and short");
    check(pulse.low >= 0.15F && pulse.high <= 0.35F && pulse.low <= pulse.high,
        "classic rumble amplitudes stay within calibrated bounds");
}

void test_need_edges_and_rearming() {
    ContentEffects effects;
    ContentObservation observation{};
    observation.beast = BeastForm::none;
    observation.survival_enabled = true;
    observation.cold_stage = 2U;
    observation.hunger_stage = 2U;
    observation.exhaustion_stage = 2U;
    check_quiet(step(effects, observation, 0), "first known need sample is a silent baseline");

    for (unsigned stage = 3; stage <= 5; ++stage) {
        observation.cold_stage = stage;
        observation.hunger_stage = stage;
        observation.exhaustion_stage = stage;
        const auto pulses = step(effects, observation, static_cast<std::int64_t>(stage) * 20);
        for (std::size_t domain = 0; domain < 3; ++domain) {
            const auto& pulse = pulses[domain];
            check(pulse && pulse->stage == stage,
                "each increasing elevated need stage emits one finite cue");
            if (pulse) {
                check(static_cast<unsigned>(pulse->kind) == domain,
                    "need cue remains associated with its independent domain");
                check(std::cmp_equal(pulse->priority, stage + 4U),
                    "need stages retain their bounded 7/8/9 priority ladder");
                check_pulse_shape(*pulse);
            }
        }
    }

    check_quiet(step(effects, observation, 120), "a held maximum need stage does not repeat");
    observation.cold_stage = 4U;
    observation.hunger_stage = 4U;
    observation.exhaustion_stage = 4U;
    check_quiet(step(effects, observation, 140), "a genuine need decrease rearms silently");
    observation.cold_stage = 5U;
    observation.hunger_stage = 5U;
    observation.exhaustion_stage = 5U;
    const auto rearmed = step(effects, observation, 160);
    check(rearmed[0] && rearmed[1] && rearmed[2],
        "a later rising stage emits again after a decrease");
}

void test_missing_disabled_invalid_and_discontinuous_samples() {
    ContentEffects effects;
    ContentObservation observation{};
    observation.beast = BeastForm::none;
    observation.survival_enabled = true;
    observation.cold_stage = 2U;
    observation.hunger_stage = 2U;
    observation.exhaustion_stage = 2U;
    check_quiet(step(effects, observation, 0), "reset test starts from a quiet baseline");

    observation.cold_stage.reset();
    observation.hunger_stage = 3U;
    observation.exhaustion_stage = 3U;
    const auto independent = step(effects, observation, 20);
    check(independent[1] && independent[2],
        "missing cold data does not reset available need domains");
    observation.cold_stage = 5U;
    check_quiet(step(effects, observation, 40),
        "first cold sample after missing data is a silent baseline");

    observation.survival_enabled = false;
    observation.cold_stage = 5U;
    observation.hunger_stage = 5U;
    observation.exhaustion_stage = 5U;
    check_quiet(step(effects, observation, 60),
        "disabled Survival Mode clears all need histories silently");
    observation.survival_enabled = true;
    check_quiet(step(effects, observation, 80),
        "first sample after re-enabling is a silent baseline");

    observation.cold_stage = 4U;
    observation.hunger_stage = 4U;
    observation.exhaustion_stage = 4U;
    check_quiet(step(effects, observation, 260),
        "a sample gap resets every domain to a quiet baseline");
    observation.cold_stage = 5U;
    observation.hunger_stage = 5U;
    observation.exhaustion_stage = 5U;
    const auto after_gap = step(effects, observation, 280);
    check(after_gap[0] && after_gap[1] && after_gap[2],
        "edges resume after the fresh post-gap baseline");

    check_quiet(effects.step(observation, 1, at(280), at(300)),
        "duplicate timestamp cannot replay an edge");
    check_quiet(effects.step(observation, 1, at(270), at(300)),
        "older timestamp resets history quietly");

    observation.cold_stage = 5U;
    observation.hunger_stage = 5U;
    observation.exhaustion_stage = 5U;
    check_quiet(step(effects, observation, 290),
        "first newer sample after an older timestamp is a silent baseline");
    observation.cold_stage = 4U;
    observation.hunger_stage = 4U;
    observation.exhaustion_stage = 4U;
    check_quiet(step(effects, observation, 310),
        "decreasing from the post-reset baseline rearms quietly");
    observation.cold_stage = 5U;
    observation.hunger_stage = 5U;
    observation.exhaustion_stage = 5U;
    const auto after_older = step(effects, observation, 330);
    check(after_older[0] && after_older[1] && after_older[2],
        "normal need edges resume after a fresh baseline following an older timestamp");

    observation.cold_stage = 3U;
    observation.hunger_stage = 3U;
    observation.exhaustion_stage = 3U;
    check_quiet(effects.step(observation, 1, at(341), at(340)), "future sample resets quietly");
    check_quiet(step(effects, observation, 360),
        "first fresh sample after invalid time is a baseline");

    observation.cold_stage = 4U;
    observation.hunger_stage = 4U;
    observation.exhaustion_stage = 4U;
    check_quiet(effects.step(observation, 0, at(380), at(380)),
        "missing player identity resets quietly");
    check_quiet(step(effects, observation, 400),
        "valid sample after missing player starts a new baseline");

    effects.reset();
    check_quiet(step(effects, observation, 420),
        "explicit reset makes the next sample a silent baseline");
}

void test_beast_form_transitions() {
    ContentEffects effects;
    ContentObservation observation{};
    observation.survival_enabled = false;
    observation.beast = BeastForm::none;
    check_quiet(step(effects, observation, 0), "ordinary known race establishes a silent baseline");

    observation.beast = BeastForm::werewolf;
    auto pulses = step(effects, observation, 20);
    const auto& werewolf_pulse = pulses[3];
    check(werewolf_pulse && werewolf_pulse->kind == ContentCueKind::transformation &&
              werewolf_pulse->beast == BeastForm::werewolf,
        "known entry into werewolf form emits a typed transformation cue");
    if (werewolf_pulse) {
        check(werewolf_pulse->priority == 9,
            "transformation cues remain below held and ordinary transient body priority");
        check_pulse_shape(*werewolf_pulse);
    }
    check_quiet(step(effects, observation, 40), "held beast form does not replay a transition");

    observation.beast = BeastForm::none;
    pulses = step(effects, observation, 60);
    const auto& leaving_pulse = pulses[3];
    check(leaving_pulse && leaving_pulse->beast == BeastForm::werewolf,
        "leaving beast form identifies the departing form");

    observation.beast.reset();
    check_quiet(step(effects, observation, 80),
        "unknown race sample clears only its transition baseline");
    observation.beast = BeastForm::vampire_lord;
    check_quiet(step(effects, observation, 100),
        "first known race after unknown data is a silent baseline");

    observation.beast = BeastForm::werewolf;
    pulses = step(effects, observation, 120);
    const auto& direct_pulse = pulses[3];
    check(direct_pulse && direct_pulse->beast == BeastForm::werewolf,
        "direct transition between known typed forms reports the new form");
}

int run_tests() {
    test_need_edges_and_rearming();
    test_missing_disabled_invalid_and_discontinuous_samples();
    test_beast_form_transitions();

    if (failures != 0) {
        std::cerr << failures << " content effects assertion(s) failed\n";
        return 1;
    }
    std::cout << "content effects tests passed\n";
    return 0;
}
} // namespace

int main() {
    return inputnext::test::run_main(__FILE__, [] { return run_tests(); });
}
