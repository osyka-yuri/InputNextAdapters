#include "model.hpp"
#include "observation.hpp"
#include "support/test_runner.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void test_player_health_basics() {
    using namespace oblivion_adapter;

    {
        const auto health = calculate_health(50.0F, 100.0F);
        require(health.valid, "Health should be valid for positive max");
        require(std::abs(health.current - 50.0F) < 0.001F, "Current health match");
        require(std::abs(health.maximum - 100.0F) < 0.001F, "Maximum health match");
        require(std::abs(health.ratio - 0.5F) < 0.001F, "Ratio should be 0.5");
    }

    {
        const auto health = calculate_health(0.0F, 100.0F);
        require(health.valid, "Health with 0 current and positive max is valid");
        require(health.current == 0.0F, "Current health 0");
        require(health.ratio == 0.0F, "Ratio 0");
    }

    {
        const auto health = calculate_health(120.0F, 100.0F);
        require(health.valid, "Valid health");
        require(health.current == 120.0F, "Current health 120");
        require(health.ratio == 1.0F, "Ratio clamped to 1.0");
    }

    {
        const auto health = calculate_health(0.0F, 0.0F);
        require(!health.valid, "Health with 0 max is invalid");
        require(health.ratio == 0.0F, "Ratio 0");
    }

    {
        const auto health_clamped = calculate_health(-10.0F, 100.0F);
        require(health_clamped.valid, "Valid with positive max");
        require(health_clamped.current == 0.0F, "Negative current clamped to 0");
        require(health_clamped.ratio == 0.0F, "Ratio 0");

        const auto health_negative = calculate_health(50.0F, -50.0F);
        require(!health_negative.valid, "Negative max health is invalid");
        require(health_negative.maximum == 0.0F, "Negative max clamped to 0");

        const float nan_val = std::numeric_limits<float>::quiet_NaN();
        const auto health_nan = calculate_health(nan_val, 100.0F);
        require(!health_nan.valid, "NaN current must be invalid");
    }
}

void test_invalid_and_nonpositive_damage_rejection() {
    using namespace oblivion_adapter;

    auto& mgr = ObservationManager::instance();
    mgr.reset();

    DamageEvent damage_event{};
    damage_event.damage = 25.0F;
    damage_event.is_player_victim = true;
    damage_event.attacker_form_id = 0x111;
    damage_event.victim_form_id = 0x14;
    require(!mgr.record_damage_event(damage_event),
        "record_damage_event must return false when player unavailable");

    auto obs = mgr.get_observation();
    require(obs.damage_event_count == 0, "Damage rejected when player is unavailable");
    require(obs.last_damage.damage == 0.0F, "No last_damage when player is unavailable");

    const auto health = calculate_health(100.0F, 100.0F);
    mgr.update_player_state(0x14, health, true, false);
    require(mgr.get_observation().player_available, "Player available");

    damage_event.damage = 0.0F;
    require(!mgr.record_damage_event(damage_event),
        "record_damage_event must return false on zero damage");
    obs = mgr.get_observation();
    require(obs.damage_event_count == 0, "Zero damage must not increment event count");
    require(obs.last_damage.damage == 0.0F, "Zero damage must not overwrite last_damage");

    damage_event.damage = -15.0F;
    require(!mgr.record_damage_event(damage_event),
        "record_damage_event must return false on negative damage");
    obs = mgr.get_observation();
    require(obs.damage_event_count == 0, "Negative damage must be rejected");

    damage_event.damage = std::numeric_limits<float>::quiet_NaN();
    require(!mgr.record_damage_event(damage_event),
        "record_damage_event must return false on NaN damage");
    obs = mgr.get_observation();
    require(obs.damage_event_count == 0, "NaN damage must be rejected");

    damage_event.damage = std::numeric_limits<float>::infinity();
    require(!mgr.record_damage_event(damage_event),
        "record_damage_event must return false on infinite damage");
    obs = mgr.get_observation();
    require(obs.damage_event_count == 0, "Infinite damage must be rejected");

    damage_event.damage = -std::numeric_limits<float>::infinity();
    require(!mgr.record_damage_event(damage_event),
        "record_damage_event must return false on negative infinite damage");
    obs = mgr.get_observation();
    require(obs.damage_event_count == 0, "Negative infinite damage must be rejected");

    damage_event.damage = 50.0F;
    damage_event.is_player_victim = false;
    require(!mgr.record_damage_event(damage_event),
        "record_damage_event must return false when victim is not player");
    obs = mgr.get_observation();
    require(obs.damage_event_count == 0, "NPC damage must be rejected");
}

