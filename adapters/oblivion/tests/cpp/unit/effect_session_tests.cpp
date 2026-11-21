#include "effect_model.hpp"
#include "effect_session.hpp"
#include "effect_settings.hpp"
#include "input_next_core.h"
#include "input_next_effects.h"
#include "inputnext_adapters/body_program.hpp"
#include "inputnext_adapters/output_session.hpp"
#include "model.hpp"
#include "observation.hpp"
#include "output_session/fake_output.hpp"
#include "support/test_runner.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace std::chrono_literals;
using inputnext::adapters::MonoTime;
using inputnext::adapters::TickClock;
using inputnext::test::output_session::FakeOutput;
using oblivion_adapter::EffectEvent;
using oblivion_adapter::EffectEventKind;
using oblivion_adapter::EffectMode;
using oblivion_adapter::EffectObservation;
using oblivion_adapter::EffectSession;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

TickClock at(MonoTime now) noexcept {
    return {.supplied = now, .use_steady_clock = false};
}

EffectObservation observation(MonoTime sampled_at,
    std::uint64_t frame,
    EffectMode mode = EffectMode::gameplay,
    std::uint64_t epoch = 1) noexcept {
    return {
        .sampled_at = sampled_at,
        .epoch = epoch,
        .frame = frame,
        .mode = mode,
        .alive = true,
        .health = oblivion_adapter::calculate_health(100.0F, 100.0F),
        .player_id = 1,
    };
}

EffectEvent hit_event(MonoTime event_at,
    std::uint64_t frame,
    EffectMode mode = EffectMode::gameplay,
    std::uint64_t epoch = 1) noexcept {
    return {
        .kind = EffectEventKind::hit,
        .at = event_at,
        .epoch = epoch,
        .frame = frame,
        .mode = mode,
        .actor_id = 1,
        .target_id = 2,
    };
}

EffectEvent equip_event(MonoTime event_at, std::uint64_t frame) noexcept {
    return {
        .kind = EffectEventKind::equip,
        .at = event_at,
        .epoch = 1,
        .frame = frame,
        .mode = EffectMode::inventory,
        .actor_id = 1,
        .source_id = 7,
    };
}

oblivion_adapter::EffectSettings test_settings() noexcept {
    oblivion_adapter::EffectSettings settings{};
    settings.led.enabled = false;
    return settings;
}

void focus_and_bind(EffectSession& session, MonoTime start, std::uint64_t epoch = 1) {
    session.set_focus(true, start);
    session.publish(observation(start, 1, EffectMode::gameplay, epoch));
    session.tick(at(start));
    session.publish(observation(start + 1ms, 2, EffectMode::gameplay, epoch));
    session.tick(at(start + 1ms));
}

bool has_haptic_delivery(const FakeOutput& output) {
    return std::ranges::any_of(output.body_calls, [](const auto& call) {
        const auto& request = call.request;
        return request.pcm.frame_count != 0 || request.fallback_rumble.low_frequency > 0.0F ||
               request.fallback_rumble.high_frequency > 0.0F;
    }) || std::ranges::any_of(output.triggers, [](const auto& call) {
        return ((call.channel_mask & INPUT_NEXT_EFFECTS_RUMBLE) != 0U &&
                   (call.rumble.low_frequency > 0.0F || call.rumble.high_frequency > 0.0F)) ||
               ((call.channel_mask & INPUT_NEXT_EFFECTS_LEFT_TRIGGER) != 0U &&
                   call.left_trigger.strength > 0.0F) ||
               ((call.channel_mask & INPUT_NEXT_EFFECTS_RIGHT_TRIGGER) != 0U &&
                   call.right_trigger.strength > 0.0F);
    });
}

std::vector<float> one_delivered_pcm(const FakeOutput& output) {
    const auto first = std::ranges::find_if(output.body_calls,
        [](const auto& call) { return call.request.pcm.frame_count != 0; });
    require(first != output.body_calls.end(), "Combat contact submits a PCM effect");
    const auto count = static_cast<std::size_t>(std::ranges::count_if(output.body_calls,
        [](const auto& call) { return call.request.pcm.frame_count != 0; }));
    require(count == 1, "One combat contact produces one PCM submission");
    return first->samples;
}

