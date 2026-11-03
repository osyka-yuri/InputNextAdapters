#pragma once

#include "native/animation_subscription.hpp"
#include "native/bindings.hpp"
#include "native/observation.hpp"
#include "native/observation_requests.hpp"
#include "native/owner_mailbox.hpp"
#include "native/owner_services.hpp"

#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/session.hpp>

#include <RE/B/BSAnimationGraphEvent.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/T/TESHitEvent.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace fallout4_adapter::native {
enum class OwnerPhaseAdmission : std::uint8_t;
enum class SinkInstall : std::uint8_t { pending, ready, failed };
struct NativeOwnerTestAccess;

class NativeOwnerPipeline final {
public:
    using CachedPlayer = OwnerMailbox::CachedPlayer;

    NativeOwnerPipeline(NativeOwnerServices& services,
        Config config,
        RE::BSTEventSink<RE::BSAnimationGraphEvent>* anim_sink,
        RE::BSTEventSink<WeaponFiredEvent>* weapon_sink,
        RE::BSTEventSink<RE::TESHitEvent>* hit_sink,
        RE::BSTEventSink<RE::MenuOpenCloseEvent>* menu_sink,
        std::uint64_t initial_request_id = 0,
        std::mutex* custom_inbox_mutex = nullptr) noexcept;

    void queue_control(Control control, MonoTime now) noexcept;
    [[nodiscard]] std::optional<CachedPlayer> callback_player() const noexcept;

    [[nodiscard]] bool accepts_animation(
        const AnimationSubscription::Source* source) const noexcept {
        return animation_subscription_.accepts(source);
    }

    [[nodiscard]] bool queue_event(std::uint64_t epoch,
        std::uint64_t ui_generation,
        PolicyEvent event,
        const AnimationSubscription::Source* animation_source = nullptr) noexcept;

    [[nodiscard]] bool run_worker_iteration(MonoTime now,
        bool is_focused,
        MonoTime& next_sample,
        bool& event_overflow_reported) noexcept;
    void finish_worker() noexcept;
    void consume_main_observation() noexcept;
    void enter_inert(const char* reason) noexcept;
    void request_stop() noexcept;
    void wait_for_work(MonoTime deadline) noexcept;

    [[nodiscard]] bool stopping() const noexcept {
        return stopping_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool inert() const noexcept { return inert_.load(std::memory_order_acquire); }

    void set_data_ready(bool ready = true) noexcept {
        data_ready_notice_.store(ready, std::memory_order_release);
    }

    void set_install_pending(bool pending = true) noexcept {
        install_pending_.store(pending, std::memory_order_release);
    }

private:
    // Component tests step individual phases through their dedicated access adapter.
    friend struct NativeOwnerTestAccess;

    using PendingBatch = OwnerMailbox::PendingBatch;
    using PendingSample = OwnerMailbox::PendingSample;

    void request_animation_refresh() noexcept;
    void report_event_overflow_once(bool& reported) noexcept;
    void report_output_admission(MonoTime now, bool is_focused, bool has_ticket) noexcept;
    void publish_task_provenance(const std::optional<Ticket>& ticket) noexcept;
    void publish_task_provenance(Ticket ticket, std::uint64_t ui_generation) noexcept;
    void request_observation_if_due(MonoTime now,
        bool is_focused,
        const std::optional<Ticket>& ticket,
        MonoTime& next_sample) noexcept;
    [[nodiscard]] bool reconcile_worker_generation(MonoTime now) noexcept;
    [[nodiscard]] bool apply_focus_change(MonoTime now, bool is_focused) noexcept;
    [[nodiscard]] bool pre_tick_generation_is_current(MonoTime now) noexcept;
    void process_pending() noexcept;

    [[nodiscard]] bool publish_observation_request(std::uint64_t epoch,
        std::uint64_t ui_generation) noexcept;
    void run_main_observation(const PendingObservation& observation);
    [[nodiscard]] bool prepare_main_observation(const PendingObservation& observation,
        std::uint64_t refresh_generation);
    void detach_animation_if_admitted(const PendingObservation& observation,
        std::uint64_t refresh_generation) noexcept;
    [[nodiscard]] bool callback_admission_is_current_locked(
        const PendingObservation& observation) const noexcept;
    void publish_main_observation(const PendingObservation& observation,
        std::uint64_t refresh_generation,
        const NativeObservation& native_observation);
    void complete_observation(std::uint64_t request_id) noexcept;
    [[nodiscard]] OwnerPhaseAdmission check_observation_group(
        const PendingObservation& observation) noexcept;
    void report_animation_subscription() noexcept;
    [[nodiscard]] SinkInstall install_sinks();
    void wake() noexcept;

    [[nodiscard]] Session& session() noexcept { return session_; }

    [[nodiscard]] std::uint64_t request_id() const noexcept;
    [[nodiscard]] bool request_pending() const noexcept;
    [[nodiscard]] bool request_pending_hint() const noexcept;
    [[nodiscard]] bool request_executing() const noexcept;
    [[nodiscard]] bool has_sample() const noexcept;
    [[nodiscard]] bool has_cached_player() const noexcept;

    [[nodiscard]] std::uint64_t worker_ui_generation() const noexcept {
        return worker_ui_generation_;
    }

    [[nodiscard]] PendingBatch take_pending_batch() noexcept;
    void apply_pending_controls(const PendingBatch& batch) noexcept;
    void process_pending_sample(const std::optional<PendingSample>& sample,
        const std::optional<Ticket>& ticket,
        MonoTime validation_now,
        std::uint64_t current_ui_generation) noexcept;
    void process_pending_events(const PendingBatch& batch,
        const std::optional<Ticket>& ticket,
        MonoTime validation_now) noexcept;

    NativeOwnerServices& services_;
    Session session_;
    const bool diagnostics_enabled_{};
    const bool step_events_enabled_{};
    const bool radiation_enabled_{};
    bool radiation_registered_{};
    AnimationSubscription animation_subscription_;

    RE::BSTEventSink<WeaponFiredEvent>* weapon_sink_{nullptr};
    RE::BSTEventSink<RE::TESHitEvent>* hit_sink_{nullptr};
    RE::BSTEventSink<RE::MenuOpenCloseEvent>* menu_sink_{nullptr};

    mutable std::mutex default_inbox_mutex_;
    std::mutex& inbox_mutex_;
    OwnerMailbox mailbox_;
    ObservationRequests observation_requests_;
    std::uint64_t worker_ui_generation_{};
    std::optional<Mode> last_observed_mode_;
    MonoTime last_observation_at_;
    std::optional<bool> last_reported_admission_;
    std::optional<bool> last_reported_animation_subscription_;

    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
    std::atomic<bool> stopping_;
    std::atomic<bool> inert_;
    std::atomic<bool> focused_;
    std::atomic<bool> data_ready_notice_;
    std::atomic<bool> install_pending_;
    std::atomic<std::uint64_t> animation_refresh_generation_;
    std::atomic<std::uint64_t> animation_refresh_completed_;

    bool weapon_fired_registered_{};
    bool hit_registered_{};
    bool menu_registered_{};
    bool hit_missing_logged_{};
    bool menu_missing_logged_{};
};
} // namespace fallout4_adapter::native
