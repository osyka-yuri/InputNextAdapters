#include "task_handler.hpp"
#include "bow_projectiles.hpp"
#include "diagnostics.hpp"
#include "effect_model.hpp"
#include "effect_worker.hpp"
#include "event_handler.hpp"
#include "interfaces.hpp"
#include "model.hpp"
#include "observation.hpp"
#include "options_input.hpp"
#include "player_state.hpp"
#include "xobse_sdk.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace oblivion_adapter::native {
namespace {

std::atomic<bool> s_first_tick_logged{false};
std::atomic<bool> s_last_available{false};

bool s_has_previous_state{false};
bool s_prev_alive{false};
bool s_prev_in_combat{false};

std::uint64_t next_effect_frame{1};
std::uint64_t effect_generation{};
EffectMode previous_effect_mode{EffectMode::unavailable};
std::optional<EffectEvent> pending_attack;
BowProjectileTracker bow_projectiles;

void reset_effect_tracking() noexcept {
    previous_effect_mode = EffectMode::unavailable;
    pending_attack.reset();
    bow_projectiles.reset();
    EventHandler::discard_contacts();
}

void sync_effect_generation(const EffectWorker& effects) noexcept {
    const auto generation = effects.generation();
    if (effect_generation != generation) {
        effect_generation = generation;
        reset_effect_tracking();
    }
}

void complete_attack(EffectWorker& effects, const EffectObservation& observation) noexcept {
    auto attack = std::exchange(pending_attack, std::nullopt);
    if (!attack || observation.mode != EffectMode::gameplay || !observation.melee_attack ||
        attack->weapon != observation.weapon || attack->source_id != observation.weapon_id) {
        return;
    }
    attack->melee_confirmed = true;
    attack->power_attack = observation.power_attack;
    static_cast<void>(effects.push_event(*attack));
}

void log_availability_transition(const PlayerHealth& health, std::uint32_t form_id) noexcept {
    const bool became_available = !s_last_available.exchange(true, std::memory_order_relaxed);
    if (became_available) {
        try {
            const std::string msg =
                "on_game_tick: player available, formID=" + diagnostics::format_form_id(form_id) +
                " health=" + std::to_string(health.current) + "/" + std::to_string(health.maximum);
            diagnostics::write(msg);
        } catch (...) {
            diagnostics::write("on_game_tick: player available");
        }
    }
}

void log_player_state_changes(bool alive, bool in_combat) noexcept {
    if (!s_has_previous_state) {
        return;
    }

    if (alive != s_prev_alive) {
        try {
            const std::string msg = "on_game_tick: player alive transition: " +
                                    std::string(s_prev_alive ? "alive" : "dead") + " -> " +
                                    std::string(alive ? "alive" : "dead");
            diagnostics::write(msg);
        } catch (...) {
            diagnostics::write("on_game_tick: player alive transition observed");
        }
    }

    if (in_combat != s_prev_in_combat) {
        try {
            const std::string msg = "on_game_tick: player combat transition: " +
                                    std::string(s_prev_in_combat ? "in_combat" : "out_of_combat") +
                                    " -> " + std::string(in_combat ? "in_combat" : "out_of_combat");
            diagnostics::write(msg);
        } catch (...) {
            diagnostics::write("on_game_tick: player combat transition observed");
        }
    }
}

void mark_player_unavailable(std::string_view reason) noexcept {
    if (s_last_available.exchange(false, std::memory_order_relaxed)) {
        diagnostics::write(reason);
    }
    ObservationManager::instance().invalidate_player();
}

void publish_unavailable(EffectWorker& effects,
    MonoTime now,
    EffectMode mode = EffectMode::unavailable) noexcept {
    reset_effect_tracking();
    effects.publish({.sampled_at = now,
        .epoch = ObservationManager::instance().current_epoch(),
        .frame = next_effect_frame++,
        .mode = mode});
}

void record_bow_shot(std::uint32_t arrow, const EffectObservation& observation) noexcept {
    if (arrow != 0U) {
        TaskHandler::record({
            .kind = EffectEventKind::bow_shot,
            .at = observation.sampled_at,
            .mode = EffectMode::gameplay,
            .actor_id = observation.player_id,
            .source_id = arrow,
            .weapon = Weapon::bow,
        });
    }
}

} // namespace