void test_monotonic_sequencing_and_epoch_transitions() {
    using namespace oblivion_adapter;

    auto& mgr = ObservationManager::instance();
    mgr.reset();
    const std::uint64_t epoch1 = mgr.current_epoch();

    const auto health = calculate_health(100.0F, 100.0F);
    mgr.update_player_state(0x14, health, true, false);

    DamageEvent damage_event{};
    damage_event.damage = 10.0F;
    damage_event.is_player_victim = true;
    damage_event.victim_form_id = 0x14;
    require(mgr.record_damage_event(damage_event), "First event recorded successfully");

    auto obs = mgr.get_observation();
    require(obs.damage_event_count == 1, "First event recorded");
    require(obs.last_damage.sequence == 1, "First sequence is 1");
    require(obs.last_damage.epoch == epoch1, "First event belongs to epoch 1");

    damage_event.damage = 20.0F;
    require(mgr.record_damage_event(damage_event), "Second event recorded successfully");
    obs = mgr.get_observation();
    require(obs.damage_event_count == 2, "Second event recorded");
    require(obs.last_damage.sequence == 2, "Second sequence is 2");
    require(obs.last_damage.epoch == epoch1, "Second event belongs to epoch 1");

    mgr.invalidate_player();
    obs = mgr.get_observation();
    require(!obs.player_available, "Player unavailable");
    require(obs.last_damage.damage == 0.0F, "last_damage must be cleared on player loss");
    require(obs.last_damage.sequence == 0, "last_damage sequence must be 0 on player loss");
    require(obs.damage_event_count == 2,
        "damage_event_count preserved on player loss within epoch");

    mgr.reset();
    const std::uint64_t epoch2 = mgr.current_epoch();
    require(epoch2 > epoch1, "Epoch must increment on reset");

    obs = mgr.get_observation();
    require(obs.epoch == epoch2, "Observation epoch updated to epoch 2");
    require(!obs.player_available, "Player not available after reset");
    require(obs.last_damage.damage == 0.0F, "No stale damage after reset");
    require(obs.damage_event_count == 0, "damage_event_count reset to 0 on session reset");

    mgr.update_player_state(0x14, health, true, false);

    damage_event.damage = 30.0F;
    require(mgr.record_damage_event(damage_event),
        "Third event in new session recorded successfully");

    obs = mgr.get_observation();
    require(obs.damage_event_count == 1, "First event in new session count is 1");
    require(obs.last_damage.damage == 30.0F, "Damage is 30");
    require(obs.last_damage.sequence == 3,
        "Monotonic sequence must be 3 (no reset to 1, no collision)");
    require(obs.last_damage.epoch == epoch2, "New event belongs to epoch 2");

    damage_event.damage = 40.0F;
    require(mgr.record_damage_event(damage_event), "Fourth event recorded successfully");
    obs = mgr.get_observation();
    require(obs.last_damage.sequence == 4, "Sequence must be 4");
    require(obs.last_damage.epoch == epoch2, "Event belongs to epoch 2");
}