std::vector<float> run_incoming_contact(bool skip_contact_frame, bool blocked) {
    FakeOutput output;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 7;
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    const auto session_owner = std::make_unique<EffectSession>(output, test_settings());
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);
    output.body_calls.clear();

    const auto event_at = start + 2ms;
    auto hit = hit_event(event_at, 3);
    hit.actor_id = 2;
    hit.target_id = 1;
    auto damage = hit;
    damage.kind = EffectEventKind::damage;
    damage.sequence = 0;
    damage.amount = 15.0F;
    require(session.push_event(hit), "Incoming hit enters the inbox before its sample");
    require(session.push_event(damage), "Matching damage enters the inbox before its sample");

    auto contact = observation(event_at, 3);
    contact.block_hit = blocked;
    session.publish(contact);
    if (skip_contact_frame) {
        auto later = contact;
        later.sampled_at += 1ms;
        later.frame = 4;
        later.block_hit = false;
        session.publish(later);
    }
    session.tick(at(event_at + 1ms));
    require(has_haptic_delivery(output), "The incoming contact reaches the output");
    return one_delivered_pcm(output);
}

std::vector<float> run_unarmed_contact(bool skip_contact_frame, oblivion_adapter::Weapon weapon) {
    FakeOutput output;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 7;
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    const auto session_owner = std::make_unique<EffectSession>(output, test_settings());
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);
    output.body_calls.clear();

    const auto event_at = start + 2ms;
    auto hit = hit_event(event_at, 3);
    hit.target_id = 2;
    hit.weapon = weapon;
    require(session.push_event(hit), "Player hit enters the inbox before its sample");

    auto contact = observation(event_at, 3);
    contact.weapon = oblivion_adapter::Weapon::unarmed;
    contact.melee_attack = true;
    session.publish(contact);
    if (skip_contact_frame) {
        auto later = contact;
        later.sampled_at += 1ms;
        later.frame = 4;
        later.weapon = oblivion_adapter::Weapon::blade_one_hand;
        later.weapon_id = 0x90;
        later.melee_attack = false;
        session.publish(later);
    }
    session.tick(at(event_at + 1ms));
    require(has_haptic_delivery(output), "The player hit reaches the output");
    return one_delivered_pcm(output);
}

std::vector<float> run_block_impact_sequence(bool first_impact) {
    FakeOutput output;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = 7;
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    const auto session_owner = std::make_unique<EffectSession>(output, test_settings());
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);
    output.body_calls.clear();

    auto first = observation(start + 2ms, 3);
    first.block_hit = first_impact;
    session.publish(first);
    session.publish(observation(start + 12ms, 4));

    auto second = observation(start + 25ms, 5);
    second.block_hit = true;
    session.publish(second);
    session.tick(at(second.sampled_at));
    return one_delivered_pcm(output);
}

void test_binding_requires_fresh_sample_and_discards_old_events() {
    FakeOutput output;
    auto const settings = test_settings();
    const auto session_owner = std::make_unique<EffectSession>(output, settings);
    auto& session = *session_owner;
    const MonoTime start{};

    session.set_focus(true, start);
    session.publish(observation(start, 1));
    require(session.push_event(hit_event(start, 1)), "Initial event enters ingress before binding");
    session.tick(at(start));
    require(output.opens.size() == 1, "The first observation creates an output binding");
    require(!has_haptic_delivery(output), "Binding does not deliver the pre-binding event");

    session.publish(observation(start + 1ms, 2));
    session.tick(at(start + 1ms));
    require(!has_haptic_delivery(output),
        "A fresh post-binding sample does not replay events captured on the previous binding");

    session.publish(observation(start + 2ms, 3));
    require(session.push_event(hit_event(start + 2ms, 3)), "Fresh player hit enters the queue");
    session.tick(at(start + 2ms));
    require(has_haptic_delivery(output), "Fresh event is delivered after binding baseline");
}

