#pragma once

#include "content_observation.hpp"
#include "effect_settings.hpp"

#include <inputnext_adapters/output_frame.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>

namespace skyrim_adapter {
using MonoTime = std::chrono::steady_clock::time_point;
using Epoch = std::uint64_t;
using FormId = std::uint32_t;

inline constexpr std::size_t inbox_capacity = 64;
inline constexpr auto observation_max_age = std::chrono::milliseconds{150};
inline constexpr auto event_max_age = std::chrono::milliseconds{150};
inline constexpr std::uint32_t hold_ttl_ms = 200;

enum class Hand : std::uint8_t { left, right, voice, unknown };

enum class EquipmentKind : std::uint8_t {
    none,
    weapon,
    bow,
    crossbow,
    shield,
    spell,
    staff,
    scroll,
    unknown
};

enum class WeaponClass : std::uint8_t {
    none,
    unarmed,
    dagger,
    sword,
    axe,
    mace,
    two_handed,
    bow,
    crossbow,
    unknown
};

enum class SpellKind : std::uint8_t { unknown, generic, fire, frost, shock, healing, ward };

enum class CastMode : std::uint8_t { none, fire_and_forget, concentration, unknown };

enum class CastPhase : std::uint8_t { none, ready, charging, casting, unknown };

enum class ShoutLevel : std::uint8_t { unknown, one_word, two_words, three_words };

enum class BowPhase : std::uint8_t {
    none,
    draw,
    attached,
    drawn,
    releasing,
    released,
    next_attack,
    follow_through,
    unknown,
};

enum class MovementGait : std::uint8_t { unknown, walking, running, sprinting, sneaking };

enum class BodyCueKind : std::uint8_t {
    none,
    melee_swing,
    melee_hit,
    incoming_hit,
    bow_draw,
    bow_full_draw,
    bow_release,
    bow_tension,
    crossbow_shot,
    spell_release,
    concentration,
    voice_fire,
    critical_health,
    survival_cold,
    survival_hunger,
    survival_exhaustion,
    transformation,
    footstep,
};

enum class WorldCueKind : std::uint8_t {
    none,
    stamina_depleted,
    stagger_entry,
    knockdown_entry,
    killmove_entry,
    landing,
};

enum class EventKind : std::uint8_t {
    weapon_swing,
    spell_cast,
    spell_fire,
    voice_cast,
    voice_fire,
    bow_draw,
    bow_release,
    begin_draw,
    end_draw,
    begin_sheathe,
    end_sheathe,
    hit,
    equip,
    footstep,
    player_bow_shot,
};

enum class Control : std::uint8_t {
    data_loaded,
    pre_load,
    load_succeeded,
    load_failed,
    new_game,
    menu_suppressed,
    menu_resumed,
    focus_lost,
    focus_gained,
    fault,
};

enum class HitFlag : std::uint32_t {
    power_attack = 1,
    sneak_attack = 2,
    bash_attack = 4,
    blocked = 8
};

struct NativeTicket {
    Epoch epoch{};
};

struct RawEvent {
    EventKind kind{};
    ShoutLevel shout_level{ShoutLevel::unknown};
    Hand hand{Hand::unknown};
    FormId actor_id{};
    FormId target_id{};
    FormId source_id{};
    FormId projectile_id{};
    std::uint32_t flags{};
    std::uint64_t sequence{};
    MonoTime observed_at;
};

struct NativeCapture {
    NativeTicket ticket{};
    std::uint64_t burst_revision{};
    MonoTime read_started_at;
    std::array<RawEvent, inbox_capacity> events{};
    std::size_t count{};
};

struct HandObservation {
    FormId equipped_id{};
    FormId caster_source_id{};
    EquipmentKind equipment{EquipmentKind::unknown};
    WeaponClass weapon{WeaponClass::unknown};
    SpellKind spell{SpellKind::unknown};
    CastMode cast_mode{CastMode::unknown};
    CastPhase cast_phase{CastPhase::unknown};
    bool two_handed{};
};

struct WorldLocation {
    FormId cell_id{};
    FormId worldspace_id{};
    float x{};
    float y{};
    float z{};
};

struct WorldObservation {
    bool moving{};
    bool sprinting{};
    bool swimming{};
    bool staggered{};
    bool knocked_down{};
    bool killmove{};
    bool mounted{};
    bool airborne{};
    MovementGait gait{MovementGait::unknown};
    FormId mount_id{};
    MovementGait mount_gait{MovementGait::unknown};
    bool mount_swimming{};
    bool mount_airborne{};
    std::optional<float> stamina_ratio;
    std::optional<WorldLocation> location;
};

struct Observation {
    MonoTime sampled_at;
    inputnext::adapters::LedObservation led{};
    FormId player_id{};
    bool available{};
    bool alive{};
    bool focused{};
    bool menus_clear{};
    // Fake observations default neutral; native capture resolves a missing preference to false.
    bool body_vibration_enabled{true};
    bool weapon_drawn{};
    bool dual_cast{};
    bool in_combat{};
    bool blocking{};
    bool shield_blocking{};
    std::optional<WorldObservation> world;
    ContentObservation content{};
    std::optional<float> health_ratio;
    std::array<HandObservation, 2> hands{};
    BowPhase bow{BowPhase::unknown};
    bool ammo_present{};
    bool ranged_ready{};
};

// Confirms that the native dual-cast flag and both typed hand observations describe one
// equipped spell. Concentrations must already be casting; fire-and-forget releases may still
// be charging or casting when their matching native event is admitted.
[[nodiscard]] inline bool confirmed_dual_spell(const Observation& observation,
    CastMode mode,
    FormId expected_source_id = 0,
    SpellKind expected_spell = SpellKind::unknown) noexcept {
    if (!observation.dual_cast || !observation.weapon_drawn ||
        (mode != CastMode::concentration && mode != CastMode::fire_and_forget)) {
        return false;
    }

    const HandObservation& left = observation.hands[0];
    const HandObservation& right = observation.hands[1];
    const auto valid_spell_hand = [mode](const HandObservation& hand) noexcept {
        return hand.equipment == EquipmentKind::spell && hand.equipped_id != 0 &&
               hand.caster_source_id == hand.equipped_id && hand.spell != SpellKind::unknown &&
               hand.cast_mode == mode;
    };
    if (!valid_spell_hand(left) || !valid_spell_hand(right)) {
        return false;
    }
    if (left.equipped_id != right.equipped_id || left.spell != right.spell) {
        return false;
    }
    if ((expected_source_id != 0 && left.caster_source_id != expected_source_id) ||
        (expected_spell != SpellKind::unknown && left.spell != expected_spell)) {
        return false;
    }

    const auto active_phase = [mode](CastPhase phase) noexcept {
        if (mode == CastMode::concentration) {
            return phase == CastPhase::casting;
        }
        return phase == CastPhase::charging || phase == CastPhase::casting;
    };
    return active_phase(left.cast_phase) && active_phase(right.cast_phase);
}

struct Action {
    RawEvent event{};
    EquipmentKind source_kind{EquipmentKind::unknown};
    WeaponClass weapon{WeaponClass::unknown};
    SpellKind spell{SpellKind::unknown};
    CastMode cast_mode{CastMode::unknown};
    bool source_known{};
    bool incoming{};
    bool outgoing{};
    FormId attributed_player_id{};
    FormId attributed_mount_id{};
    MovementGait gait{MovementGait::unknown};
    bool mount_attributed{};
};

using inputnext::adapters::RumbleIntent;
using inputnext::adapters::TriggerIntent;

struct BodyCue {
    BodyCueKind kind{BodyCueKind::none};
    WorldCueKind world_kind{WorldCueKind::none};
    ShoutLevel shout_level{ShoutLevel::unknown};
    Hand hand{Hand::unknown};
    bool dual_cast{};
    EquipmentKind source_kind{EquipmentKind::unknown};
    WeaponClass weapon{WeaponClass::unknown};
    SpellKind spell{SpellKind::unknown};
    CastMode cast_mode{CastMode::none};
    CastPhase cast_phase{CastPhase::none};
    BowPhase bow_phase{BowPhase::none};
    BeastForm beast{BeastForm::none};
    unsigned content_stage{};
    FormId source_id{};
    std::uint32_t flags{};
    bool shield_block{};
    MovementGait gait{MovementGait::unknown};
    bool mounted{};
    MonoTime started_at;
    std::uint32_t duration_ms{};
    int priority{};
    std::uint64_t sequence{};
    std::uint64_t stable_id{};
    float scale{};
    float hd_scale{1.0F};
    float classic_scale{1.0F};
    float duration_scale{1.0F};
    float frequency_hz{};
    std::optional<inputnext::adapters::AudioHapticProfile> audio_haptic;
};

struct BodyPlan {
    std::array<std::optional<BodyCue>, 2> held{};
    std::array<std::optional<BodyCue>, 2> hand_transient{};
    std::optional<BodyCue> body_transient;
};

[[nodiscard]] inline bool same_body_cue_identity(const BodyCue& left,
    const BodyCue& right) noexcept {
    const auto semantics = [](const BodyCue& cue) noexcept {
        return std::tie(cue.kind,
            cue.world_kind,
            cue.shout_level,
            cue.hand,
            cue.dual_cast,
            cue.source_kind,
            cue.weapon,
            cue.spell,
            cue.cast_mode,
            cue.cast_phase,
            cue.bow_phase,
            cue.beast,
            cue.content_stage,
            cue.source_id,
            cue.flags,
            cue.shield_block,
            cue.gait,
            cue.mounted,
            cue.started_at,
            cue.priority,
            cue.sequence,
            cue.stable_id,
            cue.scale,
            cue.hd_scale,
            cue.classic_scale,
            cue.duration_scale,
            cue.frequency_hz,
            cue.audio_haptic);
    };

    if (semantics(left) != semantics(right)) {
        return false;
    }

    // Held cues renew their finite horizon while retaining one cue identity and start.
    const bool duration_is_renewable =
        left.kind == BodyCueKind::concentration || left.kind == BodyCueKind::bow_tension;
    return duration_is_renewable || left.duration_ms == right.duration_ms;
}

[[nodiscard]] inline bool same_body_plan_identity(const BodyPlan& left,
    const BodyPlan& right) noexcept {
    const auto same_optional = [](const std::optional<BodyCue>& lhs,
                                   const std::optional<BodyCue>& rhs) noexcept {
        return lhs.has_value() == rhs.has_value() && (!lhs || same_body_cue_identity(*lhs, *rhs));
    };
    return same_optional(left.held[0], right.held[0]) &&
           same_optional(left.held[1], right.held[1]) &&
           same_optional(left.hand_transient[0], right.hand_transient[0]) &&
           same_optional(left.hand_transient[1], right.hand_transient[1]) &&
           same_optional(left.body_transient, right.body_transient);
}

struct EffectFrame {
    std::optional<TriggerIntent> left;
    std::optional<TriggerIntent> right;
    std::optional<RumbleIntent> rumble;
    BodyPlan body{};
    std::optional<inputnext::adapters::LedIntent> leds;
};

struct PolicyConfig {
    float trigger_scale{1.0F};
    float rumble_scale{1.0F};
    EffectSettings effects{};
};

struct CoordinatorConfig {
    std::uint32_t slot{};
};
} // namespace skyrim_adapter
