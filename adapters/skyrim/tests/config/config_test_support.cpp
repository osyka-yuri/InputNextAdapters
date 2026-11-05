#include "config_test_support.hpp"
#include <cstddef>
#include <memory>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/native/config.hpp>

namespace skyrim_adapter::tests::config {

bool same_config(const skyrim_adapter::native::NativeConfig& left,
    const skyrim_adapter::native::NativeConfig& right) {
    if (left.slot != right.slot || left.trigger_scale != right.trigger_scale ||
        left.rumble_scale != right.rumble_scale ||
        left.effects.body_enabled != right.effects.body_enabled ||
        left.effects.triggers_enabled != right.effects.triggers_enabled ||
        left.effects.audio_haptics_enabled != right.effects.audio_haptics_enabled ||
        left.effects.respect_game_vibration != right.effects.respect_game_vibration ||
        left.effects.leds.enabled != right.effects.leds.enabled ||
        left.effects.leds.health_enabled != right.effects.leds.health_enabled ||
        left.effects.leds.stealth_enabled != right.effects.leds.stealth_enabled ||
        left.effects.leds.radiation_enabled != right.effects.leds.radiation_enabled ||
        left.effects.leds.health_hold_ms != right.effects.leds.health_hold_ms ||
        left.effects.leds.healing_transition_ms != right.effects.leds.healing_transition_ms ||
        left.effects.leds.critical_enter != right.effects.leds.critical_enter ||
        left.effects.leds.critical_exit != right.effects.leds.critical_exit ||
        left.effects.leds.detection_hidden_score != right.effects.leds.detection_hidden_score ||
        left.effects.leds.detection_alert_score != right.effects.leds.detection_alert_score) {
        return false;
    }
    for (std::size_t index = 0; index < left.effects.body.size(); ++index) {
        const auto& lhs = left.effects.body[index];
        const auto& rhs = right.effects.body[index];
        if (lhs.enabled != rhs.enabled || lhs.hd_scale != rhs.hd_scale ||
            lhs.classic_scale != rhs.classic_scale || lhs.duration_scale != rhs.duration_scale ||
            lhs.frequency_hz != rhs.frequency_hz || lhs.audio_haptic != rhs.audio_haptic) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.effects.triggers.size(); ++index) {
        const auto& lhs = left.effects.triggers[index];
        const auto& rhs = right.effects.triggers[index];
        if (lhs.enabled != rhs.enabled || lhs.strength_scale != rhs.strength_scale ||
            lhs.start != rhs.start || lhs.frequency_code != rhs.frequency_code) {
            return false;
        }
    }
    return true;
}

std::unique_ptr<skyrim_adapter::native::NativeConfig> make_sample_prior_config() {
    using namespace skyrim_adapter::native;
    auto storage = std::make_unique<NativeConfig>();
    auto& prior = *storage;
    prior.slot = 2;
    prior.trigger_scale = 0.5F;
    prior.rumble_scale = 0.75F;
    auto& prior_body =
        prior.effects.body[static_cast<std::size_t>(skyrim_adapter::EffectId::sword_hit)];
    prior_body = {
        .enabled = false,
        .hd_scale = 0.25F,
        .classic_scale = 1.75F,
        .duration_scale = 0.75F,
        .frequency_hz = 400.0F,
        .audio_haptic = {},
    };
    auto& prior_trigger =
        prior.effects.triggers[static_cast<std::size_t>(skyrim_adapter::TriggerId::fire)];
    prior_trigger = {
        .enabled = false,
        .strength_scale = 0.4F,
        .start = 0.2F,
        .frequency_code = 200U,
    };
    prior.effects.body_enabled = false;
    prior.effects.triggers_enabled = false;
    prior.effects.audio_haptics_enabled = false;
    prior.effects.respect_game_vibration = false;
    prior.effects.leds.health_hold_ms = 1500;
    prior.effects.leds.healing_transition_ms = 75;
    prior.effects.leds.critical_enter = 0.15F;
    prior.effects.leds.critical_exit = 0.30F;
    return storage;
}

} // namespace skyrim_adapter::tests::config