void test_delayed_classic_impact_ages_once() {
    FakeOutput output;
    const auto settings = test_settings();
    const auto session_owner = std::make_unique<EffectSession>(output, settings);
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);
    require(session.push_event(hit_event(start + 2ms, 3)), "Pending hit enters the queue");
    session.publish(observation(start + 3ms, 3));
    session.tick(at(start + 90ms));
    require(has_haptic_delivery(output),
        "A late render uses the remaining impact lifetime without aging it from the sample twice");
}

void test_contact_evidence_survives_a_skipped_output_frame() {
    const auto timely_block = run_incoming_contact(false, true);
    const auto skipped_block = run_incoming_contact(true, true);
    require(timely_block == skipped_block,
        "A delayed tick preserves the block animation and its single matching contact");

    const auto ordinary_damage = run_incoming_contact(true, false);
    require(ordinary_damage != skipped_block, "An ordinary hit remains distinct from a block");

    using oblivion_adapter::Weapon;
    const auto timely_unarmed = run_unarmed_contact(false, Weapon::unarmed);
    const auto skipped_unarmed = run_unarmed_contact(true, Weapon::unarmed);
    require(timely_unarmed == skipped_unarmed,
        "A delayed tick preserves the confirmed unarmed profile from the contact frame");
    require(run_unarmed_contact(true, Weapon::unknown) != skipped_unarmed,
        "An unidentified source stays generic even during an unarmed animation");
}

void test_two_block_impacts_survive_one_worker_pass() {
    const auto two_blocks = run_block_impact_sequence(true);
    require(two_blocks != run_block_impact_sequence(false),
        "A second block cannot replace the queued pulse from the first animation");
}

void test_menu_only_admits_confirmed_ui_actions() {
    {
        FakeOutput output;
        auto const settings = test_settings();
        const auto session_owner = std::make_unique<EffectSession>(output, settings);
        auto& session = *session_owner;
        const MonoTime start{};
        focus_and_bind(session, start);

        const auto menu_sample_at = start + 2ms;
        session.publish(observation(menu_sample_at, 3, EffectMode::inventory));
        auto attack = hit_event(menu_sample_at, 3, EffectMode::inventory);
        require(!session.push_event(attack), "Gameplay hit is rejected while inventory is open");
        require(session.push_event(equip_event(menu_sample_at, 3)),
            "Confirmed inventory equip is admitted in its matching menu");
        session.tick(at(menu_sample_at));
        require(has_haptic_delivery(output), "Admitted menu action produces its short haptic");
    }

    {
        FakeOutput output;
        auto const settings = test_settings();
        const auto session_owner = std::make_unique<EffectSession>(output, settings);
        auto& session = *session_owner;
        const MonoTime start{};
        focus_and_bind(session, start);

        const auto menu_sample_at = start + 2ms;
        session.publish(observation(menu_sample_at, 3, EffectMode::inventory));
        require(!session.push_event(hit_event(menu_sample_at, 3, EffectMode::inventory)),
            "Unconfirmed combat action is suppressed in the inventory");
        session.tick(at(menu_sample_at));
        require(!has_haptic_delivery(output), "Menu transition alone does not create a pulse");
    }
}

void test_menu_change_discards_previous_profile_tail() {
    FakeOutput output;
    auto const settings = test_settings();
    const auto session_owner = std::make_unique<EffectSession>(output, settings);
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    session.publish(observation(start + 2ms, 3, EffectMode::inventory));
    require(session.push_event(equip_event(start + 2ms, 3)), "Inventory action enters ingress");
    session.tick(at(start + 2ms));
    require(has_haptic_delivery(output), "Inventory action plays in the inventory");
    output.body_calls.clear();

    session.publish(observation(start + 3ms, 4, EffectMode::alchemy));
    session.tick(at(start + 3ms));
    require(!has_haptic_delivery(output), "An inventory tail cannot resume in the alchemy menu");
}

void test_menu_during_trigger_submission_neutralizes_it() {
    FakeOutput output;
    auto const settings = test_settings();
    const auto session_owner = std::make_unique<EffectSession>(output, settings);
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    auto blocking = observation(start + 2ms, 3);
    blocking.blocking = true;
    session.publish(blocking);
    output.after_trigger_submit = [&] {
        session.publish(observation(start + 2ms, 4, EffectMode::menu));
    };
    session.tick(at(start + 2ms));
    require(output.triggers.size() == 1U, "Blocking starts one adaptive-trigger request");
    require(output.trigger_clear_requests.size() == 1U,
        "A menu appearing during the request clears the accepted trigger");
}

