#include "skyrim_adapter/world_effects.hpp"

#include "skyrim_adapter/effect_settings.hpp"
#include "skyrim_adapter/model.hpp"

#include "effect_tuning.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

namespace skyrim_adapter {
namespace {
constexpr auto maximum_sample_age = std::chrono::milliseconds{150};
constexpr auto maximum_sample_gap = std::chrono::milliseconds{150};
constexpr double maximum_sample_displacement = 512.0;
constexpr double minimum_landing_descent = 64.0;

bool valid_location(const WorldLocation& location) noexcept {
    return location.cell_id != 0 && std::isfinite(location.x) && std::isfinite(location.y) &&
           std::isfinite(location.z);
}

bool valid_observation(const Observation& observation, MonoTime now) noexcept {
    const auto player_ready = [&observation] noexcept {
        return observation.player_id != 0 && observation.available && observation.alive &&
               observation.focused && observation.menus_clear;
    };
    const auto sample_fresh = [&observation, now] noexcept {
        return observation.sampled_at <= now && now - observation.sampled_at <= maximum_sample_age;
    };
    const auto stamina_valid = [&observation] noexcept {
        const auto& stamina_ratio = observation.world->stamina_ratio;
        return !stamina_ratio ||
               (std::isfinite(*stamina_ratio) && *stamina_ratio >= 0.0F && *stamina_ratio <= 1.0F);
    };
    return player_ready() && sample_fresh() && observation.world && stamina_valid();
}

bool location_continuous(const WorldLocation& previous, const WorldLocation& current) noexcept {
    if (previous.cell_id != current.cell_id || previous.worldspace_id != current.worldspace_id) {
        return false;
    }
    const double dx = static_cast<double>(current.x) - static_cast<double>(previous.x);
    const double dy = static_cast<double>(current.y) - static_cast<double>(previous.y);
    const double dz = static_cast<double>(current.z) - static_cast<double>(previous.z);
    return (dx * dx) + (dy * dy) + (dz * dz) <=
           maximum_sample_displacement * maximum_sample_displacement;
}

bool location_discontinuous(const WorldObservation& previous,
    const WorldObservation& current) noexcept {
    if (!previous.location || !current.location) {
        return false;
    }
    return !valid_location(*previous.location) || !valid_location(*current.location) ||
           !location_continuous(*previous.location, *current.location);
}

bool observation_discontinuous(const WorldObservation& previous,
    const WorldObservation& current,
    MonoTime previous_at,
    MonoTime current_at,
    FormId previous_player_id,
    FormId current_player_id) noexcept {
    return previous_player_id != current_player_id ||
           current_at - previous_at > maximum_sample_gap ||
           location_discontinuous(previous, current);
}

std::uint64_t effect_identity(WorldCueKind kind, FormId player_id, MonoTime started_at) noexcept {
    const auto ticks =
        std::chrono::duration_cast<std::chrono::nanoseconds>(started_at.time_since_epoch()).count();
    std::uint64_t hash = 1469598103934665603ULL;
    const auto append = [&hash](std::uint64_t value) noexcept {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    append(static_cast<std::uint8_t>(kind));
    append(player_id);
    append(static_cast<std::uint64_t>(ticks));
    return hash == 0 ? 1U : hash;
}

struct LandingDescent {
    double value{0.0};
};

WorldEffect make_effect(WorldCueKind kind,
    FormId player_id,
    MonoTime started_at,
    float body_scale,
    LandingDescent descent = {}) noexcept {
    WorldEffect effect{};
    effect.body.world_kind = kind;
    effect.body.started_at = started_at;
    effect.body.scale = std::isfinite(body_scale) ? std::clamp(body_scale, 0.0F, 1.0F) : 0.0F;
    effect.body.stable_id = effect_identity(kind, player_id, started_at);

    auto& rumble = effect.rumble;
    switch (kind) {
    case WorldCueKind::stamina_depleted:
        effect.body.duration_ms = rumble.duration_ms = 180;
        effect.body.priority = rumble.priority = 8;
        rumble.shape = WorldRumbleShape::double_pulse;
        rumble.low = 0.12F;
        rumble.high = 0.16F;
        rumble.second_low = 0.07F;
        rumble.second_high = 0.10F;
        rumble.peak_ms = 45;
        rumble.gap_ms = 45;
        rumble.second_duration_ms = 45;
        break;
    case WorldCueKind::stagger_entry:
        effect.body.duration_ms = rumble.duration_ms = 105;
        effect.body.priority = rumble.priority = 9;
        rumble.shape = WorldRumbleShape::fade;
        rumble.low = 0.12F;
        rumble.high = 0.18F;
        rumble.peak_ms = 20;
        break;
    case WorldCueKind::knockdown_entry:
        effect.body.duration_ms = rumble.duration_ms = 190;
        effect.body.priority = rumble.priority = 75;
        rumble.shape = WorldRumbleShape::fade;
        rumble.low = 0.24F;
        rumble.high = 0.30F;
        rumble.peak_ms = 45;
        break;
    case WorldCueKind::killmove_entry:
        effect.body.duration_ms = rumble.duration_ms = 220;
        effect.body.priority = rumble.priority = 9;
        rumble.shape = WorldRumbleShape::fade;
        rumble.low = 0.08F;
        rumble.high = 0.14F;
        rumble.peak_ms = 25;
        break;
    case WorldCueKind::landing: {
        const float amount = static_cast<float>(
            std::clamp((descent.value - minimum_landing_descent) /
                           (maximum_sample_displacement - minimum_landing_descent),
                0.0,
                1.0));
        const float observed_strength = 0.55F + (0.45F * amount);
        effect.body.duration_ms =
            static_cast<std::uint32_t>(85U + static_cast<std::uint32_t>(70.0F * amount));
        effect.body.priority = 9;
        effect.body.scale *= observed_strength;
        rumble.duration_ms = effect.body.duration_ms;
        rumble.priority = 9;
        rumble.shape = WorldRumbleShape::fade;
        rumble.low = 0.08F * observed_strength;
        rumble.high = 0.15F * observed_strength;
        rumble.peak_ms = 20;
        break;
    }
    case WorldCueKind::none:
        break;
    }
    return effect;
}

void offer_world_effect(std::optional<WorldEffect>& selected,
    WorldEffect candidate,
    const EffectSettings& settings,
    MonoTime now) noexcept {
    BodyCue resolved_body = candidate.body;
    if (!detail::tune_body_cue(resolved_body, settings) || resolved_body.started_at > now ||
        now - resolved_body.started_at >= std::chrono::milliseconds{resolved_body.duration_ms}) {
        return;
    }
    if (!selected || candidate.body.priority > selected->body.priority) {
        selected = candidate;
    }
}
} // namespace

void WorldEffects::reset() noexcept {
    previous_ = {};
    previous_at_ = MonoTime{};
    player_id_ = 0;
    fall_apex_z_ = 0.0F;
    has_previous_ = false;
    stamina_armed_ = false;
    fall_tracking_ = false;
}

void WorldEffects::remember(const WorldObservation& world,
    MonoTime sampled_at,
    FormId player_id) noexcept {
    previous_ = world;
    previous_at_ = sampled_at;
    player_id_ = player_id;
    has_previous_ = true;
}

void WorldEffects::start_fall_if_airborne(const WorldObservation& world) noexcept {
    fall_tracking_ = world.airborne && !world.swimming && !world.mounted && world.location &&
                     valid_location(*world.location);
    if (fall_tracking_) {
        fall_apex_z_ = world.location->z;
    }
}

void WorldEffects::establish_baseline(const Observation& observation,
    const WorldObservation& world) noexcept {
    remember(world, observation.sampled_at, observation.player_id);
    stamina_armed_ = world.stamina_ratio && *world.stamina_ratio >= 0.12F;
    start_fall_if_airborne(world);
}

void WorldEffects::offer_state_edges(const Observation& observation,
    const WorldObservation& current,
    float body_scale,
    MonoTime now,
    const EffectSettings& settings,
    std::optional<WorldEffect>& selected) const noexcept {
    if (!previous_.staggered && current.staggered) {
        offer_world_effect(selected,
            make_effect(WorldCueKind::stagger_entry,
                observation.player_id,
                observation.sampled_at,
                body_scale),
            settings,
            now);
    }
    if (!previous_.knocked_down && current.knocked_down) {
        offer_world_effect(selected,
            make_effect(WorldCueKind::knockdown_entry,
                observation.player_id,
                observation.sampled_at,
                body_scale),
            settings,
            now);
    }
    if (!previous_.killmove && current.killmove) {
        offer_world_effect(selected,
            make_effect(WorldCueKind::killmove_entry,
                observation.player_id,
                observation.sampled_at,
                body_scale),
            settings,
            now);
    }
}

void WorldEffects::update_stamina(const Observation& observation,
    const WorldObservation& current,
    float body_scale,
    const EffectSettings& settings,
    MonoTime now,
    std::optional<WorldEffect>& selected) noexcept {
    if (!current.stamina_ratio) {
        stamina_armed_ = false;
        return;
    }
    if (*current.stamina_ratio >= 0.12F) {
        stamina_armed_ = true;
        return;
    }
    if (*current.stamina_ratio <= 0.02F && stamina_armed_) {
        if (observation.in_combat || current.sprinting) {
            offer_world_effect(selected,
                make_effect(WorldCueKind::stamina_depleted,
                    observation.player_id,
                    observation.sampled_at,
                    body_scale),
                settings,
                now);
        }
        stamina_armed_ = false;
    }
}

void WorldEffects::update_landing(const Observation& observation,
    const WorldObservation& current,
    float body_scale,
    const EffectSettings& settings,
    MonoTime now,
    std::optional<WorldEffect>& selected) noexcept {
    const bool has_valid_landing_location = current.location && valid_location(*current.location) &&
                                            previous_.location &&
                                            valid_location(*previous_.location);
    if (!current.location || !valid_location(*current.location)) {
        fall_tracking_ = false;
    }
    if (current.swimming || current.mounted || !current.airborne) {
        if (previous_.airborne && !current.airborne && !current.swimming && !current.mounted &&
            fall_tracking_ && has_valid_landing_location) {
            const double descent =
                static_cast<double>(fall_apex_z_) - static_cast<double>(current.location->z);
            if (descent >= minimum_landing_descent) {
                offer_world_effect(selected,
                    make_effect(WorldCueKind::landing,
                        observation.player_id,
                        observation.sampled_at,
                        body_scale,
                        LandingDescent{.value = descent}),
                    settings,
                    now);
            }
        }
        // Landing can only use a continuously sampled airborne interval with a known
        // location. Swimming, mounting, ground contact, or missing position ends it.
        fall_tracking_ = false;
    } else if (current.location && valid_location(*current.location)) {
        if (!fall_tracking_) {
            fall_apex_z_ = current.location->z;
            fall_tracking_ = true;
        } else {
            fall_apex_z_ = std::max(fall_apex_z_, current.location->z);
        }
    }
}

std::optional<WorldEffect> WorldEffects::step(const Observation& observation,
    float body_scale,
    MonoTime now,
    const EffectSettings& settings) noexcept {
    if (!observation.world || !valid_observation(observation, now)) {
        reset();
        return std::nullopt;
    }

    const WorldObservation& current = *observation.world;
    if (!has_previous_) {
        establish_baseline(observation, current);
        return std::nullopt;
    }
    if (observation.sampled_at <= previous_at_) {
        if (observation.sampled_at < previous_at_) {
            reset();
        }
        return std::nullopt;
    }

    if (observation_discontinuous(previous_,
            current,
            previous_at_,
            observation.sampled_at,
            player_id_,
            observation.player_id)) {
        reset();
        establish_baseline(observation, current);
        return std::nullopt;
    }

    std::optional<WorldEffect> selected;
    offer_state_edges(observation, current, body_scale, now, settings, selected);
    update_stamina(observation, current, body_scale, settings, now, selected);
    update_landing(observation, current, body_scale, settings, now, selected);

    remember(current, observation.sampled_at, observation.player_id);
    return selected;
}
} // namespace skyrim_adapter
