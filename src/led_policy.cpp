#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/led_policy.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace inputnext::adapters {
namespace {
using namespace std::chrono;

constexpr milliseconds source_freshness{150};
constexpr milliseconds maximum_intent_ttl{200};
constexpr milliseconds radiation_pulse_period{1600};

[[nodiscard]] MonoTime add_milliseconds_saturated(MonoTime time, std::uint32_t amount) noexcept {
    using Duration = MonoTime::duration;
    using Rep = Duration::rep;
    const auto delta =
        duration_cast<Duration>(milliseconds{static_cast<milliseconds::rep>(amount)}).count();
    const Rep current = time.time_since_epoch().count();
    if constexpr (std::is_integral_v<Rep>) {
        if (delta > 0 && current > std::numeric_limits<Rep>::max() - delta) {
            return MonoTime::max();
        }
    }
    return MonoTime{Duration{static_cast<Rep>(current + delta)}};
}

[[nodiscard]] MonoTime add_milliseconds_saturated(MonoTime time, milliseconds amount) noexcept {
    return add_milliseconds_saturated(time, static_cast<std::uint32_t>(amount.count()));
}

[[nodiscard]] bool fresh_at(MonoTime source_at, MonoTime now) noexcept {
    return source_at <= now && now < add_milliseconds_saturated(source_at, source_freshness);
}

[[nodiscard]] bool valid_health(const LedHealthSample& health) noexcept {
    return std::isfinite(health.current) && health.current >= 0.0F &&
           std::isfinite(health.maximum) && health.maximum > 0.0F;
}

[[nodiscard]] float ratio_for(const LedHealthSample& health) noexcept {
    return std::clamp(health.current / health.maximum, 0.0F, 1.0F);
}

[[nodiscard]] std::uint32_t
mix_channel(std::uint32_t from, std::uint32_t target, float amount) noexcept {
    const float mixed =
        static_cast<float>(from) +
        ((static_cast<float>(target) - static_cast<float>(from)) * std::clamp(amount, 0.0F, 1.0F));
    return static_cast<std::uint32_t>(std::lround(mixed));
}

struct ColorRange {
    std::uint32_t from{};
    std::uint32_t target{};
};

[[nodiscard]] std::uint32_t mix_rgb(ColorRange colors, float amount) noexcept {
    const auto channel = [amount](std::uint32_t left, std::uint32_t right) noexcept {
        return mix_channel(left, right, amount);
    };
    const std::uint32_t red = channel((colors.from >> 16U) & 0xFFU, (colors.target >> 16U) & 0xFFU);
    const std::uint32_t green = channel((colors.from >> 8U) & 0xFFU, (colors.target >> 8U) & 0xFFU);
    const std::uint32_t blue = channel(colors.from & 0xFFU, colors.target & 0xFFU);
    return (red << 16U) | (green << 8U) | blue;
}

[[nodiscard]] std::uint32_t health_rgb(float ratio) noexcept {
    if (ratio <= 0.5F) {
        return mix_rgb({.from = 0xFF0000U, .target = 0xFFFF00U}, ratio * 2.0F);
    }
    return mix_rgb({.from = 0xFFFF00U, .target = 0x00FF00U}, (ratio - 0.5F) * 2.0F);
}

[[nodiscard]] std::uint32_t stealth_rgb(std::int32_t score, const LedSettings& settings) noexcept {
    const auto low = static_cast<std::int64_t>(settings.detection_hidden_score);
    const auto high = static_cast<std::int64_t>(settings.detection_alert_score);
    const auto bounded = std::clamp(static_cast<std::int64_t>(score), low, high);
    const double position = static_cast<double>(bounded - low) / static_cast<double>(high - low);
    return mix_rgb({.from = 0x0000FFU, .target = 0xFF00FFU}, static_cast<float>(position));
}

[[nodiscard]] std::uint32_t radiation_rgb(MonoTime now) noexcept {
    const auto period = duration_cast<MonoTime::duration>(radiation_pulse_period).count();
    const auto ticks = now.time_since_epoch().count();
    auto phase = ticks % period;
    if (phase < 0) {
        phase += period;
    }
    const auto half_period = period / 2;
    const bool rising = phase <= half_period;
    const auto distance = rising ? phase : period - phase;
    const float pulse = static_cast<float>(distance) / static_cast<float>(half_period);
    const auto red = mix_channel(0x99U, 0xFFU, pulse);
    return (red << 16U) | 0x00FF00U;
}

[[nodiscard]] std::uint32_t source_lifetime_ms(MonoTime source_at,
    std::optional<MonoTime> logical_deadline) noexcept {
    const auto freshness_end = add_milliseconds_saturated(source_at, source_freshness);
    MonoTime end =
        std::min(freshness_end, add_milliseconds_saturated(source_at, maximum_intent_ttl));
    if (logical_deadline) {
        end = std::min(end, *logical_deadline);
    }
    if (end <= source_at) {
        return 0;
    }
    const auto whole_ms = duration_cast<milliseconds>(end - source_at).count();
    if (whole_ms <= 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(whole_ms);
}

void keep_later(std::optional<MonoTime>& deadline, MonoTime candidate) noexcept {
    if (!deadline || candidate > *deadline) {
        deadline = candidate;
    }
}

[[nodiscard]] std::optional<MonoTime> earlier_deadline(std::optional<MonoTime> left,
    std::optional<MonoTime> right) noexcept {
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }
    return std::min(*left, *right);
}

} // namespace