void test_cancelled_bow_during_trigger_submission_neutralizes_it() {
    FakeOutput output;
    const auto session_owner = std::make_unique<EffectSession>(output, test_settings());
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    auto drawn = observation(start + 2ms, 3);
    drawn.weapon = oblivion_adapter::Weapon::bow;
    drawn.weapon_id = 6;
    drawn.bow = oblivion_adapter::BowPhase::attached;
    session.publish(drawn);
    require(session.push_event(hit_event(start + 2ms, 3)), "Independent impact is queued");
    output.body_calls.clear();
    output.after_trigger_submit = [&] {
        auto released = drawn;
        released.frame = 4;
        released.bow = oblivion_adapter::BowPhase::none;
        session.publish(released);
    };
    session.tick(at(start + 2ms));

    require(output.triggers.size() == 1, "The snapshot starts one bow resistance request");
    require(output.trigger_clear_requests.size() == 1,
        "Cancelling in the same gameplay mode immediately clears the obsolete resistance");
    for (const auto& call : output.body_calls) {
        require(call.request.pcm.frame_count == 0 &&
                    call.request.fallback_rumble.low_frequency == 0.0F &&
                    call.request.fallback_rumble.high_frequency == 0.0F,
            "Delivery leaves the body silent after cancellation");
    }

    output.after_trigger_submit = {};
    output.triggers.clear();
    output.body_calls.clear();
    session.tick(at(start + 3ms));
    require(output.triggers.empty(), "The latest observation keeps resistance clear");
    require(has_haptic_delivery(output), "The still-current impact can play on the next pass");
}

void test_same_holds_allow_delivery_on_a_new_sample() {
    FakeOutput output;
    const auto session_owner = std::make_unique<EffectSession>(output, test_settings());
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    auto blocking = observation(start + 2ms, 3);
    blocking.blocking = true;
    session.publish(blocking);
    require(session.push_event(hit_event(start + 2ms, 3)), "Independent impact is queued");
    output.body_calls.clear();
    output.after_trigger_submit = [&] {
        auto later = blocking;
        later.frame = 4;
        session.publish(later);
    };
    session.tick(at(start + 2ms));

    require(output.triggers.size() == 1, "Blocking starts one adaptive-trigger request");
    require(output.trigger_clear_requests.empty(), "Unchanged holds stay active on a fresh frame");
    require(!output.body_calls.empty(), "The later snapshot does not fence unrelated delivery");
}

void test_reconcile_uses_latest_block_state_and_events() {
    FakeOutput output;
    const auto session_owner = std::make_unique<EffectSession>(output, test_settings());
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    auto blocking = observation(start + 2ms, 3);
    blocking.blocking = true;
    session.publish(blocking);
    output.after_context_query = [&] {
        auto lowered = blocking;
        lowered.frame = 4;
        lowered.blocking = false;
        session.publish(lowered);
        require(session.push_event(hit_event(start + 2ms, 4)), "New sample's hit is queued");
    };
    session.tick(at(start + 2ms));

    require(output.triggers.empty(), "Cancelled block never reaches the trigger");
    require(has_haptic_delivery(output), "The freshest observation's hit plays in this pass");
}

void test_disabled_body_leaves_trigger_and_led_channels_available() {
    const MonoTime start{};
    for (const bool body_enabled : {false, true}) {
        FakeOutput output;
        output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_LIGHTBAR;
        oblivion_adapter::EffectSettings settings{};
        settings.body_enabled = body_enabled;
        settings.rumble_scale = body_enabled ? 0.0F : 1.0F;
        const auto session_owner = std::make_unique<EffectSession>(output, settings);
        auto& session = *session_owner;
        focus_and_bind(session, start);

        auto blocking = observation(start + 2ms, 3);
        blocking.blocking = true;
        blocking.health = oblivion_adapter::calculate_health(70.0F, 100.0F);
        session.publish(blocking);
        require(session.push_event(hit_event(start + 2ms, 3)), "Gameplay event is queued");
        session.tick(at(start + 2ms));

        require(!output.triggers.empty(), "Disabled body permits adaptive trigger output");
        require(!output.leds.empty(), "Disabled body permits health LED output");
        require(output.body_calls.empty(), "Disabled body never submits even a silent lease");
        require(!session.diagnostics().body_owner_present, "Disabled body leaves no lease owner");
    }
}

