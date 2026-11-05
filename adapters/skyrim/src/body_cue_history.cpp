#include "skyrim_adapter/detail/body_cue_history.hpp"

#include "skyrim_adapter/effect_settings.hpp"
#include "skyrim_adapter/model.hpp"

#include "effect_recipes.hpp"
#include "effect_tuning.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>

namespace skyrim_adapter::detail {
namespace {
using Milliseconds = std::chrono::milliseconds;

constexpr std::size_t left_held_position = static_cast<std::size_t>(BodyCueSlot::left_held);
constexpr std::size_t right_held_position = static_cast<std::size_t>(BodyCueSlot::right_held);
constexpr int held_priority = 10;

std::uint32_t remaining_ms(MonoTime now, MonoTime started_at, std::uint32_t duration_ms) noexcept {
    if (now < started_at) {
        return duration_ms;
    }
    const std::uint32_t elapsed = elapsed_ms(now, started_at);
    return elapsed >= duration_ms ? 0U : duration_ms - elapsed;
}

std::uint64_t cue_identity(const BodyCue& cue) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto append = [&hash](std::uint64_t value) noexcept {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    append(static_cast<std::uint8_t>(cue.kind));
    append(static_cast<std::uint8_t>(cue.world_kind));
    append(static_cast<std::uint8_t>(cue.dual_cast ? Hand::unknown : cue.hand));
    if (cue.dual_cast) {
        append(1U);
    }
    append(static_cast<std::uint8_t>(cue.source_kind));
    append(static_cast<std::uint8_t>(cue.weapon));
    append(static_cast<std::uint8_t>(cue.spell));
    append(static_cast<std::uint8_t>(cue.cast_mode));
    append(static_cast<std::uint8_t>(cue.cast_phase));
    append(static_cast<std::uint8_t>(cue.bow_phase));
    append(static_cast<std::uint8_t>(cue.beast));
    append(static_cast<std::uint8_t>(cue.gait));
    append(cue.content_stage);
    append(cue.source_id);
    append(cue.flags);
    append(cue.shield_block ? 1U : 0U);
    append(cue.mounted ? 1U : 0U);
    append(cue.sequence);
    const auto ticks =
        std::chrono::duration_cast<std::chrono::nanoseconds>(cue.started_at.time_since_epoch())
            .count();
    append(static_cast<std::uint64_t>(ticks));
    return hash == 0 ? 1 : hash;
}

bool body_cue_wins(const BodyCue& candidate, const BodyCue& current) noexcept {
    if (candidate.priority != current.priority) {
        return candidate.priority > current.priority;
    }
    if (candidate.started_at != current.started_at) {
        return candidate.started_at > current.started_at;
    }
    if (candidate.sequence != current.sequence) {
        return candidate.sequence > current.sequence;
    }
    return candidate.stable_id >= current.stable_id;
}

bool same_held_identity(const BodyCue& left, const BodyCue& right) noexcept {
    const auto identity = [](const BodyCue& cue) noexcept {
        return std::tie(cue.kind,
            cue.hand,
            cue.source_kind,
            cue.dual_cast,
            cue.weapon,
            cue.spell,
            cue.cast_mode,
            cue.cast_phase,
            cue.bow_phase,
            cue.source_id,
            cue.flags,
            cue.shield_block,
            cue.gait,
            cue.mounted);
    };

    return identity(left) == identity(right);
}

bool held_bow_tension_admitted(const Observation& observation,
    const HandObservation& hand,
    bool admission_open,
    bool interrupted) noexcept {
    const bool active_bow_phase = observation.bow == BowPhase::draw ||
                                  observation.bow == BowPhase::attached ||
                                  observation.bow == BowPhase::drawn;
    return admission_open && observation.weapon_drawn && !interrupted && hand.equipped_id != 0 &&
           hand.equipment == EquipmentKind::bow && hand.weapon == WeaponClass::bow &&
           active_bow_phase;
}

bool held_concentration_admitted(const Observation& observation,
    const HandObservation& hand,
    bool admission_open,
    bool interrupted,
    bool dual_concentration) noexcept {
    return admission_open && !interrupted &&
           (dual_concentration ||
               (observation.weapon_drawn && hand.caster_source_id != 0 &&
                   hand.spell != SpellKind::unknown && hand.cast_mode == CastMode::concentration &&
                   hand.cast_phase == CastPhase::casting));
}

BodyCue make_held_body_cue(const Observation& observation,
    const HandObservation& hand,
    std::size_t index,
    bool bow_tension,
    bool dual_concentration,
    float scale) noexcept {
    BodyCue cue{};
    cue.kind = bow_tension ? BodyCueKind::bow_tension : BodyCueKind::concentration;
    cue.hand = index == 0 ? Hand::left : Hand::right;
    cue.dual_cast = dual_concentration && !bow_tension;
    cue.source_kind = hand.equipment;
    cue.weapon = bow_tension ? hand.weapon : WeaponClass::unknown;
    cue.spell = bow_tension ? SpellKind::unknown : hand.spell;
    cue.cast_mode = bow_tension ? CastMode::none : hand.cast_mode;
    cue.cast_phase = bow_tension ? CastPhase::none : hand.cast_phase;
    cue.bow_phase = bow_tension ? observation.bow : BowPhase::none;
    cue.source_id = bow_tension ? hand.equipped_id : hand.caster_source_id;
    cue.started_at = observation.sampled_at;
    cue.duration_ms = hold_ttl_ms;
    cue.priority = held_priority;
    cue.scale = scale;
    return cue;
}

std::uint32_t held_horizon_ms(MonoTime now, MonoTime started_at) noexcept {
    if (now < started_at) {
        return hold_ttl_ms;
    }
    const auto elapsed = std::chrono::ceil<Milliseconds>(now - started_at).count();
    const auto maximum =
        static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max() - hold_ttl_ms);
    if (elapsed >= maximum) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(elapsed) + hold_ttl_ms;
}
} // namespace