bool valid_led_settings(const LedSettings& settings) noexcept {
    return settings.health_hold_ms >= 1U && settings.health_hold_ms <= 60000U &&
           settings.healing_transition_ms <= 1000U && std::isfinite(settings.critical_enter) &&
           std::isfinite(settings.critical_exit) && settings.critical_enter >= 0.0F &&
           settings.critical_exit <= 1.0F && settings.critical_enter < settings.critical_exit &&
           settings.detection_hidden_score < settings.detection_alert_score;
}

LedPolicy::LedPolicy(LedSettings settings) noexcept
    : settings_(settings),
      settings_valid_(valid_led_settings(settings)) {
}

void LedPolicy::reset() noexcept {
    mode_ = LedMode::none;
    paused_ = false;
    subject_id_.reset();
    last_observation_at_.reset();
    last_now_.reset();
    paused_at_.reset();
    clear_health_state();
    stealth_.reset();
    radiation_sources_.reset();
    in_combat_ = false;
    incoming_hit_at_.reset();
    last_incoming_hit_at_.reset();
    matched_incoming_hit_at_.reset();
    last_confirmed_damage_at_.reset();
}

void LedPolicy::pause(MonoTime now) noexcept {
    mode_ = LedMode::none;
    if (!settings_valid_ || (last_now_ && now < *last_now_)) {
        reset_at(now);
        return;
    }
    if (!paused_) {
        if (!last_observation_at_ || !subject_id_ || !fresh_at(*last_observation_at_, now)) {
            reset_at(now);
            return;
        }
        paused_ = true;
    }
    // A repeated intentional pause moves the resume sampling boundary without moving
    // any indication deadline. This also rejects a sample captured before the last menu.
    paused_at_ = now;
    last_now_ = now;
    clear_hit_correlation();
}