void test_stealth_only_delivery_withdraws_in_menu() {
    FakeOutput output;
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_LIGHTBAR;
    oblivion_adapter::EffectSettings settings{};
    settings.body_enabled = false;
    settings.triggers_enabled = false;
    settings.led.health_enabled = false;
    const auto session_owner = std::make_unique<EffectSession>(output, settings);
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    auto sample = observation(start + 2ms, 3);
    sample.sneak_detected = false;
    session.publish(sample);
    session.tick(at(sample.sampled_at));
    require(output.leds.size() == 1U, "Stealth alone enables lightbar delivery");
    require(output.leds.front().leds.lightbar_rgb == 0x0000FFU, "Hidden uses blue");

    session.publish(observation(start + 3ms, 4, EffectMode::menu));
    session.tick(at(start + 3ms));
    require(output.led_clear_requests.size() == 1U, "Entering a menu clears stealth feedback");
    require(output.leds.size() == 1U, "The menu does not renew the indication");

    sample = observation(start + 4ms, 5);
    sample.sneak_detected = true;
    session.publish(sample);
    session.tick(at(sample.sampled_at));
    require(output.leds.size() == 2U, "A fresh gameplay sample resumes stealth feedback");
    require(output.leds.back().leds.lightbar_rgb == 0xFF00FFU, "Detected uses magenta");
}

void test_overflow_and_lifecycle_invalidation_clear_the_inbox() {
    FakeOutput output;
    auto const settings = test_settings();
    const auto session_owner = std::make_unique<EffectSession>(output, settings);
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    const auto event_at = start + 2ms;
    for (std::size_t index = 0; index < oblivion_adapter::effect_inbox_capacity; ++index) {
        require(session.push_event(hit_event(event_at, 2)),
            "Ingress accepts events to its capacity");
    }
    require(!session.push_event(hit_event(event_at, 2)),
        "Overflow is detected rather than wrapping");
    session.tick(at(event_at));
    require(!has_haptic_delivery(output),
        "Overflow invalidates both the snapshot and queued events");
    require(session.drain(at(event_at)), "Overflow cleanup closes the old output context");

    session.publish(observation(event_at - 1ms, 3));
    require(!session.push_event(hit_event(event_at, 3)),
        "An old pre-reset event cannot attach to the next published observation");
    session.invalidate(event_at + 1ms);
    session.publish(observation(event_at, 4));
    require(!session.push_event(hit_event(event_at, 4)),
        "Save or lifecycle invalidation requires a strictly newer observation");
}

void test_focus_and_stale_snapshot_stop_delivery() {
    FakeOutput output;
    auto const settings = test_settings();
    const auto session_owner = std::make_unique<EffectSession>(output, settings);
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    const auto event_at = start + 2ms;
    session.publish(observation(event_at, 3));
    require(session.push_event(hit_event(event_at, 3)), "Fresh hit is queued");
    session.tick(at(event_at));
    require(has_haptic_delivery(output), "Fresh hit reaches the fake output");
    const auto delivered_body_calls = output.body_calls.size();

    const auto lost_focus_at = event_at + 1ms;
    session.set_focus(false, lost_focus_at);
    session.tick(at(lost_focus_at));
    require(session.drain(at(lost_focus_at)), "Focus loss drains the prior binding");
    session.publish(observation(lost_focus_at, 4));
    session.tick(at(lost_focus_at));
    require(output.body_calls.size() == delivered_body_calls,
        "Focus loss prevents an old snapshot from producing another body refresh");

    session.set_focus(true, lost_focus_at + 1ms);
    session.publish(observation(lost_focus_at + 1ms, 5));
    session.tick(at(lost_focus_at + 1ms));
    session.publish(observation(lost_focus_at + 2ms, 6));
    session.tick(at(lost_focus_at + 2ms));
    require(output.opens.size() == 2, "Focus regain starts a clean output binding");

    const auto stale_at = lost_focus_at + 2ms + oblivion_adapter::effect_max_age + 1ms;
    const auto stale_calls = output.body_calls.size();
    session.tick(at(stale_at));
    require(session.drain(at(stale_at)), "Stale sample cleanup completes");
    require(output.body_calls.size() == stale_calls,
        "Snapshot age over 150 ms cannot renew or replay a prior effect");
}

