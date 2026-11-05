#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"
#include <algorithm>
#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <input_next_effects.h>
#include <memory>
#include <skyrim_adapter/blocking_menus.hpp>
#include <skyrim_adapter/coordinator.hpp>
#include <skyrim_adapter/effect_settings.hpp>
#include <skyrim_adapter/model.hpp>
#include <string_view>

namespace skyrim_adapter::tests::session {

namespace {

void run_game_vibration_gate(bool enable_hd) {
    const auto fixture_owner = std::make_unique<Fixture>();
    Fixture& fixture = *fixture_owner;
    const std::uint64_t audio_instance_id = enable_hd ? 71 : 0;
    const std::uint32_t route = enable_hd ? INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD
                                          : INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC;
    if (enable_hd) {
        advertise_hd(fixture.output, audio_instance_id);
    }
    fixture.output.body_route = route;
    fixture.settle(idle());

    fixture.time += 10ms;
    Action fire = spell_release(Hand::left, fixture.time, 700, SpellKind::fire);
    const Observation enabled = idle();
    fixture.observe(enabled, std::span{&fire, 1});
    fixture.coordinator.tick(fixture.time);
    const auto& initial_body = fixture.output.body_submissions.back();
    INFO("enabled game preference installs PCM only when HD is advertised");
    REQUIRE((initial_body.request.pcm_command == (enable_hd ? INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE
                                                            : INPUT_NEXT_EFFECTS_BODY_PCM_KEEP) &&
             (enable_hd ? !initial_body.samples.empty() : initial_body.samples.empty())));
    INFO("the pre-off body replacement has an accepted receipt for the selected route and audio "
         "instance");
    REQUIRE(
        (fixture.output.body_submissions.back().request.audio_instance_id == audio_instance_id &&
            fixture.coordinator.diagnostics().accepted_body_route == route &&
            (!enable_hd || fixture.coordinator.diagnostics().body_fallback_reason ==
                               INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE)));
    INFO("the same event also installs its independent adaptive trigger");
    REQUIRE((!fixture.output.submissions.empty() &&
             fixture.output.submissions.back().left_trigger.mode ==
                 INPUT_NEXT_EFFECTS_TRIGGER_VIBRATION));

    const auto replacements_before_off = std::count_if(fixture.output.body_submissions.begin(),
        fixture.output.body_submissions.end(),
        [](const BodySubmission& item) {
            return item.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE;
        });
    fixture.time += 10ms;
    Observation disabled = idle();
    disabled.body_vibration_enabled = false;
    fixture.observe(disabled);
    fixture.coordinator.tick(fixture.time);
    const auto& stopped = fixture.output.body_submissions.back().request;
    INFO("turning game vibration off preserves the route policy and submits a neutral fallback to "
         "that instance");
    REQUIRE((stopped.pcm_command == (enable_hd ? INPUT_NEXT_EFFECTS_BODY_PCM_STOP
                                               : INPUT_NEXT_EFFECTS_BODY_PCM_KEEP) &&
             stopped.fallback_rumble.low_frequency == 0.0F &&
             stopped.fallback_rumble.high_frequency == 0.0F &&
             stopped.audio_instance_id == audio_instance_id));
    INFO("game vibration gate leaves accepted adaptive trigger channels active");
    REQUIRE(std::ranges::none_of(fixture.output.clears, [](const Clear& clear) {
        return (clear.mask &
                   (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER)) != 0;
    }));

    fixture.time += 10ms;
    fixture.observe(enabled, std::span{&fire, 1});
    fixture.coordinator.tick(fixture.time);
    const auto replacements_after_on = std::count_if(fixture.output.body_submissions.begin(),
        fixture.output.body_submissions.end(),
        [](const BodySubmission& item) {
            return item.request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE;
        });
    INFO("reenabling game vibration does not replay the previously admitted transient");
    REQUIRE(replacements_after_on == replacements_before_off);
}

} // namespace

TEST_CASE("skyrim.session.known_blocking_menu_classification_gates",
    "[component][skyrim][session]") {
    INFO("unexpected count of known blocking menus");
    REQUIRE(known_blocking_menus.size() == 22);
    for (const auto menu : known_blocking_menus) {
        INFO("known blocking menu rejected");
        REQUIRE(is_known_blocking_menu(menu));
    }
    INFO("empty menu name classified as blocking");
    REQUIRE(!is_known_blocking_menu(""));
    INFO("hud menu classified as blocking");
    REQUIRE(!is_known_blocking_menu("HUD Menu"));
    INFO("case insensitivity leaked into blocking menu classification");
    REQUIRE(!is_known_blocking_menu("inventorymenu"));
    INFO("trailing space incorrectly accepted as blocking menu");
    REQUIRE(!is_known_blocking_menu("InventoryMenu "));
    INFO("trailing space in console accepted as blocking menu");
    REQUIRE(!is_known_blocking_menu("Console "));
    INFO("unknown menu classified as blocking");
    REQUIRE(!is_known_blocking_menu("UnknownMenu"));
    const std::array<char, 8> non_null_terminated_buf{'C', 'o', 'n', 's', 'o', 'l', 'e', '!'};
    INFO("non-null-terminated string_view slice for Console rejected");
    REQUIRE(is_known_blocking_menu(std::string_view{non_null_terminated_buf.data(), 7}));
    INFO("non-null-terminated string_view slice with different length accepted");
    REQUIRE(!is_known_blocking_menu(std::string_view{non_null_terminated_buf.data(), 8}));
}

TEST_CASE("skyrim.session.game_vibration_gate_classic", "[component][skyrim][session]") {
    run_game_vibration_gate(false);
}

TEST_CASE("skyrim.session.game_vibration_gate_hd", "[component][skyrim][session]") {
    run_game_vibration_gate(true);
}

TEST_CASE("skyrim.session.trigger_start_overrides_preserve_valid_recipe_windows",
    "[component][skyrim][session]") {
    const auto bow_policy = std::make_unique<PolicyConfig>();
    bow_policy->effects.triggers[static_cast<std::size_t>(TriggerId::bow_draw)].start = 0.8F;
    const auto bow_fixture_owner = std::make_unique<Fixture>(*bow_policy);
    Fixture& bow_fixture = *bow_fixture_owner;
    bow_fixture.settle(idle());
    Observation drawing = idle();
    drawing.bow = BowPhase::draw;
    drawing.hands[1] = {
        .equipped_id = 51,
        .caster_source_id = 0,
        .equipment = EquipmentKind::bow,
        .weapon = WeaponClass::bow,
    };
    bow_fixture.advance(drawing);
    INFO("the real BowDraw recipe retains a valid end when start extends beyond its authored "
         "boundary");
    REQUIRE((!bow_fixture.output.submissions.empty() &&
             bow_fixture.output.submissions.back().right_trigger.mode ==
                 INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK &&
             bow_fixture.output.submissions.back().right_trigger.start == 0.8F &&
             bow_fixture.output.submissions.back().right_trigger.end == 0.8F));

    const auto block_policy = std::make_unique<PolicyConfig>();
    block_policy->effects.triggers[static_cast<std::size_t>(TriggerId::weapon_block)].start = 1.0F;
    const auto block_fixture_owner = std::make_unique<Fixture>(*block_policy);
    Fixture& block_fixture = *block_fixture_owner;
    block_fixture.settle(idle());
    Observation blocking = idle();
    blocking.blocking = true;
    blocking.hands[1] = {
        .equipped_id = 52,
        .caster_source_id = 0,
        .equipment = EquipmentKind::weapon,
        .weapon = WeaponClass::sword,
    };
    block_fixture.advance(blocking);
    INFO("the real block recipe accepts the inclusive start upper bound without an invalid "
         "interval");
    REQUIRE((!block_fixture.output.submissions.empty() &&
             block_fixture.output.submissions.back().left_trigger.mode ==
                 INPUT_NEXT_EFFECTS_TRIGGER_FEEDBACK &&
             block_fixture.output.submissions.back().left_trigger.start == 1.0F &&
             block_fixture.output.submissions.back().left_trigger.end == 1.0F));
}

TEST_CASE("skyrim.session.gate_and_freshness", "[component][skyrim][session]") {
    const auto fixture_owner = std::make_unique<Fixture>();
    auto& fixture = *fixture_owner;
    fixture.settle(casting());
    fixture.coordinator.control(Control::focus_lost, fixture.time);
    fixture.coordinator.tick(fixture.time);
    const auto previous = fixture.output.submissions.size();
    fixture.advance(casting());
    INFO("blurred game produced effects");
    REQUIRE(fixture.output.submissions.size() == previous);
    fixture.coordinator.control(Control::menu_suppressed, fixture.time);
    fixture.coordinator.control(Control::focus_gained, fixture.time);
    fixture.advance(casting());
    INFO("focus gain bypassed menu suppression");
    REQUIRE(fixture.output.submissions.size() == previous);
    fixture.coordinator.control(Control::menu_resumed, fixture.time);
    fixture.advance(casting());
    INFO("fresh playable state did not resume");
    REQUIRE(fixture.output.submissions.size() > previous);
    const auto clear_count = fixture.output.clears.size();
    fixture.coordinator.tick(fixture.time + 151ms);
    INFO("stale snapshot kept held output");
    REQUIRE(fixture.output.clears.size() > clear_count);
}

} // namespace skyrim_adapter::tests::session