std::optional<LedIntent> LedPolicy::resume(const LedObservation& observation,
    MonoTime now) noexcept {
    mode_ = LedMode::none;
    if (!paused_) {
        return std::nullopt;
    }
    if (!settings_valid_ || (last_now_ && now < *last_now_) || !paused_at_ ||
        !last_observation_at_ || !subject_id_ || observation.subject_id == 0U ||
        observation.subject_id != *subject_id_ || !fresh_at(observation.sampled_at, now) ||
        observation.sampled_at <= *paused_at_ || observation.sampled_at <= *last_observation_at_) {
        reset_at(now);
        return std::nullopt;
    }

    // Resume samples establish current facts only. In particular, HP observed while
    // the menu was open cannot start, extend, or attribute a health/damage indication.
    last_now_ = now;
    subject_id_ = observation.subject_id;
    last_observation_at_ = observation.sampled_at;
    stealth_ = observation.stealth;
    radiation_sources_ = observation.radiation_sources;
    in_combat_ = observation.in_combat;
    clear_hit_correlation();
    if (!observation.health || !valid_health(*observation.health)) {
        clear_health_state();
    } else {
        health_current_ = observation.health->current;
        health_maximum_ = observation.health->maximum;
        health_ratio_ = ratio_for(*observation.health);
        ramp_from_ = health_ratio_;
        ramp_to_ = health_ratio_;
        ramp_started_ = observation.sampled_at;
        ramp_until_ = observation.sampled_at;
        ramp_active_ = false;
        comparable_loss_at_.reset();
        comparable_loss_maximum_ = 0.0F;
        update_critical_latch();
    }
    paused_ = false;
    paused_at_.reset();
    return arbitrate(now);
}

LedMode LedPolicy::mode() const noexcept {
    return mode_;
}

bool LedPolicy::begin_update(const LedObservation& observation, MonoTime now) noexcept {
    mode_ = LedMode::none;
    if (!settings_valid_) {
        reset();
        return false;
    }
    if (last_now_ && now < *last_now_) {
        reset();
        return false;
    }
    last_now_ = now;
    if (observation.subject_id == 0U) {
        reset_at(now);
        return false;
    }
    // A fresh source sample can bridge a delayed delivery tick. Its own source-to-source
    // gap is checked by accepts_observation; render latency alone is not a lost baseline.
    if (last_observation_at_ && !fresh_at(*last_observation_at_, now) &&
        !fresh_at(observation.sampled_at, now)) {
        reset_at(now);
    }
    return true;
}

bool LedPolicy::accepts_observation(const LedObservation& observation,
    MonoTime now,
    bool& reset_for_baseline) const noexcept {
    reset_for_baseline = false;
    if (!fresh_at(observation.sampled_at, now)) {
        return false;
    }
    if (!last_observation_at_) {
        reset_for_baseline = true;
        return true;
    }
    const bool changed_subject = subject_id_ && *subject_id_ != observation.subject_id;
    if (changed_subject) {
        if (observation.sampled_at < *last_observation_at_) {
            return false;
        }
        reset_for_baseline = true;
        return true;
    }
    if (observation.sampled_at <= *last_observation_at_) {
        return false;
    }
    reset_for_baseline = observation.sampled_at >=
                         add_milliseconds_saturated(*last_observation_at_, source_freshness);
    return true;
}

bool LedPolicy::event_carrier_is_current(const LedObservation& observation,
    MonoTime now) const noexcept {
    return last_observation_at_ && observation.sampled_at == *last_observation_at_ &&
           fresh_at(observation.sampled_at, now);
}

void LedPolicy::reset_at(MonoTime now) noexcept {
    reset();
    last_now_ = now;
}

void LedPolicy::clear_health_state() noexcept {
    health_current_.reset();
    health_maximum_ = 0.0F;
    health_ratio_ = 0.0F;
    ramp_from_ = 0.0F;
    ramp_to_ = 0.0F;
    ramp_started_ = MonoTime{};
    ramp_until_ = MonoTime{};
    ramp_active_ = false;
    health_deadline_.reset();
    damage_deadline_.reset();
    comparable_loss_at_.reset();
    comparable_loss_maximum_ = 0.0F;
    critical_latched_ = false;
    clear_hit_correlation();
}

void LedPolicy::clear_hit_correlation() noexcept {
    incoming_hit_at_.reset();
    matched_incoming_hit_at_.reset();
}

void LedPolicy::apply_observation(const LedObservation& observation) noexcept {
    subject_id_ = observation.subject_id;
    last_observation_at_ = observation.sampled_at;
    stealth_ = observation.stealth;
    radiation_sources_ = observation.radiation_sources;
    in_combat_ = observation.in_combat;
    if (!observation.health || !valid_health(*observation.health)) {
        clear_health_state();
        return;
    }
    apply_health_sample(*observation.health, observation.sampled_at);
}