void test_loading_snapshot_invalidates_gameplay_state() {
    FakeOutput output;
    const auto session_owner = std::make_unique<EffectSession>(output, test_settings());
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    const auto loading_at = start + 2ms;
    session.publish(observation(loading_at, 3, EffectMode::loading));
    require(!session.push_event(hit_event(loading_at, 3)),
        "A loading snapshot cannot keep the prior gameplay observation admissible");
    session.tick(at(loading_at));
    require(session.drain(at(loading_at)), "Loading transition drains the old output context");

    const auto gameplay_at = loading_at + 1ms;
    session.publish(observation(gameplay_at, 4));
    session.tick(at(gameplay_at));
    const auto event_at = gameplay_at + 1ms;
    require(session.push_event(hit_event(event_at, 5)),
        "A fresh post-load observation admits a new gameplay event");
    session.publish(observation(event_at, 5));
    session.tick(at(event_at));
    require(has_haptic_delivery(output), "Fresh post-load gameplay can deliver a new contact");
}

void test_gate_counter_requires_the_same_controller_after_loading() {
    enum class Transition : std::uint8_t {
        same_controller,
        new_controller,
        focus_loss,
        different_save
    };
    for (const auto transition : {Transition::same_controller,
             Transition::new_controller,
             Transition::focus_loss,
             Transition::different_save}) {
        auto& manager = oblivion_adapter::ObservationManager::instance();
        manager.reset();
        const auto save_epoch = manager.current_epoch();
        FakeOutput output;
        auto settings = test_settings();
        settings.profiles[oblivion_adapter::profile_index(oblivion_adapter::EffectId::gate_closed)]
            .enabled = true;
        const auto session_owner = std::make_unique<EffectSession>(output, settings);
        auto& session = *session_owner;
        const MonoTime start{};
        focus_and_bind(session, start, save_epoch);

        auto before = observation(start + 2ms, 3, EffectMode::gameplay, save_epoch);
        before.cell_id = 20;
        before.action_stats = oblivion_adapter::ActionStats{.gates_closed = 3};
        session.publish(before);
        session.tick(at(before.sampled_at));

        const auto loading_at = start + 10ms;
        manager.update_player_state(before.player_id,
            before.health,
            before.alive,
            before.in_combat);
        manager.invalidate_player();
        require(manager.current_epoch() == save_epoch &&
                    !manager.get_observation().player_available,
            "Cell loading invalidates the player while preserving the save epoch");
        session.publish(observation(loading_at, 4, EffectMode::loading, manager.current_epoch()));
        session.tick(at(loading_at));
        if (transition == Transition::new_controller) {
            ++output.target.connection_id;
        } else if (transition == Transition::focus_loss) {
            session.set_focus(false, start + 20ms);
            session.set_focus(true, start + 200ms);
        }

        if (transition == Transition::different_save) {
            manager.reset();
        }
        auto returned =
            observation(start + 300ms, 5, EffectMode::gameplay, manager.current_epoch());
        returned.cell_id = 21;
        returned.action_stats = oblivion_adapter::ActionStats{.gates_closed = 4};
        manager.update_player_state(returned.player_id,
            returned.health,
            returned.alive,
            returned.in_combat);
        session.publish(returned);
        session.tick(at(returned.sampled_at));
        require(!has_haptic_delivery(output), "The binding sample cannot play a gate closure");

        ++returned.frame;
        returned.sampled_at += 16ms;
        session.publish(returned);
        session.tick(at(returned.sampled_at));
        const bool expected = transition == Transition::same_controller;
        require(has_haptic_delivery(output) == expected,
            "A closure can cross cell loading only on the same controller and save");

        output.body_calls.clear();
        ++returned.frame;
        returned.sampled_at += 400ms;
        session.publish(returned);
        session.tick(at(returned.sampled_at));
        require(!has_haptic_delivery(output), "An unchanged gate counter cannot replay a closure");
    }
}