bool body_cue_active(const BodyCue& cue, MonoTime now) noexcept {
    return (cue.kind != BodyCueKind::none || cue.world_kind != WorldCueKind::none) &&
           cue.duration_ms != 0 && cue.started_at <= now &&
           remaining_ms(now, cue.started_at, cue.duration_ms) != 0;
}

void BodyCueHistory::clear_history() noexcept {
    cues_ = {};
}

void BodyCueHistory::expire(MonoTime now) noexcept {
    for (auto& cue : cues_) {
        if (cue && !body_cue_active(*cue, now)) {
            cue.reset();
        }
    }
}

void BodyCueHistory::offer(BodyCueSlot slot, BodyCue cue) noexcept {
    const std::size_t position = slot_index(slot);
    if (position >= cues_.size() || cue.duration_ms == 0) {
        return;
    }
    if (cue.stable_id == 0) {
        cue.stable_id = cue_identity(cue);
    }
    if (const auto& existing = cues_[position]; !existing || body_cue_wins(cue, *existing)) {
        cues_[position] = cue;
    }
}

void BodyCueHistory::refresh_held(const Observation& observation,
    MonoTime now,
    bool interrupted,
    const EffectSettings& settings,
    float rumble_scale) noexcept {
    const bool dual_concentration = confirmed_dual_spell(observation, CastMode::concentration);
    for (std::size_t index = 0; index < observation.hands.size(); ++index) {
        refresh_held_hand(observation,
            now,
            interrupted,
            dual_concentration,
            index,
            settings,
            rumble_scale);
    }
}