void LedPolicy::apply_health_sample(const LedHealthSample& health, MonoTime sampled_at) noexcept {
    const auto prior_current = health_current_;
    const float prior_maximum = health_maximum_;
    const float new_ratio = ratio_for(health);
    if (!prior_current || health.maximum != prior_maximum) {
        health_current_ = health.current;
        health_maximum_ = health.maximum;
        health_ratio_ = new_ratio;
        ramp_active_ = false;
        ramp_from_ = new_ratio;
        ramp_to_ = new_ratio;
        comparable_loss_at_.reset();
        comparable_loss_maximum_ = 0.0F;
        clear_hit_correlation();
    } else if (health.current != *prior_current) {
        const float shown_at_sample = displayed_health_ratio(sampled_at);
        const bool health_decreased = health.current < *prior_current;
        health_current_ = health.current;
        health_maximum_ = health.maximum;
        health_ratio_ = new_ratio;
        if (health_decreased || settings_.healing_transition_ms == 0U) {
            ramp_active_ = false;
            ramp_from_ = new_ratio;
            ramp_to_ = new_ratio;
        } else {
            ramp_from_ = shown_at_sample;
            ramp_to_ = new_ratio;
            ramp_started_ = sampled_at;
            ramp_until_ = add_milliseconds_saturated(sampled_at, settings_.healing_transition_ms);
            ramp_active_ = ramp_until_ > ramp_started_;
            if (!ramp_active_) {
                ramp_from_ = new_ratio;
            }
        }
        keep_later(health_deadline_,
            add_milliseconds_saturated(sampled_at, settings_.health_hold_ms));
        if (health_decreased) {
            comparable_loss_at_ = sampled_at;
            comparable_loss_maximum_ = health.maximum;
        }
    }
    update_critical_latch();
}

void LedPolicy::update_critical_latch() noexcept {
    if (health_ratio_ <= settings_.critical_enter) {
        critical_latched_ = true;
    } else if (health_ratio_ >= settings_.critical_exit) {
        critical_latched_ = false;
    }
}

void LedPolicy::apply_event_evidence(const LedObservation& observation,
    MonoTime now,
    bool health_baseline_before_sample) noexcept {
    bool accepted_confirmed_damage = false;
    if (observation.confirmed_damage_at) {
        const MonoTime event_at = *observation.confirmed_damage_at;
        if (fresh_at(event_at, now) &&
            (!last_confirmed_damage_at_ || event_at > *last_confirmed_damage_at_)) {
            last_confirmed_damage_at_ = event_at;
            accepted_confirmed_damage = true;
        }
    }
    if (observation.incoming_hit_at) {
        const MonoTime event_at = *observation.incoming_hit_at;
        if (fresh_at(event_at, now) &&
            (!last_incoming_hit_at_ || event_at > *last_incoming_hit_at_)) {
            last_incoming_hit_at_ = event_at;
            if (health_baseline_before_sample && health_current_) {
                incoming_hit_at_ = event_at;
            }
        }
    }
    if (accepted_confirmed_damage && health_baseline_before_sample && health_current_) {
        const MonoTime deadline =
            add_milliseconds_saturated(*observation.confirmed_damage_at, settings_.health_hold_ms);
        keep_later(health_deadline_, deadline);
        keep_later(damage_deadline_, deadline);
    }
    match_incoming_hit(now);
}

void LedPolicy::match_incoming_hit(MonoTime now) noexcept {
    if (!incoming_hit_at_ || !comparable_loss_at_ || !health_current_ ||
        comparable_loss_maximum_ != health_maximum_ || !fresh_at(*incoming_hit_at_, now) ||
        !fresh_at(*comparable_loss_at_, now) ||
        (matched_incoming_hit_at_ && *matched_incoming_hit_at_ == *incoming_hit_at_) ||
        *incoming_hit_at_ > *comparable_loss_at_ ||
        *comparable_loss_at_ >= add_milliseconds_saturated(*incoming_hit_at_, source_freshness)) {
        return;
    }
    const MonoTime deadline =
        add_milliseconds_saturated(*incoming_hit_at_, settings_.health_hold_ms);
    keep_later(health_deadline_, deadline);
    keep_later(damage_deadline_, deadline);
    matched_incoming_hit_at_ = incoming_hit_at_;
}