void test_full_inbox_keeps_space_for_a_gate_counter_result() {
    using oblivion_adapter::ActionStats;
    using oblivion_adapter::EffectId;
    using oblivion_adapter::profile_index;
    FakeOutput output;
    auto settings = test_settings();
    settings.profiles[profile_index(EffectId::gate_closed)].enabled = true;
    settings.profiles[profile_index(EffectId::generic_hit)].enabled = false;
    const auto session_owner = std::make_unique<EffectSession>(output, settings);
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    auto sample = observation(start + 2ms, 3);
    sample.cell_id = 20;
    sample.action_stats = ActionStats{.gates_closed = 3};
    session.publish(sample);
    session.tick(at(sample.sampled_at));

    sample.sampled_at += 20ms;
    ++sample.frame;
    sample.action_stats = ActionStats{.gates_closed = 4};
    for (std::size_t index = 0; index < oblivion_adapter::effect_inbox_capacity; ++index) {
        auto hit = hit_event(sample.sampled_at, sample.frame);
        hit.target_id += static_cast<std::uint32_t>(index);
        require(session.push_event(hit), "Each of the 64 game events fits in the inbox");
    }
    session.publish(sample);
    session.tick(at(sample.sampled_at));
    require(has_haptic_delivery(output), "A full inbox leaves the gate result room to play");

    output.body_calls.clear();
    sample.sampled_at += 400ms;
    ++sample.frame;
    session.publish(sample);
    session.tick(at(sample.sampled_at));
    require(!has_haptic_delivery(output), "The delivered gate result does not replay");
}

void test_menu_counter_result_survives_closing_before_delivery() {
    FakeOutput output;
    const auto session_owner = std::make_unique<EffectSession>(output, test_settings());
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    auto sample = observation(start + 2ms, 3, EffectMode::container);
    sample.action_stats = oblivion_adapter::ActionStats{};
    session.publish(sample);
    session.tick(at(sample.sampled_at));
    require(!has_haptic_delivery(output), "Opening a container establishes a silent baseline");

    sample = observation(start + 3ms, 4);
    sample.action_stats = oblivion_adapter::ActionStats{.items_pickpocketed = 2};
    session.publish(sample);
    session.tick(at(sample.sampled_at));
    require(has_haptic_delivery(output), "The confirmed pickpocket survives closing the menu");
}

void test_controller_rebind_drops_queued_old_binding_event() {
    FakeOutput output;
    auto const settings = test_settings();
    const auto session_owner = std::make_unique<EffectSession>(output, settings);
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    const auto switch_at = start + 2ms;
    output.target.connection_id += 1;
    session.publish(observation(switch_at, 3));
    require(session.push_event(hit_event(switch_at, 3)),
        "Event accepted before the next binding snapshot");
    session.tick(at(switch_at));
    require(output.opens.size() == 2, "Changed controller creates a new output context");
    require(!has_haptic_delivery(output), "Queued event from the old controller is discarded");

    session.publish(observation(switch_at + 1ms, 4));
    session.tick(at(switch_at + 1ms));
    require(!has_haptic_delivery(output), "New controller waits for a fresh snapshot");
    session.publish(observation(switch_at + 2ms, 5));
    require(session.push_event(hit_event(switch_at + 2ms, 5)), "New controller event is accepted");
    session.tick(at(switch_at + 2ms));
    require(has_haptic_delivery(output), "New controller receives only post-binding events");
}