void TaskHandler::on_game_tick() noexcept {
    auto& interfaces = PluginInterfaceManager::instance();
    if (!interfaces.on_game_thread() || !interfaces.effects()) {
        return;
    }
    if (!s_first_tick_logged.exchange(true, std::memory_order_relaxed)) {
        diagnostics::write("on_game_tick: first game loop tick observed on game thread");
    }

    interfaces.poll_pause_shortcut();
    if (!interfaces.is_session_active()) {
        mark_player_unavailable("on_game_tick: player unavailable (no active session)");
        return;
    }

    ObservationManager::instance().increment_tick();

    auto& effects = *interfaces.effects();
    const auto now = std::chrono::steady_clock::now();
    const auto mode = PlayerStateReader::read_effect_mode();
    sync_effect_generation(effects);
    if (mode == EffectMode::unavailable || mode == EffectMode::loading) {
        mark_player_unavailable("on_game_tick: game mode unavailable");
        publish_unavailable(effects, now, mode);
        return;
    }

    PlayerCharacter* player = PlayerStateReader::get_player();
    if (!player || !PlayerStateReader::is_player_pointer_valid(player)) {
        mark_player_unavailable("on_game_tick: player unavailable (invalid player pointer)");
        publish_unavailable(effects, now);
        return;
    }

    const auto health_opt = PlayerStateReader::read_player_health(player);
    if (!health_opt || !health_opt->valid) {
        mark_player_unavailable("on_game_tick: player health invalid or unreadable");
        publish_unavailable(effects, now);
        return;
    }

    const auto alive_opt = PlayerStateReader::read_player_alive(player);
    const auto combat_opt = PlayerStateReader::read_player_in_combat(player);
    const auto form_id_opt = PlayerStateReader::read_player_form_id(player);

    if (!alive_opt.has_value() || !combat_opt.has_value() || !form_id_opt.has_value()) {
        mark_player_unavailable("on_game_tick: player status (alive/combat/formID) unreadable");
        publish_unavailable(effects, now);
        return;
    }

    const bool alive = *alive_opt;
    const bool in_combat = *combat_opt;
    const std::uint32_t form_id = *form_id_opt;

    if (mode == EffectMode::gameplay) {
        log_availability_transition(*health_opt, form_id);
        log_player_state_changes(alive, in_combat);
        s_prev_alive = alive;
        s_prev_in_combat = in_combat;
        s_has_previous_state = true;
        ObservationManager::instance().update_player_state(form_id, *health_opt, alive, in_combat);
    } else {
        // Diagnostic snapshots hide the player in menus. Effects still need the menu state.
        mark_player_unavailable("on_game_tick: player temporarily unavailable (menu or pause)");
    }

    if (!alive) {
        publish_unavailable(effects, now);
        return;
    }

    EffectObservation observation{};
    observation.sampled_at = now;
    observation.epoch = ObservationManager::instance().current_epoch();
    observation.frame = next_effect_frame;
    observation.mode = mode;
    observation.alive = alive;
    observation.in_combat = in_combat;
    observation.health = *health_opt;
    observation.player_id = form_id;
    PlayerStateReader::read_effect_state(player,
        observation,
        effects.stealth_enabled() && foreground_is_game());
    complete_attack(effects, observation);

    if (mode == EffectMode::gameplay && previous_effect_mode == EffectMode::gameplay) {
        EventHandler::flush_contacts();
    } else {
        EventHandler::discard_contacts();
    }
    const auto arrow = bow_projectiles.sample(observation,
        std::chrono::steady_clock::now(),
        [player](std::uint32_t cell, ProjectileScan& scan) {
            return PlayerStateReader::scan_projectiles(player, cell, scan);
        });
    record_bow_shot(arrow, observation);
    previous_effect_mode = mode;
    ++next_effect_frame;
    effects.publish(observation);
}

void TaskHandler::record(EffectEvent event) noexcept {
    if (auto* effects = PluginInterfaceManager::instance().effects()) {
        event.epoch = ObservationManager::instance().current_epoch();
        event.frame = next_effect_frame;
        if (event.kind == EffectEventKind::attack) {
            // xOBSE invokes OnAttack before the engine installs the new action.
            pending_attack = event;
        } else {
            static_cast<void>(effects->push_event(event));
        }
    }
}

void TaskHandler::note_bow_attachment(PlayerCharacter* player, const EffectEvent& event) noexcept {
    const auto* effects = PluginInterfaceManager::instance().effects();
    if (!effects) {
        return;
    }
    sync_effect_generation(*effects);
    EffectObservation observation{
        .sampled_at = event.at,
        .epoch = ObservationManager::instance().current_epoch(),
        .mode = event.mode,
        .alive = PlayerStateReader::read_player_alive(player).value_or(false),
        .player_id = event.actor_id,
        .cell_id = PlayerStateReader::read_player_cell_id(player),
    };
    observation.weapon = PlayerStateReader::read_equipped_weapon(player, observation.weapon_id);
    const auto previous_shot = bow_projectiles.note_attachment(observation,
        [player](std::uint32_t cell, ProjectileScan& scan) {
            return PlayerStateReader::scan_projectiles(player, cell, scan);
        });
    record_bow_shot(previous_shot, observation);
}

EffectMode TaskHandler::last_effect_mode() noexcept {
    return previous_effect_mode;
}

bool TaskHandler::register_task(OBSETasks2Interface* tasks) noexcept {
    if (s_task_ptr) {
        return true;
    }
    if (!tasks || !tasks->EnqueueTask) {
        diagnostics::write("register_task: missing OBSETasks2Interface or EnqueueTask pointer");
        return false;
    }

    s_task_ptr = tasks->EnqueueTask(&TaskHandler::on_game_tick);
    if (!s_task_ptr) {
        diagnostics::write("register_task: EnqueueTask returned null");
        return false;
    }

    diagnostics::write("register_task: successfully enqueued game thread task");
    return true;
}

void TaskHandler::unregister_task(OBSETasks2Interface* tasks) noexcept {
    if (tasks && tasks->RemoveTask && s_task_ptr) {
        tasks->RemoveTask(s_task_ptr);
        s_task_ptr = nullptr;
        diagnostics::write("unregister_task: removed game thread task");
    }
    reset_state();
}

void TaskHandler::reset_diagnostics() noexcept {
    s_has_previous_state = false;
    s_prev_alive = false;
    s_prev_in_combat = false;
    reset_effect_tracking();
}

void TaskHandler::reset_state() noexcept {
    s_first_tick_logged.store(false, std::memory_order_relaxed);
    s_last_available.store(false, std::memory_order_relaxed);
    reset_diagnostics();
}

} // namespace oblivion_adapter::native
