#include "support.hpp"
#include "test_support.hpp"

#include "effect_tuning.hpp"
#include <algorithm>
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <skyrim_adapter/body_waveform.hpp>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/model.hpp>
#include <skyrim_adapter/policy.hpp>
#include <vector>

using skyrim_adapter::test::require_value;

namespace skyrim_adapter::effect_settings_tests {
namespace {
using namespace skyrim_adapter::detail;

enum class PcmChannel : std::uint8_t {
    left = 0,
    right = 1,
};

double
magnitude_at(const std::vector<float>& interleaved, PcmChannel channel, double frequency_hz) {
    const std::size_t frames = interleaved.size() / 2U;
    double real{};
    double imaginary{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double phase = 2.0 * std::numbers::pi * frequency_hz * static_cast<double>(frame) /
                             static_cast<double>(body_pcm_sample_rate);
        const double sample = interleaved[(frame * 2U) + static_cast<std::size_t>(channel)];
        real += sample * std::cos(phase);
        imaginary -= sample * std::sin(phase);
    }
    return std::hypot(real, imaginary) / static_cast<double>(frames);
}

} // namespace

TEST_CASE("skyrim.effect_settings.duration_scales_before_expiry_and_stretch_the_full_envelope",
    "[component][skyrim][effect_settings]") {
    const auto config = std::make_unique<PolicyConfig>();
    config->effects.body[static_cast<std::size_t>(EffectId::sword_swing)].duration_scale = 2.0F;
    const auto policy_owner = std::make_unique<Policy>(*config);
    Policy& policy = *policy_owner;
    Action swing = action(EventKind::weapon_swing, Hand::right, at(1000), 40);
    swing.source_known = true;
    swing.source_kind = EquipmentKind::weapon;
    swing.weapon = WeaponClass::sword;
    const std::array<Action, 1> actions{swing};
    const EffectFrame aged = policy.step(observation(at(1060)), actions, at(1060));
    INFO("aged right hand transient cue missing");
    REQUIRE(aged.body.hand_transient[1].has_value());
    INFO("aged rumble cue missing");
    REQUIRE(aged.rumble.has_value());
    const auto& aged_cue = require_value(aged.body.hand_transient[1]);
    const auto& aged_rumble = require_value(aged.rumble);
    INFO("duration tuning keeps the cue after its original 60ms lifetime");
    CHECK(aged_cue.duration_ms == 120);
    CHECK(aged_cue.started_at == at(1000));
    CHECK(aged_rumble.ttl_ms == 60);

    const auto stale_policy_owner = std::make_unique<Policy>(*config);
    Policy& stale_policy = *stale_policy_owner;
    Action stale = swing;
    stale.event.observed_at = at(909);
    stale.event.sequence = 41;
    const std::array<Action, 1> stale_actions{stale};
    const EffectFrame stale_frame =
        stale_policy.step(observation(at(1060)), stale_actions, at(1060));
    INFO("duration tuning does not bypass the independent native event freshness limit");
    CHECK(!stale_frame.body.hand_transient[1].has_value());

    const auto settings_storage = std::make_unique<EffectSettings>();
    auto& settings = *settings_storage;
    settings.body[static_cast<std::size_t>(EffectId::stamina_depleted)].duration_scale = 2.0F;
    BodyCue pulse{};
    pulse.world_kind = WorldCueKind::stamina_depleted;
    pulse.started_at = at(0);
    pulse.duration_ms = 180;
    pulse.scale = 1.0F;
    INFO("a double-pulse cue doubles its expiry and retains the renderer time scale");
    CHECK(tune_body_cue(pulse, settings));
    CHECK(pulse.duration_ms == 360);
    CHECK(pulse.duration_scale == 2.0F);

    float low = 0.12F;
    float high = 0.16F;
    float second_low = 0.07F;
    float second_high = 0.10F;
    std::uint32_t duration = 180;
    std::uint32_t peak_ms = 45;
    std::uint32_t gap_ms = 45;
    std::uint32_t second_duration = 45;
    tune_classic_recipe(pulse,
        detail::ClassicRumbleTuning{
            .low = low,
            .high = high,
            .second_low = second_low,
            .second_high = second_high,
            .duration_ms = duration,
            .peak_ms = peak_ms,
            .gap_ms = gap_ms,
            .second_duration_ms = second_duration,
        });
    INFO("classic pulse onset, gap, second pulse and expiry all stretch together");
    CHECK(duration == 360);
    CHECK(peak_ms == 90);
    CHECK(gap_ms == 90);
    CHECK(second_duration == 90);

    pulse.classic_scale = 1.5F;
    tune_classic_recipe(pulse,
        detail::ClassicRumbleTuning{
            .low = low,
            .high = high,
            .second_low = second_low,
            .second_high = second_high,
            .duration_ms = duration,
            .peak_ms = peak_ms,
            .gap_ms = gap_ms,
            .second_duration_ms = second_duration,
        });
    INFO("classic rumble tuning updates and scales all output motor amplitudes");
    CHECK(std::abs(low - 0.18F) < 1.0e-5F);
    CHECK(std::abs(high - 0.24F) < 1.0e-5F);
    CHECK(std::abs(second_low - 0.105F) < 1.0e-5F);
    CHECK(std::abs(second_high - 0.15F) < 1.0e-5F);

    const auto plan_storage = std::make_unique<BodyPlan>();
    auto& plan = *plan_storage;
    plan.body_transient = pulse;
    std::vector<float> gap(std::size_t{4096} * 2U);
    std::vector<float> second_pulse(std::size_t{4096} * 2U);
    std::vector<float> expired(std::size_t{body_pcm_max_frames} * 2U, 1.0F);
    (void)build_body_pcm(plan, at(90), gap);
    (void)build_body_pcm(plan, at(200), second_pulse);
    (void)build_body_pcm(plan, at(360), expired);
    INFO("HD pulse cadence stretches its silent gap across streamed PCM windows");
    CHECK(peak(gap) == 0.0F);
    INFO("HD second pulse remains audible after its stretched 180ms onset");
    CHECK(peak(second_pulse) > 0.0F);
    INFO("scaled transient reaches an exact silent boundary after 360ms");
    CHECK(std::ranges::all_of(expired, [](float sample) { return sample == 0.0F; }));
}

TEST_CASE("skyrim.effect_settings.frequency_ratio_identity_and_sanitization",
    "[component][skyrim][effect_settings]") {
    BodyCue pulse{};
    pulse.world_kind = WorldCueKind::stamina_depleted;
    pulse.started_at = at(0);
    pulse.duration_ms = 360;
    pulse.duration_scale = 2.0F;
    pulse.frequency_hz = 320.0F;
    pulse.scale = 1.0F;
    pulse.stable_id = 1234;
    const auto tuned_plan_storage = std::make_unique<BodyPlan>();
    auto& tuned_plan = *tuned_plan_storage;
    tuned_plan.body_transient = pulse;
    std::vector<float> tuned(std::size_t{body_pcm_sample_rate / 10U} * 2U);
    (void)build_body_pcm(tuned_plan, at(200), tuned);

    BodyCue authored = pulse;
    authored.frequency_hz = 0.0F;
    const auto authored_plan_storage = std::make_unique<BodyPlan>();
    auto& authored_plan = *authored_plan_storage;
    authored_plan.body_transient = authored;
    std::vector<float> original(tuned.size());
    (void)build_body_pcm(authored_plan, at(200), original);
    INFO("FrequencyHz replaces the primary and secondary carriers while preserving their ratio");
    CHECK(magnitude_at(tuned, PcmChannel::left, 320.0) >
          magnitude_at(tuned, PcmChannel::left, 64.0) * 1.5);
    CHECK(magnitude_at(tuned, PcmChannel::left, 500.0) >
          magnitude_at(tuned, PcmChannel::left, 100.0) * 1.5);
    INFO("FrequencyHz zero preserves the authored dual-carrier default recipe");
    CHECK(magnitude_at(original, PcmChannel::left, 64.0) >
          magnitude_at(original, PcmChannel::left, 320.0) * 1.5);
    CHECK(magnitude_at(original, PcmChannel::left, 100.0) >
          magnitude_at(original, PcmChannel::left, 500.0) * 1.5);

    const auto identity_a_storage = std::make_unique<BodyPlan>();
    auto& identity_a = *identity_a_storage;
    identity_a.body_transient = authored;
    const auto identity_b_storage = std::make_unique<BodyPlan>(identity_a);
    auto& identity_b = *identity_b_storage;
    auto transient_b = authored;
    transient_b.hd_scale = 1.5F;
    identity_b.body_transient = transient_b;
    INFO("HDScale participates in PCM plan identity");
    CHECK(!same_body_plan_identity(identity_a, identity_b));
    transient_b = authored;
    transient_b.classic_scale = 0.5F;
    identity_b.body_transient = transient_b;
    INFO("ClassicScale participates in PCM plan identity");
    CHECK(!same_body_plan_identity(identity_a, identity_b));
    transient_b = authored;
    transient_b.duration_scale = 1.5F;
    identity_b.body_transient = transient_b;
    INFO("DurationScale participates in PCM plan identity");
    CHECK(!same_body_plan_identity(identity_a, identity_b));
    transient_b = authored;
    transient_b.frequency_hz = 250.0F;
    identity_b.body_transient = transient_b;
    INFO("FrequencyHz participates in PCM plan identity");
    CHECK(!same_body_plan_identity(identity_a, identity_b));

    const auto invalid_storage = std::make_unique<EffectSettings>();
    auto& invalid = *invalid_storage;
    invalid.body[static_cast<std::size_t>(EffectId::fire_release)] = {
        .enabled = true,
        .hd_scale = std::numeric_limits<float>::quiet_NaN(),
        .classic_scale = 3.0F,
        .duration_scale = 4.0F,
        .frequency_hz = std::numeric_limits<float>::infinity(),
        .audio_haptic = {},
    };
    invalid.body[static_cast<std::size_t>(EffectId::fire_held)].duration_scale = 2.0F;
    invalid.triggers[static_cast<std::size_t>(TriggerId::weapon_block)].start =
        std::numeric_limits<float>::infinity();
    invalid.triggers[static_cast<std::size_t>(TriggerId::weapon_block)].frequency_code = 200U;
    invalid.triggers[static_cast<std::size_t>(TriggerId::fire)].frequency_code = 500U;
    sanitize_effect_settings(invalid);
    INFO("programmatic body settings receive finite bounded defaults");
    CHECK(invalid.body[static_cast<std::size_t>(EffectId::fire_release)].hd_scale == 1.0F);
    CHECK(invalid.body[static_cast<std::size_t>(EffectId::fire_release)].classic_scale == 2.0F);
    CHECK(invalid.body[static_cast<std::size_t>(EffectId::fire_release)].duration_scale == 2.0F);
    CHECK(invalid.body[static_cast<std::size_t>(EffectId::fire_release)].frequency_hz == 0.0F);
    INFO("held durations and unsupported or out-of-range trigger fields are sanitized");
    CHECK(invalid.body[static_cast<std::size_t>(EffectId::fire_held)].duration_scale == 1.0F);
    CHECK(!invalid.triggers[static_cast<std::size_t>(TriggerId::weapon_block)].start.has_value());
    CHECK(!invalid.triggers[static_cast<std::size_t>(TriggerId::weapon_block)]
            .frequency_code.has_value());
    CHECK(!invalid.triggers[static_cast<std::size_t>(TriggerId::fire)].frequency_code.has_value());
}

} // namespace skyrim_adapter::effect_settings_tests
