#pragma once

#include "event_reducer.hpp"
#include "native_game.hpp"
#include "native_observation.hpp"
#include "nvse_sdk.hpp"
#include "stealth_detection.hpp"

#include <fallout_gamebryo/settings.hpp>
#include <fallout_gamebryo/worker.hpp>
#include <inputnext_adapters/output.hpp>

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace fallout_new_vegas {
class NativeState final {
public:
    static NativeState& instance() noexcept;

    [[nodiscard]] bool load(const NVSEInterface& nvse) noexcept;
    void on_dry_fire() noexcept;

private:
    struct ShotEvents {
        bool fire{};
        bool projectile{};

        [[nodiscard]] bool available() const noexcept { return fire && projectile; }
    };

    struct EventSources {
        ShotEvents shots;
        bool misc_stats{};
        bool hit{};
        bool equip{};
        bool lockpick_break{};
        bool terminal_success{};
        bool terminal_failure{};

        [[nodiscard]] bool complete() const noexcept {
            return shots.available() && misc_stats && hit && equip && lockpick_break &&
                   terminal_success && terminal_failure;
        }
    };

    NativeState() noexcept
        : game_thread_id_(0),
          invalidation_pending_(false),
          initialized_(false),
          stopping_(false) {}

    void receive_message(NVSEMessagingInterface::Message* message) noexcept;
    void initialize_after_plugin_load();
    void on_main_loop() noexcept;
    [[nodiscard]] std::optional<NativeSignal> observe_action(const GameSample& sample) noexcept;
    void enqueue(NativeSignal signal,
        std::optional<fallout_gamebryo::MonoTime> event_time = std::nullopt) noexcept;
    void enqueue_terminal_outcome(NativeSignalKind kind, void* parameters) noexcept;
    [[nodiscard]] bool accepting_events() const noexcept;
    void invalidate(fallout_gamebryo::MonoTime now) noexcept;
    [[nodiscard]] EventSources register_events() noexcept;
    [[nodiscard]] ShotEvents register_shot_events(bool rebind) noexcept;
    [[nodiscard]] static bool foreground() noexcept;
    static void message_callback(NVSEMessagingInterface::Message* message);
    static void showoff_fire(TESObjectREFR* actor, void* parameters);
    static void showoff_projectile_create(TESObjectREFR* projectile, void* parameters);
    static void game_hit(TESObjectREFR* object, void* parameters);
    static void showoff_misc_stat_change(TESObjectREFR* actor, void* parameters);
    static void game_equip(TESObjectREFR* object, void* parameters);
    static void game_lockpick_broken(TESObjectREFR* object, void* parameters);
    static void game_terminal_hacked(TESObjectREFR* object, void* parameters);
    static void game_terminal_hack_failed(TESObjectREFR* object, void* parameters);
    static void delivery_changed(inputnext::adapters::DeliveryDiagnostics diagnostics) noexcept;
    static void cleanup_incomplete() noexcept;

    const NVSEInterface* nvse_{};
    NVSEEventManagerInterface* events_{};
    fallout_gamebryo::Settings settings_;
    inputnext::adapters::RuntimeOutput output_;
    std::unique_ptr<fallout_gamebryo::EffectWorker> worker_;
    EventSources event_sources_;
    SignalTranslator translator_;
    LandingTriggerReadiness landing_trigger_readiness_;
    StealthDetection stealth_detection_;
    // Only game-thread callbacks write this batch. The worker copies it on publish.
    std::array<fallout_gamebryo::Event, fallout_gamebryo::event_capacity> event_buffer_{};
    std::size_t event_count_{};
    std::optional<fallout_gamebryo::Observation> last_observation_;
    std::uint64_t epoch_{1};
    std::uint64_t frame_{};
    std::optional<VatsSelection> last_vats_selection_;
    PlayerAnimation last_animation_{PlayerAnimation::other};
    bool in_melee_attack_{};
    bool dry_fire_hook_attempted_{};
    std::atomic<DWORD> game_thread_id_;
    std::atomic<bool> invalidation_pending_;
    std::atomic<bool> initialized_;
    std::atomic<bool> stopping_;
};
} // namespace fallout_new_vegas
