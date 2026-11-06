#include "native_observation/native_observation_test_cases.hpp"
#include "native_observation/native_observation_test_support.hpp"

#include "native/observation.hpp"
#include "skyrim_adapter/model.hpp"
#include <skyrim_adapter/detail/ingress.hpp>

#include <chrono>

namespace skyrim_adapter::native_observation_tests {

namespace {

Observation ingress_observation(MonoTime sampled_at) {
    Observation observation{};
    observation.sampled_at = sampled_at;
    observation.player_id = 77;
    observation.available = observation.alive = observation.focused = observation.menus_clear =
        true;
    WorldObservation world{};
    world.gait = MovementGait::walking;
    observation.world = world;
    return observation;
}

} // namespace

void resolved_incoming_action_preserves_event_envelope() {
    const auto timestamp = MonoTime{std::chrono::milliseconds{42}};
    const RawEvent event{
        .kind = EventKind::hit,
        .hand = Hand::unknown,
        .actor_id = 55,
        .target_id = 77,
        .source_id = 91,
        .projectile_id = 12,
        .flags = 3,
        .sequence = 8,
        .observed_at = timestamp,
    };
    Observation observation{};
    observation.sampled_at = timestamp;
    observation.player_id = 77;
    observation.available = observation.alive = observation.focused = observation.menus_clear =
        true;
    const auto action =
        native::resolve_action(event, observation, timestamp + std::chrono::milliseconds{1});
    require(action.has_value(), "incoming player hit was discarded");
    if (!action) {
        require(false, "incoming player hit action is available after its presence check");
        return;
    }
    const auto& resolved_action = action.value();
    require(resolved_action.incoming && !resolved_action.outgoing,
        "incoming hit was mislabeled as outgoing");
    require(resolved_action.event.actor_id == event.actor_id &&
                resolved_action.event.target_id == event.target_id &&
                resolved_action.event.source_id == event.source_id &&
                resolved_action.event.projectile_id == event.projectile_id &&
                resolved_action.event.sequence == event.sequence &&
                resolved_action.event.observed_at == timestamp,
        "native conversion changed stable event values or the original timestamp");

    observation.player_id = 88;
    require(!native::resolve_action(event, observation, timestamp + std::chrono::milliseconds{1}),
        "hit unrelated to the current player was retained");
}

void footsteps_on_foot_player_attribution() {
    using skyrim_adapter::detail::action_attribution_matches;
    using skyrim_adapter::detail::current_footstep_attribution;
    using skyrim_adapter::detail::footstep_enqueue_attributable;

    const MonoTime now{std::chrono::milliseconds{200}};
    const Observation observation = ingress_observation(now - std::chrono::milliseconds{10});
    require(footstep_enqueue_attributable(observation, 77, now),
        "fresh on-foot player step was rejected");
    require(!footstep_enqueue_attributable(observation, 123, now),
        "unrelated NPC step was admitted");
    require(current_footstep_attribution(observation, 77, now).has_value(),
        "current on-foot player step did not resolve");

    const RawEvent event{
        .kind = EventKind::footstep,
        .hand = Hand::unknown,
        .actor_id = 77,
        .target_id = 0,
        .source_id = 0,
        .projectile_id = 0,
        .flags = 0,
        .sequence = 9,
        .observed_at = now,
    };
    const auto player_action = native::resolve_action(event, observation, now);
    require(player_action && player_action->gait == MovementGait::walking &&
                !player_action->mount_attributed && player_action->attributed_player_id == 77,
        "player step conversion did not preserve fresh locomotion attribution");
    require(player_action && action_attribution_matches(*player_action, observation, now),
        "fresh player step failed its publication guard");
}

void footsteps_mount_attribution_and_identity_barrier() {
    using skyrim_adapter::detail::action_attribution_matches;
    using skyrim_adapter::detail::current_footstep_attribution;
    using skyrim_adapter::detail::footstep_enqueue_attributable;

    const MonoTime now{std::chrono::milliseconds{200}};
    Observation observation = ingress_observation(now - std::chrono::milliseconds{10});

    if (!observation.world) {
        require(false, "ingress observation includes its world sample");
        return;
    }
    WorldObservation& world = *observation.world;
    world.mounted = true;
    world.mount_id = 88;
    world.mount_gait = MovementGait::running;
    require(footstep_enqueue_attributable(observation, 88, now),
        "fresh current-mount step was rejected");
    require(!footstep_enqueue_attributable(observation, 77, now),
        "rider footstep was admitted while mounted");
    require(!current_footstep_attribution(observation, 77, now),
        "rider feet produced a duplicate step while the mount was on the ground");

    const RawEvent event{
        .kind = EventKind::footstep,
        .hand = Hand::unknown,
        .actor_id = 88,
        .target_id = 0,
        .source_id = 0,
        .projectile_id = 0,
        .flags = 0,
        .sequence = 9,
        .observed_at = now,
    };
    const auto mount_action = native::resolve_action(event, observation, now);
    if (!mount_action) {
        require(false, "current mount action is available");
        return;
    }
    const auto& resolved_mount_action = mount_action.value();
    require(resolved_mount_action.mount_attributed &&
                resolved_mount_action.attributed_mount_id == 88 &&
                resolved_mount_action.gait == MovementGait::running,
        "current mount step conversion lost the mount's exact gait or identity");
    require(action_attribution_matches(resolved_mount_action, observation, now),
        "fresh current-mount step failed its publication guard");
    Observation changed_player = observation;
    changed_player.player_id = 79;
    require(!action_attribution_matches(resolved_mount_action, changed_player, now),
        "captured mount step survived a player identity change before publication");

    Observation changed_mount = observation;
    if (!changed_mount.world) {
        require(false, "copied mount observation retains its world sample");
        return;
    }
    changed_mount.world->mount_id = 89;
    require(!action_attribution_matches(resolved_mount_action, changed_mount, now),
        "captured mount identity survived a mount change before publication");
    Observation dismounted = observation;
    if (!dismounted.world) {
        require(false, "copied dismount observation retains its world sample");
        return;
    }
    dismounted.world->mounted = false;
    dismounted.world->mount_id = 0;
    require(!action_attribution_matches(resolved_mount_action, dismounted, now),
        "captured mount step survived a dismount before publication");
    require(!current_footstep_attribution(observation, 123, now),
        "NPC step resolved after capture");
}

void footsteps_freshness_and_validity_guards() {
    using skyrim_adapter::detail::action_attribution_matches;
    using skyrim_adapter::detail::current_footstep_attribution;
    using skyrim_adapter::detail::footstep_enqueue_attributable;

    const MonoTime now{std::chrono::milliseconds{200}};
    Observation observation = ingress_observation(now - std::chrono::milliseconds{10});
    if (!observation.world) {
        require(false, "ingress observation includes its world sample");
        return;
    }
    WorldObservation& world = *observation.world;
    world.mounted = true;
    world.mount_id = 88;
    world.mount_gait = MovementGait::running;

    const RawEvent event{
        .kind = EventKind::footstep,
        .hand = Hand::unknown,
        .actor_id = 88,
        .target_id = 0,
        .source_id = 0,
        .projectile_id = 0,
        .flags = 0,
        .sequence = 9,
        .observed_at = now,
    };

    require(footstep_enqueue_attributable(observation, 88, now),
        "valid fresh mount step must be attributable at enqueue");
    require(current_footstep_attribution(observation, 88, now).has_value(),
        "valid fresh mount step must resolve its current attribution");
    const auto valid_action = native::resolve_action(event, observation, now);
    require(valid_action.has_value(), "valid fresh mount step must resolve to an action");
    require(valid_action && action_attribution_matches(*valid_action, observation, now),
        "valid fresh mount step must pass the publication attribution guard");

    Observation stale = observation;
    stale.sampled_at = now - observation_max_age - std::chrono::milliseconds{1};
    require(!footstep_enqueue_attributable(stale, 88, now),
        "stale mount sample was admitted at enqueue");
    require(!native::resolve_action(event, stale, now),
        "stale mount sample was resolved after capture");
    Observation future = observation;
    future.sampled_at = now + std::chrono::milliseconds{1};
    require(!footstep_enqueue_attributable(future, 88, now),
        "future mount sample was admitted at enqueue");
    require(!current_footstep_attribution(future, 88, now),
        "future mount sample resolved after capture");
    require(!footstep_enqueue_attributable(observation, 88, now + std::chrono::milliseconds{151}),
        "callback older than the fresh-snapshot window was admitted");

    Observation invalid = observation;
    invalid.available = false;
    require(!current_footstep_attribution(invalid, 88, now),
        "unavailable player observation admitted a step");
    invalid = observation;
    invalid.alive = false;
    require(!current_footstep_attribution(invalid, 88, now),
        "dead player observation admitted a step");
    invalid = observation;
    invalid.focused = false;
    require(!current_footstep_attribution(invalid, 88, now),
        "unfocused player observation admitted a step");
    invalid = observation;
    invalid.menus_clear = false;
    require(!current_footstep_attribution(invalid, 88, now), "menu observation admitted a step");

    invalid = observation;
    if (!invalid.world) {
        require(false, "invalid mount observation retains its world sample");
        return;
    }
    invalid.world->mount_gait = MovementGait::unknown;
    require(!current_footstep_attribution(invalid, 88, now), "unknown mount gait was guessed");
    invalid = observation;
    if (!invalid.world) {
        require(false, "invalid swimming observation retains its world sample");
        return;
    }
    invalid.world->mount_swimming = true;
    require(!current_footstep_attribution(invalid, 88, now), "swimming mount produced a footstep");
    invalid = observation;
    if (!invalid.world) {
        require(false, "invalid airborne observation retains its world sample");
        return;
    }
    invalid.world->mount_airborne = true;
    require(!current_footstep_attribution(invalid, 88, now), "airborne mount produced a footstep");

    Observation on_foot = ingress_observation(now);
    if (!on_foot.world) {
        require(false, "on-foot observation includes its world sample");
        return;
    }
    on_foot.world->gait = MovementGait::unknown;
    require(!current_footstep_attribution(on_foot, 77, now),
        "stationary player produced a footstep");
    on_foot.world->gait = MovementGait::walking;
    on_foot.world->swimming = true;
    require(!current_footstep_attribution(on_foot, 77, now), "swimming player produced a footstep");
    on_foot.world->swimming = false;
    on_foot.world->airborne = true;
    require(!current_footstep_attribution(on_foot, 77, now), "airborne player produced a footstep");
}

void source_less_swing_requires_an_exact_fresh_empty_hand() {
    using skyrim_adapter::detail::action_attribution_matches;
    using skyrim_adapter::detail::source_less_empty_hand_swing;

    const MonoTime now{std::chrono::milliseconds{500}};
    Observation observation = ingress_observation(now - std::chrono::milliseconds{1});
    observation.hands[1] = {
        .equipped_id = 0,
        .caster_source_id = 0,
        .equipment = EquipmentKind::none,
        .weapon = WeaponClass::none,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    const RawEvent event{
        .kind = EventKind::weapon_swing,
        .hand = Hand::right,
        .actor_id = 77,
        .target_id = 0,
        .source_id = 0,
        .projectile_id = 0,
        .flags = 0,
        .sequence = 12,
        .observed_at = now - std::chrono::milliseconds{1},
    };
    require(source_less_empty_hand_swing(event, observation, now),
        "exact empty-hand swing was rejected");
    const auto action = native::resolve_action(event, observation, now);
    require(action && action->source_known && action->source_kind == EquipmentKind::weapon &&
                action->weapon == WeaponClass::unarmed && action->event.source_id == 0,
        "source-less swing did not classify only the Action as unarmed while preserving its event");
    require(action && action_attribution_matches(*action, observation, now),
        "exact empty-hand action failed postcapture revalidation");

    RawEvent unknown_hand = event;
    unknown_hand.hand = Hand::unknown;
    require(!source_less_empty_hand_swing(unknown_hand, observation, now),
        "unknown swing hand was guessed");
    const auto unknown_action = native::resolve_action(unknown_hand, observation, now);
    require(unknown_action && !unknown_action->source_known,
        "unknown-hand source-less swing became known unarmed");

    RawEvent stale = event;
    stale.observed_at = now - event_max_age - std::chrono::milliseconds{1};
    require(!source_less_empty_hand_swing(stale, observation, now),
        "stale empty-hand swing was classified");
    RawEvent future = event;
    future.observed_at = now + std::chrono::milliseconds{1};
    require(!source_less_empty_hand_swing(future, observation, now),
        "future empty-hand swing was classified");
    Observation stale_sample = observation;
    stale_sample.sampled_at = now - observation_max_age - std::chrono::milliseconds{1};
    require(!source_less_empty_hand_swing(event, stale_sample, now),
        "stale hand observation was classified");

    observation.hands[1].caster_source_id = 99;
    require(!source_less_empty_hand_swing(event, observation, now),
        "source-less swing with caster metadata was classified unarmed");
    observation.hands[1] = {
        .equipped_id = 99,
        .caster_source_id = 0,
        .equipment = EquipmentKind::weapon,
        .weapon = WeaponClass::sword,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    require(!source_less_empty_hand_swing(event, observation, now),
        "source-less swing with an armed hand was classified unarmed");
    observation.hands[1] = {
        .equipped_id = 0,
        .caster_source_id = 0,
        .equipment = EquipmentKind::spell,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = CastMode::concentration,
        .cast_phase = CastPhase::casting,
        .two_handed = false,
    };
    require(!source_less_empty_hand_swing(event, observation, now),
        "source-less swing with spell metadata was classified unarmed");
    const auto spell_action = native::resolve_action(event, observation, now);
    require(spell_action && !spell_action->source_known &&
                spell_action->weapon != WeaponClass::unarmed,
        "source-less swing with spell metadata did not remain unknown");
    observation.hands[1] = {
        .equipped_id = 99,
        .caster_source_id = 0,
        .equipment = EquipmentKind::staff,
        .weapon = WeaponClass::none,
        .spell = SpellKind::fire,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    require(!source_less_empty_hand_swing(event, observation, now),
        "source-less swing with staff metadata was classified unarmed");
    const auto staff_action = native::resolve_action(event, observation, now);
    require(staff_action && !staff_action->source_known &&
                staff_action->weapon != WeaponClass::unarmed,
        "source-less swing with staff metadata did not remain unknown");
    observation.hands[1] = {
        .equipped_id = 0,
        .caster_source_id = 0,
        .equipment = EquipmentKind::none,
        .weapon = WeaponClass::unknown,
        .spell = SpellKind::unknown,
        .cast_mode = CastMode::none,
        .cast_phase = CastPhase::none,
        .two_handed = false,
    };
    require(!source_less_empty_hand_swing(event, observation, now),
        "source-less swing with unknown weapon metadata was classified unarmed");
    RawEvent nonzero_source = event;
    nonzero_source.source_id = 1;
    require(!source_less_empty_hand_swing(nonzero_source, observation, now),
        "a source-bearing swing entered the source-less fallback");
}

} // namespace skyrim_adapter::native_observation_tests