void test_observation_freshness_and_invalidation() {
    using namespace oblivion_adapter;

    auto& mgr = ObservationManager::instance();
    mgr.reset();

    auto obs = mgr.get_observation();
    require(!obs.player_available, "Initially unavailable");
    require(!obs.health.valid, "Initially invalid health");
    require(obs.damage_event_count == 0, "No damage events");
    require(obs.tick_count == 0, "No ticks");

    const auto valid_health = calculate_health(80.0F, 100.0F);
    mgr.update_player_state(0x14, valid_health, true, false);

    obs = mgr.get_observation();
    require(obs.player_available, "Player available");
    require(obs.health.valid, "Health valid");
    require(obs.health.current == 80.0F, "Current health 80");

    mgr.invalidate_player();
    obs = mgr.get_observation();
    require(!obs.player_available, "Player unavailable");
    require(!obs.health.valid, "Health invalidated when player unavailable");
    require(obs.health.current == 0.0F, "Health values cleared");
    require(!obs.player_alive, "Alive flag cleared");
    require(obs.last_damage.damage == 0.0F, "Damage event cleared on loss");

    mgr.update_player_state(0x14, valid_health, true, false);
    require(mgr.get_observation().player_available, "Player available again");

    DamageEvent damage_event{};
    damage_event.damage = 18.0F;
    damage_event.is_player_victim = true;
    require(mgr.record_damage_event(damage_event),
        "Record damage event before health invalidation");
    obs = mgr.get_observation();
    require(obs.damage_event_count == 1, "Damage event count is 1");
    require(obs.last_damage.damage == 18.0F, "Damage recorded before invalidation");

    PlayerHealth invalid_health{};
    invalid_health.valid = false;
    mgr.update_player_state(0x14, invalid_health, true, false);

    obs = mgr.get_observation();
    require(!obs.player_available, "Previous snapshot not kept when health reading fails");
    require(!obs.health.valid, "Health must not remain valid");
    require(obs.last_damage.damage == 0.0F, "Damage event cleared on invalid health");
    require(obs.damage_event_count == 1,
        "damage_event_count preserved when health is temporarily invalid");

    mgr.reset();
    obs = mgr.get_observation();
    require(obs.damage_event_count == 0, "damage_event_count reset to 0 strictly on session reset");
}

void test_session_save_load_and_menu_reset() {
    using namespace oblivion_adapter;

    auto& mgr = ObservationManager::instance();
    mgr.reset();

    const auto health = calculate_health(70.0F, 100.0F);
    mgr.update_player_state(0x14, health, true, true);

    DamageEvent damage_event{};
    damage_event.damage = 25.0F;
    damage_event.is_player_victim = true;
    require(mgr.record_damage_event(damage_event), "Damage event recorded successfully");

    require(mgr.get_observation().damage_event_count == 1, "Event count 1 before reset");

    const std::uint64_t prev_epoch = mgr.current_epoch();
    mgr.reset();
    mgr.invalidate_player();

    const auto obs = mgr.get_observation();
    require(obs.epoch > prev_epoch, "Epoch advanced on session reset");
    require(!obs.player_available, "Player not available after reset");
    require(!obs.health.valid, "Health invalid after reset");
    require(obs.damage_event_count == 0, "Damage count reset to 0");
    require(obs.last_damage.damage == 0.0F, "Old damage event cleared (no stale reuse)");
    require(!obs.last_damage.is_player_victim, "Last damage victim flag cleared");
    require(obs.last_damage.sequence == 0, "Last damage sequence cleared");
}

void test_menu_mode_pause_and_resume_without_session_change() {
    using namespace oblivion_adapter;

    auto& mgr = ObservationManager::instance();
    mgr.reset();
    const std::uint64_t active_epoch = mgr.current_epoch();

    const auto valid_health = calculate_health(80.0F, 100.0F);
    mgr.update_player_state(0x14, valid_health, true, false);
    require(mgr.get_observation().player_available, "Player available in active game");

    DamageEvent damage_event{};
    damage_event.damage = 35.0F;
    damage_event.is_player_victim = true;
    require(mgr.record_damage_event(damage_event), "Damage event recorded successfully");

    auto obs = mgr.get_observation();
    require(obs.last_damage.damage == 35.0F, "Damage recorded");
    const std::uint64_t seq1 = obs.last_damage.sequence;
    require(seq1 > 0, "Sequence allocated");
    require(obs.last_damage.epoch == active_epoch, "Event belongs to active epoch");

    mgr.invalidate_player();

    obs = mgr.get_observation();
    require(!obs.player_available, "Player marked unavailable during menu/pause");
    require(mgr.current_epoch() == active_epoch,
        "Session epoch must NOT change during in-game menu/pause");
    require(obs.last_damage.damage == 0.0F,
        "Transient damage cleared to prevent stale reproduction");
    require(obs.damage_event_count == 1, "Damage count preserved during menu pause");

    mgr.update_player_state(0x14, valid_health, true, false);

    obs = mgr.get_observation();
    require(obs.player_available, "Player available again after closing menu");
    require(mgr.current_epoch() == active_epoch, "Session epoch must remain identical");
    require(obs.epoch == active_epoch, "Observation epoch matches active session");
    require(obs.last_damage.damage == 0.0F,
        "Old damage must NOT be reproduced upon returning from menu");
    require(obs.damage_event_count == 1, "Damage count preserved upon unpause");

    DamageEvent ev2{};
    ev2.damage = 15.0F;
    ev2.is_player_victim = true;
    require(mgr.record_damage_event(ev2), "Second damage event recorded successfully");

    obs = mgr.get_observation();
    require(obs.last_damage.damage == 15.0F, "New damage recorded");
    require(obs.damage_event_count == 2, "Damage count increments to 2 on new hit");
    require(obs.last_damage.sequence == seq1 + 1, "Monotonic sequence continues by +1");
    require(obs.last_damage.epoch == active_epoch, "Event belongs to same active epoch");
}

