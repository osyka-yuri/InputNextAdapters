#pragma once

#include "native/observation.hpp"
#include "native/observation_requests.hpp"

#include <fallout4_adapter/session.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fallout4_adapter::native {
// NativeOwnerPipeline protects mailbox state with inbox_mutex_.
// event_overflow_count() also supports concurrent reads without that lock.
class OwnerMailbox final {
public:
    static constexpr std::size_t control_capacity = 16;
    static constexpr std::size_t event_capacity = 64;

    struct ControlNotice {
        Control control{};
        MonoTime arrived_at;
    };

    struct PendingEvent {
        std::uint64_t epoch{};
        std::uint64_t ui_generation{};
        PolicyEvent event{};
    };

    struct PendingSample {
        Ticket ticket{};
        NativeObservation observation{};
    };

    struct PendingBatch {
        std::array<ControlNotice, control_capacity> controls{};
        std::size_t control_count{};
        std::array<PendingEvent, event_capacity> events{};
        std::size_t event_count{};
        std::optional<PendingSample> sample;
        bool overflow{};
    };

    struct CachedPlayer {
        std::uint64_t epoch{};
        std::uint64_t ui_generation{};
        std::uint32_t handle{};
        std::uintptr_t address{};
        std::uint32_t form_id{};
        std::uint64_t weapon_identity{};
        bool power_armor{};
        bool sprinting{};
    };

    // Caller holds inbox_mutex_. A full control queue records overflow and drops this notice.
    void enqueue_control(Control control, MonoTime now) noexcept;

    // Caller holds inbox_mutex_. Event admission is checked before the bounded FIFO appends.
    template <class IsInert, class AnimationSourceAccepted>
    [[nodiscard]] bool enqueue_event(std::uint64_t epoch,
        std::uint64_t ui_generation,
        PolicyEvent event,
        std::uint64_t current_generation,
        bool has_animation_source,
        const IsInert& is_inert,
        const AnimationSourceAccepted& animation_source_accepted) noexcept {
        if (is_inert() ||
            !ui_provenance_matches(epoch,
                event_epoch_,
                ui_generation,
                event_ui_generation_,
                current_generation) ||
            !ui_provenance_matches(epoch,
                cached_player_.epoch,
                ui_generation,
                cached_player_.ui_generation,
                current_generation) ||
            (has_animation_source && !animation_source_accepted())) {
            return false;
        }

        if (event_count_ == events_.size()) {
            event_head_ = (event_head_ + 1U) % events_.size();
            --event_count_;
            event_overflows_.fetch_add(1, std::memory_order_relaxed);
        }
        const auto index = (event_head_ + event_count_) % events_.size();
        events_[index] =
            PendingEvent{.epoch = epoch, .ui_generation = ui_generation, .event = event};
        ++event_count_;
        return true;
    }

    // Construct output before taking inbox_mutex_. Draining under that lock preserves FIFO order
    // and clears the drained queues and sample.
    void take_pending_batch(PendingBatch& output) noexcept;

    [[nodiscard]] std::uint64_t event_overflow_count() const noexcept {
        return event_overflows_.load(std::memory_order_relaxed);
    }

    // Caller holds inbox_mutex_.
    [[nodiscard]] std::optional<CachedPlayer> callback_player(
        std::uint64_t current_generation) const noexcept;
    void publish_task_provenance(const std::optional<Ticket>& ticket,
        std::uint64_t ui_generation) noexcept;
    template <class IsStopping, class IsInert>
    [[nodiscard]] bool observation_request_matches_controls(const PendingObservation& observation,
        std::uint64_t current_generation,
        const IsStopping& is_stopping,
        const IsInert& is_inert) const noexcept;
    template <class CurrentGeneration, class IsFocused>
    [[nodiscard]] bool callback_admission_is_current(const PendingObservation& observation,
        const CurrentGeneration& current_generation,
        const IsFocused& is_focused) const noexcept;

    // Each invalidation is explicit because focus, lifecycle, generation, and inert transitions
    // intentionally discard different portions of the mailbox state.
    void clear_task_provenance() noexcept;
    void clear_event_admission() noexcept;
    void discard_generation_data() noexcept;
    void discard_inert_publication() noexcept;
    void reject_sample(const PendingSample& sample) noexcept;
    void publish_observation(const PendingObservation& request,
        const NativeObservation& observation) noexcept;
    void retract_observation(const PendingObservation& request) noexcept;

    [[nodiscard]] bool has_sample() const noexcept { return sample_.has_value(); }

    [[nodiscard]] bool has_cached_player() const noexcept { return cached_player_.address != 0; }

private:
    std::array<ControlNotice, control_capacity> controls_{};
    std::size_t control_head_{}, control_count_{};
    std::array<PendingEvent, event_capacity> events_{};
    std::size_t event_head_{}, event_count_{};
    std::optional<PendingSample> sample_;
    CachedPlayer cached_player_{};
    std::uint64_t task_epoch_{}, task_ui_generation_{}, event_epoch_{}, event_ui_generation_{};
    bool control_overflow_{};
    std::atomic<std::uint64_t> event_overflows_;
};

template <class IsStopping, class IsInert>
bool OwnerMailbox::observation_request_matches_controls(const PendingObservation& observation,
    std::uint64_t current_generation,
    const IsStopping& is_stopping,
    const IsInert& is_inert) const noexcept {
    if (is_stopping() || is_inert() || control_count_ != 0 || control_overflow_ ||
        !ui_generation_matches(observation.ui_generation, current_generation) ||
        observation.ui_generation != task_ui_generation_) {
        return false;
    }
    if (observation.lifecycle_epoch == 0) {
        return task_epoch_ == 0;
    }
    return ui_provenance_matches(observation.lifecycle_epoch,
        task_epoch_,
        observation.ui_generation,
        task_ui_generation_,
        current_generation);
}

template <class CurrentGeneration, class IsFocused>
bool OwnerMailbox::callback_admission_is_current(const PendingObservation& observation,
    const CurrentGeneration& current_generation,
    const IsFocused& is_focused) const noexcept {
    return control_count_ == 0 && !control_overflow_ &&
           ui_provenance_matches(observation.lifecycle_epoch,
               task_epoch_,
               observation.ui_generation,
               task_ui_generation_,
               current_generation()) &&
           is_focused() && ui_generation_matches(observation.ui_generation, current_generation());
}
} // namespace fallout4_adapter::native