void BodyCueHistory::refresh_held_hand(const Observation& observation,
    MonoTime now,
    bool interrupted,
    bool dual_concentration,
    std::size_t hand_index,
    const EffectSettings& settings,
    float rumble_scale) noexcept {
    if (hand_index >= observation.hands.size()) {
        return;
    }
    const std::size_t position = hand_index == 0 ? left_held_position : right_held_position;
    const HandObservation& hand = observation.hands[hand_index];
    const bool admission_open = held_admission_open(hand_index, observation.sampled_at);
    const bool bow_tension =
        held_bow_tension_admitted(observation, hand, admission_open, interrupted);
    const bool concentration = held_concentration_admitted(observation,
        hand,
        admission_open,
        interrupted,
        dual_concentration);
    if (!bow_tension && !concentration) {
        cues_[position].reset();
        return;
    }

    BodyCue cue = make_held_body_cue(observation,
        hand,
        hand_index,
        bow_tension,
        dual_concentration,
        rumble_scale);
    if (!tune_body_cue(cue, settings)) {
        cues_[position].reset();
        return;
    }

    const auto& previous = cues_[position];
    if (previous && body_cue_active(*previous, now) && same_held_identity(*previous, cue)) {
        cue.started_at = previous->started_at;
        cue.stable_id = previous->stable_id;
    } else {
        cue.stable_id = cue_identity(cue);
    }
    cue.duration_ms = held_horizon_ms(now, cue.started_at);
    offer(position == left_held_position ? BodyCueSlot::left_held : BodyCueSlot::right_held, cue);
}

bool BodyCueHistory::held_admission_open(std::size_t hand_index,
    MonoTime sampled_at) const noexcept {
    if (hand_index >= held_admission_after_.size()) {
        return false;
    }
    const auto& admission_after = held_admission_after_[hand_index];
    return !admission_after || sampled_at > *admission_after;
}

void BodyCueHistory::fence_held_admission(std::size_t hand_index,
    MonoTime admission_after) noexcept {
    if (hand_index >= held_admission_after_.size()) {
        return;
    }
    auto& hand_barrier = held_admission_after_[hand_index];
    if (!hand_barrier || admission_after > *hand_barrier) {
        hand_barrier = admission_after;
    }
    cues_[hand_index == 0 ? left_held_position : right_held_position].reset();
}

void BodyCueHistory::fence_all_held_admission(MonoTime admission_after) noexcept {
    fence_held_admission(0, admission_after);
    fence_held_admission(1, admission_after);
}

bool BodyCueHistory::has_admitted_bow_observation(const Observation& observation) const noexcept {
    for (std::size_t index = 0; index < observation.hands.size(); ++index) {
        const HandObservation& hand = observation.hands[index];
        const bool bow_equipped = hand.equipped_id != 0 && (hand.equipment == EquipmentKind::bow ||
                                                               hand.weapon == WeaponClass::bow);
        if (bow_equipped && held_admission_open(index, observation.sampled_at)) {
            return true;
        }
    }
    return false;
}

std::optional<BodyCue> BodyCueHistory::held_cue(std::size_t hand_index,
    MonoTime now) const noexcept {
    if (hand_index >= held_admission_after_.size()) {
        return std::nullopt;
    }
    const auto& cue = cues_[hand_index == 0 ? left_held_position : right_held_position];
    if (cue && body_cue_active(*cue, now)) {
        return cue;
    }
    return std::nullopt;
}

BodyPlan BodyCueHistory::materialize(MonoTime now) const noexcept {
    BodyPlan plan{};
    const auto copy_active = [this, now](BodyCueSlot slot) noexcept -> std::optional<BodyCue> {
        const auto& cue = cues_[slot_index(slot)];
        if (cue && body_cue_active(*cue, now)) {
            return cue;
        }
        return std::nullopt;
    };
    plan.held[0] = copy_active(BodyCueSlot::left_held);
    plan.held[1] = copy_active(BodyCueSlot::right_held);
    plan.hand_transient[0] = copy_active(BodyCueSlot::left_transient);
    plan.hand_transient[1] = copy_active(BodyCueSlot::right_transient);
    plan.body_transient = copy_active(BodyCueSlot::body_transient);
    return plan;
}

} // namespace skyrim_adapter::detail