void test_alive_and_combat_state_distinction() {
    using namespace oblivion_adapter;

    auto& mgr = ObservationManager::instance();
    mgr.reset();

    const auto valid_h = calculate_health(50.0F, 100.0F);

    mgr.update_player_state(0x14, valid_h, true, true);
    auto obs = mgr.get_observation();
    require(obs.player_available, "Player available");
    require(obs.player_alive, "Player alive");
    require(obs.player_in_combat, "Player in combat");

    const auto zero_h = calculate_health(0.0F, 100.0F);
    mgr.update_player_state(0x14, zero_h, false, false);
    obs = mgr.get_observation();
    require(obs.player_available, "Player still tracked as available object");
    require(!obs.player_alive, "Player dead (confirmed reading)");
    require(!obs.player_in_combat, "Player out of combat");

    mgr.invalidate_player();
    obs = mgr.get_observation();
    require(!obs.player_available, "Player marked unavailable on read failure");
    require(!obs.health.valid, "Health invalid on read failure");
}

void test_damage_classification() {
    using namespace oblivion_adapter;

    const int dummy_player = 0x14;
    const int dummy_enemy = 0x123;
    const void* player_ptr = &dummy_player;
    const void* enemy_ptr = &dummy_enemy;

    DamageClassificationInput input{};
    input.damage = 25.5F;
    input.victim_ptr = player_ptr;
    input.player_ptr = player_ptr;
    input.attacker_ptr = enemy_ptr;
    input.victim_form_id = 0x14;
    input.attacker_form_id = 0x123;

    const DamageEvent ev_enemy = classify_damage_event(input);
    require(ev_enemy.is_player_victim, "Player victim correctly classified");
    require(!ev_enemy.is_player_attacker, "Enemy is not player attacker");
    require(ev_enemy.damage == 25.5F, "Damage value preserved");
    require(ev_enemy.victim_form_id == 0x14, "Victim form ID preserved");
    require(ev_enemy.attacker_form_id == 0x123, "Attacker form ID preserved");

    input.damage = 10.0F;
    input.attacker_ptr = player_ptr;
    input.attacker_form_id = 0x14;
    const DamageEvent ev_self = classify_damage_event(input);
    require(ev_self.is_player_victim, "Self-damage victim is player");
    require(ev_self.is_player_attacker, "Self-damage attacker is player (pointer equality)");
    require(ev_self.damage == 10.0F, "Self-damage amount preserved");

    // Fall damage can arrive with a null attacker.
    input.damage = 15.0F;
    input.attacker_ptr = nullptr;
    input.attacker_form_id = 0;
    const DamageEvent ev_fall = classify_damage_event(input);
    require(ev_fall.is_player_victim, "Fall damage victim is player");
    require(!ev_fall.is_player_attacker, "Fall damage has null attacker (not player attacker)");

    input.damage = 50.0F;
    input.victim_ptr = enemy_ptr;
    input.victim_form_id = 0x123;
    input.attacker_ptr = enemy_ptr;
    input.attacker_form_id = 0x123;
    const DamageEvent ev_npc = classify_damage_event(input);
    require(!ev_npc.is_player_victim, "NPC victim must NOT be classified as player victim");
    require(ev_npc.damage == 0.0F, "Unclassified event has 0 damage");

    input.victim_ptr = nullptr;
    require(!classify_damage_event(input).is_player_victim, "Null victim rejected");
    input.victim_ptr = player_ptr;
    input.player_ptr = nullptr;
    require(!classify_damage_event(input).is_player_victim, "Null player rejected");

    input.player_ptr = player_ptr;
    input.damage = 0.0F;
    require(!classify_damage_event(input).is_player_victim, "Zero damage rejected");
    input.damage = -5.0F;
    require(!classify_damage_event(input).is_player_victim, "Negative damage rejected");
    input.damage = std::numeric_limits<float>::quiet_NaN();
    require(!classify_damage_event(input).is_player_victim, "NaN damage rejected");
    input.damage = std::numeric_limits<float>::infinity();
    require(!classify_damage_event(input).is_player_victim, "Positive infinity damage rejected");
    input.damage = -std::numeric_limits<float>::infinity();
    require(!classify_damage_event(input).is_player_victim, "Negative infinity damage rejected");

    auto& mgr = ObservationManager::instance();
    mgr.reset();
    const auto health = calculate_health(100.0F, 100.0F);
    mgr.update_player_state(0x14, health, true, false);

    require(mgr.record_damage_event(ev_enemy),
        "Classified enemy damage accepted by ObservationManager");
    auto obs = mgr.get_observation();
    require(obs.damage_event_count == 1, "Damage event count is 1");
    require(obs.last_damage.damage == 25.5F, "Observation contains accepted damage");
    require(!obs.last_damage.is_player_attacker, "Observation confirms attacker is not player");

    require(mgr.record_damage_event(ev_self),
        "Classified self damage accepted by ObservationManager");
    obs = mgr.get_observation();
    require(obs.damage_event_count == 2, "Damage event count is 2");
    require(obs.last_damage.damage == 10.0F, "Observation contains latest self-damage");
    require(obs.last_damage.is_player_attacker, "Observation confirms attacker is player");
}

