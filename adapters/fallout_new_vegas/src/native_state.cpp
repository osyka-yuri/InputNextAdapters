#include "native_state.hpp"

#include "diagnostics.hpp"
#include "dry_fire_hook.hpp"
#include "event_reducer.hpp"
#include "native_game.hpp"
#include "native_observation.hpp"
#include "stealth_detection.hpp"

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/settings.hpp>
#include <fallout_gamebryo/worker.hpp>
#include <input_next_effects.h>
#include <inputnext_adapters/audio_haptics.hpp>
#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <nvse/PluginAPI.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <new> // IWYU pragma: keep
#include <optional>
#include <span>
#include <utility>

namespace fallout_new_vegas {
namespace {
using fallout_gamebryo::Mode;
using fallout_gamebryo::MonoTime;
using fallout_gamebryo::WeaponClass;
using std::chrono::steady_clock;

bool load_settings(const std::filesystem::path& directory, fallout_gamebryo::Settings& settings) {
    constexpr auto config_name = "InputNextFalloutNewVegas.ini";
    const auto config_path = directory / config_name;
    fallout_gamebryo::ConfigError error{};
    switch (fallout_gamebryo::read_config(config_path, settings, &error)) {
    case fallout_gamebryo::ConfigStatus::invalid:
    case fallout_gamebryo::ConfigStatus::io_error:
        if (error.line != 0U) {
            log(std::format("initialization skipped. {}:{} {}",
                config_name,
                error.line,
                error.message));
        } else {
            log(std::format("initialization skipped. {} {}", config_name, error.message));
        }
        return false;
    case fallout_gamebryo::ConfigStatus::missing:
        log("InputNextFalloutNewVegas.ini is missing. Using built-in defaults");
        break;
    case fallout_gamebryo::ConfigStatus::loaded:
        break;
    }

    using Override = fallout_gamebryo::AudioHapticOverrideStatus;
    const auto audio = fallout_gamebryo::load_audio_haptics(config_path,
        settings,
        inputnext::adapters::AudioHapticGame::fallout_new_vegas);
    switch (audio.override_status) {
    case Override::loaded:
        log("loaded audio haptic overrides from InputNextFalloutNewVegas.audiohaptics");
        break;
    case Override::invalid:
        log("InputNextFalloutNewVegas.audiohaptics failed bank validation");
        break;
    case Override::wrong_game:
        log("InputNextFalloutNewVegas.audiohaptics targets a different game");
        break;
    case Override::io_error:
        log("could not read InputNextFalloutNewVegas.audiohaptics");
        break;
    case Override::not_found:
        break;
    }
    if (audio.loaded_profiles != 0U) {
        log(std::format("{} audio haptic profiles available", audio.loaded_profiles));
    }
    return true;
}
} // namespace

NativeState& NativeState::instance() noexcept {
    // xNVSE keeps callbacks for the process lifetime. DLL detach must never join the worker.
    alignas(NativeState) static std::array<std::byte, sizeof(NativeState)> storage;
    static auto* const state = new (storage.data()) NativeState{};
    return *state;
}

bool NativeState::load(const NVSEInterface& nvse) noexcept {
    const auto handle = nvse.GetPluginHandle();
    if (handle == static_cast<PluginHandle>(kPluginHandle_Invalid)) {
        return false;
    }
    const auto* messaging =
        static_cast<NVSEMessagingInterface*>(nvse.QueryInterface(kInterface_Messaging));
    if (!messaging || messaging->version < NVSEMessagingInterface::kVersion ||
        !messaging->RegisterListener) {
        return false;
    }
    nvse_ = &nvse;
    return messaging->RegisterListener(handle, "NVSE", &NativeState::message_callback);
}

void NativeState::message_callback(NVSEMessagingInterface::Message* message) {
    NativeState::instance().receive_message(message);
}

void NativeState::receive_message(NVSEMessagingInterface::Message* message) noexcept {
    if (!message || stopping_.load(std::memory_order_acquire)) {
        return;
    }
    switch (message->type) {
    case NVSEMessagingInterface::kMessage_PostPostLoad:
        try {
            initialize_after_plugin_load();
        } catch (...) {
            log("initialization stopped after an exception while reading settings or starting "
                "output");
        }
        break;
    case NVSEMessagingInterface::kMessage_DeferredInit:
        if (initialized_.load(std::memory_order_acquire) && !dry_fire_hook_attempted_) {
            dry_fire_hook_attempted_ = true;
            install_dry_fire_hook();
        }
        break;
    case NVSEMessagingInterface::kMessage_PreLoadGame:
    case NVSEMessagingInterface::kMessage_ExitToMainMenu:
        invalidation_pending_.store(true, std::memory_order_release);
        break;
    case NVSEMessagingInterface::kMessage_NewGame:
    case NVSEMessagingInterface::kMessage_PostLoadGame:
        invalidation_pending_.store(true, std::memory_order_release);
        if (initialized_.load(std::memory_order_acquire) && events_) {
            event_sources_.shots = register_shot_events(true);
        }
        break;
    case NVSEMessagingInterface::kMessage_ExitGame:
    case NVSEMessagingInterface::kMessage_ExitGame_Console:
        if (!stopping_.exchange(true, std::memory_order_acq_rel) && worker_) {
            worker_->request_stop();
        }
        break;
    case NVSEMessagingInterface::kMessage_MainGameLoop:
        on_main_loop();
        break;
    default:
        break;
    }
}

void NativeState::initialize_after_plugin_load() {
    if (initialized_.load(std::memory_order_acquire)) {
        return;
    }
    const char* runtime_directory = nvse_->GetRuntimeDirectory();
    if (!runtime_directory || *runtime_directory == '\0') {
        log("initialization skipped because xNVSE returned no game directory");
        return;
    }
    const std::filesystem::path game_directory{runtime_directory};
    const auto plugin_directory = game_directory / L"Data" / L"NVSE" / L"Plugins";
    initialize_diagnostics(plugin_directory);
    log("xNVSE load accepted for FalloutNV.exe 1.4.0.525");
    if (!load_settings(plugin_directory, settings_)) {
        return;
    }

    const auto connection = output_.start(game_directory / L"inputnext_runtime.dll");
    if (!connection) {
        log("initialization skipped because inputnext_runtime.dll failed ABI validation");
        return;
    }

    if (settings_.led.enabled && settings_.led.stealth_enabled) {
        log(stealth_detection_.bind(native_stealth_reader())
                ? "stealth lightbar uses FalloutNV player detection"
                : "stealth lightbar unavailable: FalloutNV detection entry point is not mapped");
    }

    events_ =
        static_cast<NVSEEventManagerInterface*>(nvse_->QueryInterface(kInterface_EventManager));
    if (!events_ || !events_->SetNativeEventHandler || !events_->RemoveNativeEventHandler) {
        events_ = nullptr;
        log("native events unavailable because xNVSE EventManager is incomplete");
    }

    worker_ = std::make_unique<fallout_gamebryo::EffectWorker>(output_,
        settings_,
        fallout_gamebryo::WorkerCallbacks{.foreground = &NativeState::foreground,
            .delivery_changed = &NativeState::delivery_changed,
            .cleanup_incomplete = &NativeState::cleanup_incomplete});
    if (!worker_->start()) {
        log("initialization skipped because the output worker could not start");
        worker_.reset();
        return;
    }

    translator_.reset(epoch_);
    if (events_) {
        event_sources_ = register_events();
    }
    initialized_.store(true, std::memory_order_release);
    log(event_sources_.complete()
            ? "gameplay observation active with all native event sources"
            : "gameplay observation active with limited native event sources");
}

bool NativeState::foreground() noexcept {
    HWND foreground_window = GetForegroundWindow();
    if (!foreground_window) {
        return false;
    }
    DWORD process_id{};
    GetWindowThreadProcessId(foreground_window, &process_id);
    return process_id == GetCurrentProcessId();
}

void NativeState::on_main_loop() noexcept {
    const auto current_thread = GetCurrentThreadId();
    DWORD no_thread{};
    game_thread_id_.compare_exchange_strong(no_thread,
        current_thread,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
    if (game_thread_id_.load(std::memory_order_acquire) != current_thread) {
        return;
    }
    if (!initialized_.load(std::memory_order_acquire) ||
        stopping_.load(std::memory_order_acquire) || !worker_) {
        return;
    }
    const auto sampled_at = steady_clock::now();
    if (invalidation_pending_.exchange(false, std::memory_order_acq_rel)) {
        invalidate(sampled_at);
    }
    ++frame_;
    translator_.advance(sampled_at);
    auto sample = sample_game(ObservationFacts{.at = sampled_at,
        .epoch = epoch_,
        .frame = frame_,
        .attack_trigger = settings_.attack_trigger,
        .aim_trigger = settings_.aim_trigger,
        .focused = foreground()});
    auto& observation = sample.observation;
    observation.stealth = stealth_detection_.sample(observation, sample.sneaking);
    if (should_cancel_pending_shots(observation, last_observation_)) {
        translator_.cancel_pending_shots();
    }
    landing_trigger_readiness_.apply(observation,
        last_observation_,
        sample.animation == PlayerAnimation::special_idle);

    observation.at = steady_clock::now();
    if (settings_.diagnostics) {
        log_stealth_change(observation, last_observation_);
    }
    if (settings_.diagnostics && sample.raw_reaction &&
        (observation.mode == Mode::gameplay || observation.mode == Mode::vats_playback) &&
        (!last_observation_ || last_observation_->mode != observation.mode ||
            last_observation_->reaction != observation.reaction)) {
        log_player_reaction(*sample.raw_reaction, observation);
    }
    last_observation_ = observation;
    if (const auto signal = observe_action(sample)) {
        enqueue(*signal, observation.at);
    }
    if (observation.epoch == epoch_) {
        worker_->publish(observation, std::span{event_buffer_}.first(event_count_));
        event_count_ = 0;
    }
}

std::optional<NativeSignal> NativeState::observe_action(const GameSample& sample) noexcept {
    const auto previous_vats = std::exchange(last_vats_selection_, sample.vats_selection);
    const auto previous_animation = std::exchange(last_animation_, sample.animation);
    const auto& observation = sample.observation;
    const bool melee_active = observation.mode == Mode::gameplay &&
                              sample.animation == PlayerAnimation::melee_attack &&
                              (observation.weapon.kind == WeaponClass::unarmed ||
                                  observation.weapon.kind == WeaponClass::melee);
    const bool was_melee_active = std::exchange(in_melee_attack_, melee_active);
    if (sample.vats_selection && sample.vats_selection != previous_vats) {
        return NativeSignal{.kind = NativeSignalKind::vats_select, .source = observation.player};
    }
    if (previous_vats && observation.mode == Mode::vats_playback) {
        return NativeSignal{.kind = NativeSignalKind::vats_accept, .source = observation.player};
    }
    if (melee_active && !was_melee_active) {
        return NativeSignal{.kind = NativeSignalKind::melee_swing,
            .source = observation.player,
            .form = observation.weapon.id,
            .weapon_class = observation.weapon.kind};
    }
    if (observation.mode == Mode::gameplay && observation.weapon.kind == WeaponClass::thrown &&
        sample.animation == PlayerAnimation::throw_release &&
        previous_animation != PlayerAnimation::throw_release) {
        return NativeSignal{.kind = NativeSignalKind::thrown,
            .source = observation.player,
            .form = observation.weapon.id,
            .weapon_class = observation.weapon.kind};
    }
    return std::nullopt;
}

void NativeState::enqueue(NativeSignal signal, std::optional<MonoTime> event_time) noexcept {
    if (!accepting_events() || !last_observation_ || !last_observation_->focused ||
        !last_observation_->alive) {
        return;
    }
    signal.at = event_time ? *event_time : steady_clock::now();
    signal.epoch = epoch_;
    signal.frame = frame_;
    if (signal.mode == Mode::unavailable) {
        signal.mode = last_observation_->mode;
        signal.menu = last_observation_->menu;
    }
    const auto event = translator_.translate(signal, form_id(current_player()));
    if (!event) {
        return;
    }
    if (event_count_ == event_buffer_.size()) {
        invalidate(signal.at);
        log("native event inbox overflow; resetting the active effects session");
        return;
    }
    event_buffer_[event_count_++] = *event;
    if (settings_.diagnostics) {
        log_native_event(event->kind);
    }
}

bool NativeState::accepting_events() const noexcept {
    const auto thread = game_thread_id_.load(std::memory_order_acquire);
    return thread != 0 && thread == GetCurrentThreadId() &&
           initialized_.load(std::memory_order_acquire) &&
           !stopping_.load(std::memory_order_acquire) &&
           !invalidation_pending_.load(std::memory_order_acquire);
}

void NativeState::invalidate(MonoTime now) noexcept {
    ++epoch_;
    translator_.reset(epoch_);
    last_observation_.reset();
    last_vats_selection_.reset();
    last_animation_ = PlayerAnimation::other;
    in_melee_attack_ = false;
    event_count_ = 0;
    if (worker_) {
        worker_->invalidate(now);
    }
}

void NativeState::delivery_changed(inputnext::adapters::DeliveryDiagnostics diagnostics) noexcept {
    if (instance().settings_.diagnostics) {
        try {
            const auto* route =
                diagnostics.accepted_body_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD ? "hd"
                                                                                        : "classic";
            log(std::format("controller output status={} body_owner={} body_route={} fallback={}",
                diagnostics.last_status,
                diagnostics.body_owner_present,
                route,
                diagnostics.body_fallback_reason));
            return;
        } catch (...) {
            log("controller output diagnostic formatting failed");
        }
    }
    log(diagnostics.body_owner_present ? "controller effect output active"
                                       : "controller effect output status changed");
}

void NativeState::cleanup_incomplete() noexcept {
    log("output context cleanup remains pending at game exit");
}
} // namespace fallout_new_vegas
