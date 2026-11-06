#include "native_observation/native_observation_test_cases.hpp"
#include "native_observation/native_observation_test_support.hpp"

#include "native/observation.hpp"
#include "skyrim_adapter/model.hpp"

#include <RE/A/ActorState.h>
#include <cstdint>

namespace skyrim_adapter::native_observation_tests {

namespace {

struct FakeSetting {
    enum class Type : std::uint8_t { kBool, kInteger };

    Type type{};
    bool value{};

    [[nodiscard]] Type GetType() const noexcept { return type; }

    [[nodiscard]] bool GetBool() const noexcept { return value; }
};

} // namespace

void gamepad_vibration_preference_requires_a_typed_boolean() {
    using native::detail::read_boolean_setting;

    require(!read_boolean_setting(static_cast<const FakeSetting*>(nullptr)),
        "missing native preference must be unavailable for body output");
    const FakeSetting wrong_type{.type = FakeSetting::Type::kInteger, .value = true};
    require(!read_boolean_setting(&wrong_type), "a nonboolean preference must fail closed");
    const FakeSetting disabled{.type = FakeSetting::Type::kBool, .value = false};
    require(!read_boolean_setting(&disabled).value_or(true),
        "a native false preference must remain false");
    const FakeSetting enabled{.type = FakeSetting::Type::kBool, .value = true};
    require(read_boolean_setting(&enabled).value_or(false),
        "a native true preference must remain true");
}

void bow_phases_use_native_states() {
    using RE::ATTACK_STATE_ENUM;
    using skyrim_adapter::native::detail::bow_phase;

    require(bow_phase(ATTACK_STATE_ENUM::kBowDraw) == BowPhase::draw,
        "bow draw state was not mapped");
    require(bow_phase(ATTACK_STATE_ENUM::kBowAttached) == BowPhase::attached,
        "bow attached state was not mapped");
    require(bow_phase(ATTACK_STATE_ENUM::kBowDrawn) == BowPhase::drawn,
        "bow drawn state was not mapped");
    require(bow_phase(ATTACK_STATE_ENUM::kBowReleasing) == BowPhase::releasing,
        "bow releasing state was not mapped");
    require(bow_phase(ATTACK_STATE_ENUM::kBowReleased) == BowPhase::released,
        "bow released state was not mapped");
    require(bow_phase(ATTACK_STATE_ENUM::kBowNextAttack) == BowPhase::next_attack,
        "bow next-attack state was not mapped");
    require(bow_phase(ATTACK_STATE_ENUM::kBowFollowThrough) == BowPhase::follow_through,
        "bow follow-through state was not mapped");
    require(bow_phase(ATTACK_STATE_ENUM::kSwing) == BowPhase::none,
        "melee state was reported as a bow phase");
    require(bow_phase(static_cast<ATTACK_STATE_ENUM>(0xFF)) == BowPhase::unknown,
        "unrecognized native attack state was guessed");
}

void movement_gait_requires_direction_and_uses_native_precedence() {
    using skyrim_adapter::native::detail::movement_gait;
    RE::ActorState::ActorState1 state{};
    state.sprinting = 1;
    require(movement_gait(state) == MovementGait::unknown,
        "a gait flag without directional movement remains unknown");
    state.movingForward = 1;
    state.sprinting = 0;
    state.walking = 1;
    require(movement_gait(state) == MovementGait::walking, "walking state was not recognized");
    state.running = 1;
    require(movement_gait(state) == MovementGait::running, "running takes precedence over walking");
    state.sneaking = 1;
    require(movement_gait(state) == MovementGait::sneaking,
        "sneaking takes precedence over running");
    require(movement_gait(state) == MovementGait::sneaking, "sneaking state was not retained");
    state.sprinting = 1;
    require(movement_gait(state) == MovementGait::sprinting,
        "sprinting takes precedence over sneaking");
}

void menu_gate_ignores_always_open_hud_and_suppresses_interactions() {
    using skyrim_adapter::native::detail::menu_suppresses_effects;

    require(!menu_suppresses_effects(true, true, false, false, false, false, true, true, false),
        "always-open HUD was treated as an interaction menu");
    require(!menu_suppresses_effects(false, false, true, false, false, false, false, false, false),
        "menu outside the active stack suppressed effects");
    require(menu_suppresses_effects(true, false, true, false, false, false, false, false, false),
        "pausing menu did not suppress effects");
    require(menu_suppresses_effects(true, false, false, false, false, false, true, false, false),
        "input-context menu did not suppress effects");
    require(menu_suppresses_effects(true, false, false, false, false, false, false, true, false),
        "cursor interaction did not suppress effects");
}

void knock_state_only_identifies_verified_down_phases() {
    using RE::KNOCK_STATE_ENUM;
    using skyrim_adapter::native::detail::knocked_down;

    require(!knocked_down(KNOCK_STATE_ENUM::kNormal),
        "normal knock state was reported as a knockdown");
    require(!knocked_down(KNOCK_STATE_ENUM::kExplodeLeadIn),
        "unverified pre-knock state was guessed");
    require(!knocked_down(KNOCK_STATE_ENUM::kOut),
        "out state without a down sample was not treated as an entry");
    require(!knocked_down(KNOCK_STATE_ENUM::kOutLeadIn),
        "out lead-in without a down sample was treated as an entry");
    require(knocked_down(KNOCK_STATE_ENUM::kDown), "down state was not recognized");
    require(!knocked_down(KNOCK_STATE_ENUM::kGetUp),
        "get-up recovery was treated as a new knockdown");
    require(!knocked_down(static_cast<KNOCK_STATE_ENUM>(0xFF)), "unknown knock state was guessed");
}

} // namespace skyrim_adapter::native_observation_tests