void test_save_epoch_change_discards_old_events_and_rebinds() {
    FakeOutput output;
    auto const settings = test_settings();
    const auto session_owner = std::make_unique<EffectSession>(output, settings);
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    const auto old_event_at = start + 2ms;
    session.publish(observation(old_event_at, 3, EffectMode::gameplay, 1));
    require(session.push_event(hit_event(old_event_at, 3, EffectMode::gameplay, 1)),
        "Current save event is queued before the session epoch changes");

    const auto new_save_at = start + 3ms;
    session.publish(observation(new_save_at, 1, EffectMode::gameplay, 2));
    session.tick(at(new_save_at));
    require(output.opens.size() == 2, "Save change retires the prior binding and opens a new one");

    const auto new_binding_sample_at = start + 4ms;
    session.publish(observation(new_binding_sample_at, 2, EffectMode::gameplay, 2));
    session.tick(at(new_binding_sample_at));
    require(!session.push_event(hit_event(new_binding_sample_at, 2, EffectMode::gameplay, 1)),
        "An event from the previous save cannot enter the new epoch");
    require(!has_haptic_delivery(output), "Changing saves does not replay queued combat events");

    const auto fresh_event_at = start + 5ms;
    session.publish(observation(fresh_event_at, 3, EffectMode::gameplay, 2));
    require(session.push_event(hit_event(fresh_event_at, 3, EffectMode::gameplay, 2)),
        "New save event is admitted after its post-binding snapshot");
    session.tick(at(fresh_event_at));
    require(has_haptic_delivery(output), "Fresh events from the new save produce output");
}

void test_busy_close_and_ack_retry_until_drained() {
    FakeOutput output;
    auto const settings = test_settings();
    const auto session_owner = std::make_unique<EffectSession>(output, settings);
    auto& session = *session_owner;
    const MonoTime start{};
    focus_and_bind(session, start);

    output.close_status = INPUT_NEXT_STATUS_BUSY;
    session.stop(start + 2ms);
    session.tick(at(start + 2ms));
    require(output.close_calls == 1 && output.ack_close_calls == 0,
        "BUSY close leaves the exact context retained for a later worker pass");

    output.close_status = INPUT_NEXT_STATUS_REVOKED;
    output.ack_close_status = INPUT_NEXT_STATUS_BUSY;
    session.tick(at(start + 3ms));
    require(output.close_calls == 2 && output.ack_close_calls == 1,
        "Successful close records its receipt even when acknowledgement is BUSY");
    require(!session.drain(at(start + 3ms)), "BUSY acknowledgement remains pending");

    output.ack_close_status = INPUT_NEXT_STATUS_OK;
    require(session.drain(at(start + 4ms)), "A later worker drain acknowledges the close receipt");
    const auto close_calls = output.close_calls;
    const auto ack_calls = output.ack_close_calls;
    require(session.drain(at(start + 5ms)), "Repeated drain stays successful after cleanup");
    require(output.close_calls == close_calls && output.ack_close_calls == ack_calls,
        "Repeated cleanup does not close or acknowledge an already released context again");
}

} // namespace

int main() {
    return inputnext::test::run_main("Oblivion effect session tests", [] -> int {
        test_binding_requires_fresh_sample_and_discards_old_events();
        test_delayed_classic_impact_ages_once();
        test_contact_evidence_survives_a_skipped_output_frame();
        test_two_block_impacts_survive_one_worker_pass();
        test_menu_only_admits_confirmed_ui_actions();
        test_menu_change_discards_previous_profile_tail();
        test_menu_during_trigger_submission_neutralizes_it();
        test_cancelled_bow_during_trigger_submission_neutralizes_it();
        test_same_holds_allow_delivery_on_a_new_sample();
        test_reconcile_uses_latest_block_state_and_events();
        test_disabled_body_leaves_trigger_and_led_channels_available();
        test_stealth_only_delivery_withdraws_in_menu();
        test_overflow_and_lifecycle_invalidation_clear_the_inbox();
        test_focus_and_stale_snapshot_stop_delivery();
        test_loading_snapshot_invalidates_gameplay_state();
        test_gate_counter_requires_the_same_controller_after_loading();
        test_full_inbox_keeps_space_for_a_gate_counter_result();
        test_menu_counter_result_survives_closing_before_delivery();
        test_controller_rebind_drops_queued_old_binding_event();
        test_save_epoch_change_discards_old_events_and_rebinds();
        test_busy_close_and_ack_retry_until_drained();
        std::cout << "All Oblivion effect session tests passed.\n";
        return 0;
    });
}
