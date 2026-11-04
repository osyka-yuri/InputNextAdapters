#include "support.hpp"

#include "effect_tuning.hpp"
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <input_next_effects.h>
#include <memory>
#include <optional>
#include <skyrim_adapter/content_observation.hpp>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/model.hpp>

namespace skyrim_adapter::effect_settings_tests {
namespace {
using namespace skyrim_adapter::detail;

constexpr WeaponClass melee_weapon_class(EffectId id) noexcept {
    switch (id) {
    case EffectId::unarmed_swing:
    case EffectId::unarmed_hit:
        return WeaponClass::unarmed;
    case EffectId::dagger_swing:
    case EffectId::dagger_hit:
        return WeaponClass::dagger;
    case EffectId::sword_swing:
    case EffectId::sword_hit:
        return WeaponClass::sword;
    case EffectId::axe_swing:
    case EffectId::axe_hit:
        return WeaponClass::axe;
    case EffectId::mace_swing:
    case EffectId::mace_hit:
        return WeaponClass::mace;
    case EffectId::two_handed_swing:
    case EffectId::two_handed_hit:
        return WeaponClass::two_handed;
    default:
        return WeaponClass::unknown;
    }
}

constexpr BowPhase bow_phase(EffectId id) noexcept {
    switch (id) {
    case EffectId::bow_tension_draw:
        return BowPhase::draw;
    case EffectId::bow_tension_attached:
        return BowPhase::attached;
    case EffectId::bow_tension_drawn:
        return BowPhase::drawn;
    default:
        return BowPhase::none;
    }
}

constexpr SpellKind spell_kind(EffectId id) noexcept {
    switch (id) {
    case EffectId::fire_release:
    case EffectId::fire_held:
        return SpellKind::fire;
    case EffectId::frost_release:
    case EffectId::frost_held:
        return SpellKind::frost;
    case EffectId::shock_release:
    case EffectId::shock_held:
        return SpellKind::shock;
    case EffectId::healing_release:
    case EffectId::healing_held:
        return SpellKind::healing;
    case EffectId::ward_release:
    case EffectId::ward_held:
        return SpellKind::ward;
    case EffectId::generic_release:
    case EffectId::generic_held:
        return SpellKind::generic;
    default:
        return SpellKind::unknown;
    }
}

constexpr bool is_mounted_footstep(EffectId id) noexcept {
    return id == EffectId::mounted_walk || id == EffectId::mounted_run ||
           id == EffectId::mounted_sprint;
}

constexpr MovementGait movement_gait(EffectId id) noexcept {
    switch (id) {
    case EffectId::footstep_walk:
    case EffectId::mounted_walk:
        return MovementGait::walking;
    case EffectId::footstep_run:
    case EffectId::mounted_run:
        return MovementGait::running;
    case EffectId::footstep_sprint:
    case EffectId::mounted_sprint:
        return MovementGait::sprinting;
    case EffectId::footstep_sneak:
        return MovementGait::sneaking;
    default:
        return MovementGait::unknown;
    }
}

BodyCue cue_for(EffectId id) {
    BodyCue cue{};
    cue.started_at = at(0);
    cue.duration_ms = 100;
    switch (id) {
    case EffectId::unarmed_swing:
    case EffectId::dagger_swing:
    case EffectId::sword_swing:
    case EffectId::axe_swing:
    case EffectId::mace_swing:
    case EffectId::two_handed_swing:
        cue.kind = BodyCueKind::melee_swing;
        cue.weapon = melee_weapon_class(id);
        break;
    case EffectId::unarmed_hit:
    case EffectId::dagger_hit:
    case EffectId::sword_hit:
    case EffectId::axe_hit:
    case EffectId::mace_hit:
    case EffectId::two_handed_hit:
        cue.kind = BodyCueKind::melee_hit;
        cue.weapon = melee_weapon_class(id);
        break;
    case EffectId::incoming_hit:
    case EffectId::shield_hit:
        cue.kind = BodyCueKind::incoming_hit;
        cue.shield_block = id == EffectId::shield_hit;
        break;
    case EffectId::bow_draw:
        cue.kind = BodyCueKind::bow_draw;
        break;
    case EffectId::bow_full_draw:
        cue.kind = BodyCueKind::bow_full_draw;
        break;
    case EffectId::bow_release:
        cue.kind = BodyCueKind::bow_release;
        break;
    case EffectId::bow_tension_draw:
    case EffectId::bow_tension_attached:
    case EffectId::bow_tension_drawn:
        cue.kind = BodyCueKind::bow_tension;
        cue.bow_phase = bow_phase(id);
        break;
    case EffectId::crossbow_shot:
        cue.kind = BodyCueKind::crossbow_shot;
        break;
    case EffectId::fire_release:
    case EffectId::frost_release:
    case EffectId::shock_release:
    case EffectId::healing_release:
    case EffectId::ward_release:
    case EffectId::generic_release:
        cue.kind = BodyCueKind::spell_release;
        cue.spell = spell_kind(id);
        break;
    case EffectId::fire_held:
    case EffectId::frost_held:
    case EffectId::shock_held:
    case EffectId::healing_held:
    case EffectId::ward_held:
    case EffectId::generic_held:
        cue.kind = BodyCueKind::concentration;
        cue.spell = spell_kind(id);
        break;
    case EffectId::voice:
        cue.kind = BodyCueKind::voice_fire;
        break;
    case EffectId::critical_health:
        cue.kind = BodyCueKind::critical_health;
        break;
    case EffectId::stamina_depleted:
        cue.world_kind = WorldCueKind::stamina_depleted;
        break;
    case EffectId::stagger:
        cue.world_kind = WorldCueKind::stagger_entry;
        break;
    case EffectId::knockdown:
        cue.world_kind = WorldCueKind::knockdown_entry;
        break;
    case EffectId::killmove:
        cue.world_kind = WorldCueKind::killmove_entry;
        break;
    case EffectId::landing:
        cue.world_kind = WorldCueKind::landing;
        break;
    case EffectId::survival_cold:
        cue.kind = BodyCueKind::survival_cold;
        break;
    case EffectId::survival_hunger:
        cue.kind = BodyCueKind::survival_hunger;
        break;
    case EffectId::survival_exhaustion:
        cue.kind = BodyCueKind::survival_exhaustion;
        break;
    case EffectId::werewolf:
    case EffectId::vampire_lord:
        cue.kind = BodyCueKind::transformation;
        cue.beast = id == EffectId::werewolf ? BeastForm::werewolf : BeastForm::vampire_lord;
        break;
    case EffectId::footstep_walk:
    case EffectId::footstep_run:
    case EffectId::footstep_sprint:
    case EffectId::footstep_sneak:
    case EffectId::mounted_walk:
    case EffectId::mounted_run:
    case EffectId::mounted_sprint:
        cue.kind = BodyCueKind::footstep;
        cue.mounted = is_mounted_footstep(id);
        cue.gait = movement_gait(id);
        break;
    case EffectId::count:
        break;
    }
    return cue;
}
} // namespace

TEST_CASE("skyrim.effect_settings.all_body_descriptors_have_a_consumer",
    "[component][skyrim][effect_settings]") {
    INFO("the full typed body catalogue has 52 descriptors");
    CHECK(effect_descriptors.size() == 52);
    const auto default_settings = std::make_unique<EffectSettings>();
    const EffectSettings& defaults = *default_settings;
    for (const EffectId id : {
             EffectId::footstep_walk,
             EffectId::footstep_run,
             EffectId::footstep_sneak,
             EffectId::mounted_walk,
             EffectId::mounted_run,
         }) {
        INFO("walking, running, and sneaking profiles default to disabled");
        CHECK(!defaults.body[static_cast<std::size_t>(id)].enabled);
    }
    INFO("sprint, mounted sprint, landing, and other body profiles remain enabled by default");
    CHECK(defaults.body[static_cast<std::size_t>(EffectId::footstep_sprint)].enabled);
    CHECK(defaults.body[static_cast<std::size_t>(EffectId::mounted_sprint)].enabled);
    CHECK(defaults.body[static_cast<std::size_t>(EffectId::landing)].enabled);
    CHECK(defaults.body[static_cast<std::size_t>(EffectId::sword_swing)].enabled);

    for (const EffectDescriptor& descriptor : effect_descriptors) {
        CAPTURE(descriptor.id, descriptor.section);
        BodyCue cue = cue_for(descriptor.id);
        INFO("each body descriptor resolves from a production BodyCue family");
        CHECK(effect_id_for_body_cue(cue) == descriptor.id);

        const auto settings_storage = std::make_unique<EffectSettings>();
        auto& settings = *settings_storage;
        EffectAdjustment& adjustment = settings.body[static_cast<std::size_t>(descriptor.id)];
        adjustment.enabled = true;
        adjustment.hd_scale = 1.5F;
        adjustment.classic_scale = 0.5F;
        adjustment.duration_scale = 1.5F;
        adjustment.frequency_hz = 300.0F;

        INFO("each mapped body descriptor admits a valid cue");
        CHECK(tune_body_cue(cue, settings));

        INFO("each body profile resolves supported amplitude fields and frequency");
        CHECK(cue.hd_scale == 1.5F);
        CHECK(cue.classic_scale == (descriptor.classic ? 0.5F : 0.0F));
        CHECK(cue.frequency_hz == 300.0F);

        INFO("only transient body descriptors stretch their cue duration");
        CHECK(cue.duration_ms == (descriptor.transient ? 150U : 100U));
        CHECK(cue.duration_scale == (descriptor.transient ? 1.5F : 1.0F));
    }
}

TEST_CASE("skyrim.effect_settings.unsupported_classic_gain_cannot_admit_a_profile",
    "[component][skyrim][effect_settings]") {
    for (const EffectDescriptor& descriptor : effect_descriptors) {
        if (descriptor.classic) {
            continue;
        }

        CAPTURE(descriptor.id, descriptor.section);
        const auto settings_storage = std::make_unique<EffectSettings>();
        auto& settings = *settings_storage;
        EffectAdjustment& adjustment = settings.body[static_cast<std::size_t>(descriptor.id)];
        adjustment.enabled = true;
        adjustment.hd_scale = 0.0F;
        adjustment.classic_scale = 2.0F;
        BodyCue cue = cue_for(descriptor.id);

        INFO("unsupported classic gain cannot make a zero-HD profile active");
        CHECK(!body_profile_admitted(settings, descriptor.id));
        CHECK(!tune_body_cue(cue, settings));
    }
}

TEST_CASE("skyrim.effect_settings.all_trigger_descriptors_have_a_consumer",
    "[component][skyrim][effect_settings]") {
    INFO("the typed trigger catalogue has 11 descriptors");
    CHECK(trigger_descriptors.size() == 11);

    INFO("all six spell-family triggers resolve to their actual recipe family");
    CHECK(trigger_id_for_spell(SpellKind::fire) == TriggerId::fire);
    CHECK(trigger_id_for_spell(SpellKind::frost) == TriggerId::frost);
    CHECK(trigger_id_for_spell(SpellKind::shock) == TriggerId::shock);
    CHECK(trigger_id_for_spell(SpellKind::healing) == TriggerId::healing);
    CHECK(trigger_id_for_spell(SpellKind::ward) == TriggerId::ward);
    CHECK(trigger_id_for_spell(SpellKind::generic) == TriggerId::generic);

    HandObservation hand{};
    hand.caster_source_id = 7;
    hand.spell = SpellKind::fire;
    hand.cast_mode = CastMode::fire_and_forget;
    hand.cast_phase = CastPhase::charging;
    INFO("charging spell recipes use the independent magic-charge profile");
    CHECK(trigger_id_for_held_magic(hand) == TriggerId::magic_charge);

    hand.cast_mode = CastMode::concentration;
    hand.cast_phase = CastPhase::casting;
    INFO("sustained spell recipes use their actual element profile");
    CHECK(trigger_id_for_held_magic(hand) == TriggerId::fire);

    Observation current = observation(at(10));
    current.weapon_drawn = true;
    current.blocking = true;
    current.hands[1] = {
        .equipped_id = 9,
        .caster_source_id = 0,
        .equipment = EquipmentKind::weapon,
        .weapon = WeaponClass::sword,
    };
    INFO("weapon blocking reaches its own trigger profile");
    CHECK(trigger_id_for_blocking(current) == TriggerId::weapon_block);

    current.hands[0] = {
        .equipped_id = 10,
        .caster_source_id = 0,
        .equipment = EquipmentKind::shield,
        .weapon = WeaponClass::none,
    };
    current.shield_blocking = true;
    INFO("shield blocking reaches its own trigger profile");
    CHECK(trigger_id_for_blocking(current) == TriggerId::shield_block);

    Action impact = action(EventKind::hit, Hand::unknown, at(10), 1);
    impact.incoming = true;
    impact.event.flags = static_cast<std::uint32_t>(HitFlag::blocked);
    INFO("confirmed shield impact reaches its separate trigger profile");
    CHECK(trigger_id_for_action(impact, current) == TriggerId::shield_hit);

    Action cast_event = action(EventKind::spell_cast, Hand::left, at(10), 2);
    cast_event.spell = SpellKind::fire;
    Action const voice_cast_event = action(EventKind::voice_cast, Hand::voice, at(10), 3);
    INFO("cast notification events do not invent action trigger profiles");
    CHECK(trigger_id_for_action(cast_event, current) == TriggerId::count);
    CHECK(trigger_id_for_action(voice_cast_event, current) == TriggerId::count);

    for (const TriggerDescriptor& descriptor : trigger_descriptors) {
        CAPTURE(descriptor.id, descriptor.section);
        const auto settings_storage = std::make_unique<EffectSettings>();
        auto& settings = *settings_storage;
        TriggerAdjustment& adjustment = settings.triggers[static_cast<std::size_t>(descriptor.id)];
        adjustment.strength_scale = 1.5F;
        adjustment.start = 0.33F;
        adjustment.frequency_code =
            descriptor.vibration ? std::optional<unsigned>{123U} : std::nullopt;
        input_next_effects_trigger_v1 payload{
            .mode = descriptor.vibration ? INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION
                                         : INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK,
            .start = 0.12F,
            .end = 1.0F,
            .strength = 0.4F,
            .frequency = 38.0F,
        };
        INFO("each trigger descriptor consumes a typed trigger adjustment");
        CHECK(tune_trigger_payload(payload, descriptor.id, settings));

        INFO("trigger start and strength tuning apply before the global trigger gain");
        CHECK(payload.start == 0.33F);
        CHECK(payload.strength == 0.6F);

        INFO("a trigger start override retains the authored end when it is already later");
        CHECK(payload.end == 1.0F);

        INFO("only vibration profiles replace the device frequency code");
        CHECK(payload.frequency == (descriptor.vibration ? 123.0F : 38.0F));
    }
}

} // namespace skyrim_adapter::effect_settings_tests