void test_initial_observation_state() {
    using namespace oblivion_adapter;

    const auto& mgr = ObservationManager::instance();
    require(mgr.current_epoch() == 1, "Default current_epoch() must be 1");
    const auto obs = mgr.get_observation();
    require(obs.epoch == 1, "Default get_observation().epoch must be 1");
    require(mgr.current_epoch() == obs.epoch,
        "current_epoch() and get_observation().epoch must match in initial state");
    require(!obs.player_available, "Player must not be available initially");
    require(obs.damage_event_count == 0, "No damage events in initial state");
    require(obs.last_damage.damage == 0.0F, "No last_damage in initial state");
}

void test_tick_count_contract() {
    using namespace oblivion_adapter;

    auto& mgr = ObservationManager::instance();
    mgr.reset();

    require(mgr.get_observation().tick_count == 0, "Initial tick_count must be 0");

    mgr.increment_tick();
    require(mgr.get_observation().tick_count == 1, "First increment yields 1");

    mgr.increment_tick();
    mgr.increment_tick();
    require(mgr.get_observation().tick_count == 3, "Sequential increments yield 3");

    mgr.invalidate_player();
    require(mgr.get_observation().tick_count == 3, "Invalidation preserves tick_count");

    const auto valid_health = calculate_health(50.0F, 100.0F);
    mgr.update_player_state(0x14, valid_health, true, false);
    require(mgr.get_observation().tick_count == 3,
        "update_player_state does not double-count ticks");

    mgr.reset();
    require(mgr.get_observation().tick_count == 0, "reset clears tick_count to 0");
}

} // namespace

int main() {
    return inputnext::test::run_main("Oblivion model tests", [] -> int {
        test_initial_observation_state();
        test_tick_count_contract();
        test_player_health_basics();
        test_invalid_and_nonpositive_damage_rejection();
        test_monotonic_sequencing_and_epoch_transitions();
        test_observation_freshness_and_invalidation();
        test_session_save_load_and_menu_reset();
        test_menu_mode_pause_and_resume_without_session_change();
        test_alive_and_combat_state_distinction();
        test_damage_classification();
        std::cout << "All Oblivion model tests passed.\n";
        return 0;
    });
}