float LedPolicy::displayed_health_ratio(MonoTime now) const noexcept {
    if (!ramp_active_) {
        return health_ratio_;
    }
    if (now <= ramp_started_) {
        return ramp_from_;
    }
    if (now >= ramp_until_) {
        return ramp_to_;
    }
    const auto total = ramp_until_ - ramp_started_;
    const auto elapsed = now - ramp_started_;
    const float fraction = static_cast<float>(elapsed.count()) / static_cast<float>(total.count());
    return ramp_from_ + ((ramp_to_ - ramp_from_) * fraction);
}

std::optional<LedIntent> LedPolicy::arbitrate(MonoTime now) noexcept {
    if (!last_observation_at_ || !fresh_at(*last_observation_at_, now)) {
        mode_ = LedMode::none;
        return std::nullopt;
    }

    const bool health_active = settings_.enabled && settings_.health_enabled && health_current_ &&
                               health_deadline_ && now < *health_deadline_;
    const bool recent_damage = health_active && damage_deadline_ && now < *damage_deadline_;
    const bool critical_health = health_active && critical_latched_;
    const bool radiation_active = settings_.enabled && settings_.radiation_enabled &&
                                  radiation_sources_ && *radiation_sources_ > 0U;
    const bool stealth_active =
        settings_.enabled && settings_.stealth_enabled && stealth_ && stealth_->sneaking;

    std::optional<MonoTime> logical_deadline;
    std::uint32_t color = 0;
    if (!settings_.enabled) {
        mode_ = LedMode::none;
        return std::nullopt;
    }
    if (critical_health) {
        mode_ = LedMode::critical_health;
        logical_deadline = health_deadline_;
        color = health_rgb(displayed_health_ratio(now));
    } else if (recent_damage) {
        mode_ = LedMode::recent_damage;
        logical_deadline = earlier_deadline(health_deadline_, damage_deadline_);
        color = health_rgb(displayed_health_ratio(now));
    } else if (radiation_active) {
        mode_ = LedMode::radiation;
        color = radiation_rgb(now);
    } else if (health_active && in_combat_) {
        mode_ = LedMode::combat_health;
        logical_deadline = health_deadline_;
        color = health_rgb(displayed_health_ratio(now));
    } else if (stealth_active) {
        mode_ = LedMode::stealth;
        color = stealth_rgb(stealth_->detection_score, settings_);
    } else if (health_active) {
        mode_ = LedMode::health;
        logical_deadline = health_deadline_;
        color = health_rgb(displayed_health_ratio(now));
    } else {
        mode_ = LedMode::none;
        return std::nullopt;
    }

    const std::uint32_t ttl = source_lifetime_ms(*last_observation_at_, logical_deadline);
    if (ttl == 0U) {
        mode_ = LedMode::none;
        return std::nullopt;
    }

    LedIntent intent{};
    intent.payload.lightbar_rgb = color;
    intent.payload.player_index = 0U;
    intent.payload.microphone_led = 0U;
    intent.payload.reserved = 0U;
    intent.source_at = *last_observation_at_;
    intent.ttl_ms = ttl;
    return intent;
}

std::optional<LedIntent> LedPolicy::update(const LedObservation& observation,
    MonoTime now) noexcept {
    if (paused_) {
        return resume(observation, now);
    }
    if (!begin_update(observation, now)) {
        return std::nullopt;
    }
    bool reset_for_baseline = false;
    const bool accept_sample = accepts_observation(observation, now, reset_for_baseline);
    if (reset_for_baseline) {
        reset_at(now);
    }
    const bool health_baseline_before_sample = health_current_.has_value();
    if (accept_sample) {
        apply_observation(observation);
    }
    if (event_carrier_is_current(observation, now)) {
        apply_event_evidence(observation, now, health_baseline_before_sample);
    }
    return arbitrate(now);
}

} // namespace inputnext::adapters
