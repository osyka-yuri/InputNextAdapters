#include <skyrim_adapter/coordinator.hpp>

#include <skyrim_adapter/body_waveform.hpp>
#include <skyrim_adapter/model.hpp>

#include "observation.hpp"

#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>

namespace skyrim_adapter {
using coordinator_detail::fresh;
using coordinator_detail::hard_observation_valid;

void Coordinator::tick() noexcept {
    run_tick({}, true);
}

void Coordinator::tick(MonoTime now) noexcept {
    run_tick(now, false);
}

void Coordinator::run_tick(MonoTime supplied, bool use_steady_clock) noexcept {
    std::unique_lock delivery_lock{delivery_gate_};
    active_context_.reset();

    struct DeliveryExit {
        Coordinator& coordinator;
        DeliveryExit(const DeliveryExit&) = delete;
        DeliveryExit& operator=(const DeliveryExit&) = delete;
        DeliveryExit(DeliveryExit&&) = delete;
        DeliveryExit& operator=(DeliveryExit&&) = delete;

        explicit DeliveryExit(Coordinator& value) noexcept : coordinator{value} {}

        ~DeliveryExit() {
            coordinator.active_context_.reset();
            coordinator.publish_delivery_diagnostics();
        }
    } const delivery_exit{*this};

    // Production time is sampled only after acquiring the delivery mutex. Explicit-time ticks
    // keep their caller-supplied value for every freshness and lease check.
    inputnext::adapters::TickClock clock{.supplied = supplied,
        .use_steady_clock = use_steady_clock};
    tick_locked(clock);
}

bool Coordinator::close_and_drain() noexcept {
    {
        std::scoped_lock lock(ingress_gate_);
        if (!closing_) {
            closing_ = true;
            ++epoch_;
            discard_actions();
            stage_delivery_reset_locked(DeliveryResetKind::hard);
            observation_.reset();
            binding_started_at_.reset();
            ++observation_revision_;
            ++hard_fence_revision_;
            suspended_ = true;
            gameplay_resume_pending_ = false;
            withdrawal_pending_ = false;
            body_withdrawal_pending_ = false;
        }
    }

    std::unique_lock delivery_lock{delivery_gate_};
    active_context_.reset();
    inputnext::adapters::TickClock clock{.supplied = {}, .use_steady_clock = true};
    const bool drained = output_session_.close_and_drain(clock);
    publish_delivery_diagnostics();
    return drained;
}

void Coordinator::reset_delivery_history([[maybe_unused]] std::uint64_t burst_revision) noexcept {
    bool hard_reset = true;
    std::optional<MonoTime> menu_pause_at;
    std::uint64_t reset_revision{};
    {
        std::scoped_lock lock(ingress_gate_);
        reset_revision = burst_revision_;
        const bool hard_reset_ready =
            pending_hard_reset_revision_ && *pending_hard_reset_revision_ <= reset_revision;
        const bool menu_pause_ready =
            pending_menu_pause_revision_ && *pending_menu_pause_revision_ <= reset_revision;
        const bool menu_reset_ready =
            (pending_menu_reset_revision_ && *pending_menu_reset_revision_ <= reset_revision) ||
            menu_pause_ready;
        if (hard_reset_ready) {
            pending_hard_reset_revision_.reset();
        }
        if (menu_reset_ready) {
            hard_reset = hard_reset_ready;
            pending_menu_reset_revision_.reset();
        }
        if (menu_pause_ready) {
            menu_pause_at = pending_menu_pause_at_;
            pending_menu_pause_revision_.reset();
            pending_menu_pause_at_.reset();
        }
        // Unknown burst causes keep the legacy full-reset behavior.
        if (!hard_reset_ready && !menu_reset_ready) {
            hard_reset = true;
        }
    }
    if (hard_reset) {
        policy_.reset();
    } else {
        policy_.reset_for_menu(menu_pause_at);
    }
    output_session_.reset_attempt_history();
    processed_burst_revision_ = reset_revision;
}

bool Coordinator::live_admitted_locked(MonoTime now) const noexcept {
    return !closing_ && !diagnostics_.faulted && data_loaded_ && load_ready_ && focused_ &&
           observation_ && fresh(observation_->sampled_at, now) &&
           hard_observation_valid(*observation_);
}

bool Coordinator::maintain_live_admission(MonoTime now) noexcept {
    bool current_admission{};
    bool reset_history{};
    std::uint64_t burst_snapshot{};
    {
        std::scoped_lock lock(ingress_gate_);
        current_admission = live_admitted_locked(now);
        if (!current_admission) {
            if (!suspended_) {
                invalidate_locked(false);
                stage_delivery_reset_locked(DeliveryResetKind::hard);
                reset_history = true;
            } else {
                request_withdrawal_locked(true);
            }
        }
        burst_snapshot = burst_revision_;
    }
    if (reset_history) {
        reset_delivery_history(burst_snapshot);
    }
    return current_admission;
}

bool Coordinator::process_withdrawal_phase(inputnext::adapters::TickClock& clock) noexcept {
    const bool initially_live = maintain_live_admission(clock.now());
    std::uint64_t revision{};
    bool pending{};
    bool body_required{};
    {
        std::scoped_lock lock(ingress_gate_);
        revision = withdrawal_revision_;
        pending = withdrawal_pending_;
        body_required = body_withdrawal_pending_;
    }
    if (!pending) {
        return initially_live;
    }

    const bool body_cleared = !body_required || output_session_.clear_body(clock);
    static_cast<void>(maintain_live_admission(clock.now()));

    const bool triggers_cleared = output_session_.withdraw_triggers();
    static_cast<void>(maintain_live_admission(clock.now()));
    const bool leds_cleared = output_session_.withdraw_leds();
    static_cast<void>(maintain_live_admission(clock.now()));
    if (!body_cleared || !triggers_cleared || !leds_cleared) {
        return false;
    }

    const bool live_after_cleanup = maintain_live_admission(clock.now());

    bool still_pending{};
    {
        std::scoped_lock lock(ingress_gate_);
        if (withdrawal_revision_ == revision) {
            withdrawal_pending_ = false;
            if (body_required) {
                body_withdrawal_pending_ = false;
            }
        }
        still_pending = withdrawal_pending_;
    }
    return live_after_cleanup && !still_pending;
}

bool Coordinator::capture_delivery_batch(MonoTime now,
    DeliveryContext& context,
    std::array<Action, inbox_capacity>& actions,
    std::size_t& action_count) noexcept {
    std::scoped_lock lock(ingress_gate_);
    if (!live_admitted_locked(now) || withdrawal_pending_ || body_withdrawal_pending_ ||
        !observation_) {
        return false;
    }
    const Observation observation = *observation_;
    const bool playable =
        menus_clear_ && observation.menus_clear &&
        (!gameplay_resume_pending_ || observation.sampled_at > gameplay_resumed_at_);
    if (!playable && !suspended_) {
        invalidate_locked(true);
        if (!menus_clear_ || !observation.menus_clear) {
            stage_menu_pause_locked(observation.sampled_at);
        } else {
            stage_delivery_reset_locked(DeliveryResetKind::menu);
        }
        return false;
    }
    context = DeliveryContext{
        .epoch = epoch_,
        .burst_revision = burst_revision_,
        .hard_fence_revision = hard_fence_revision_,
        .observation_revision = observation_revision_,
        .observation = observation,
        .playable = playable,
    };
    action_count = pending_count_;
    std::copy_n(pending_.begin(), action_count, actions.begin());
    pending_count_ = 0;
    active_context_ = context;
    if (playable && epoch_ == context.epoch && burst_revision_ == context.burst_revision) {
        gameplay_resume_pending_ = false;
        suspended_ = false;
    }
    return true;
}

void Coordinator::tick_locked(inputnext::adapters::TickClock& clock) noexcept {
    {
        std::scoped_lock lock(ingress_gate_);
        if (closing_) {
            return;
        }
    }
    std::uint64_t burst_snapshot{};
    bool reset_history{};
    {
        std::scoped_lock lock(ingress_gate_);
        burst_snapshot = burst_revision_;
        reset_history =
            burst_snapshot != processed_burst_revision_ ||
            (pending_hard_reset_revision_ && *pending_hard_reset_revision_ <= burst_snapshot) ||
            (pending_menu_reset_revision_ && *pending_menu_reset_revision_ <= burst_snapshot) ||
            (pending_menu_pause_revision_ && *pending_menu_pause_revision_ <= burst_snapshot);
    }
    if (reset_history) {
        reset_delivery_history(burst_snapshot);
    }

    if (!process_withdrawal_phase(clock)) {
        output_session_.maintain_cleanup();
        return;
    }

    std::optional<MonoTime> observation_at;
    {
        std::scoped_lock lock(ingress_gate_);
        if (observation_) {
            observation_at = observation_->sampled_at;
        }
    }
    if (!output_session_.reconcile(observation_at, clock)) {
        return;
    }

    // Reconcile may block in the runtime and may publish binding fences. Process requests that
    // arrived during it once, then take one bounded ingress batch for this delivery attempt.
    if (!process_withdrawal_phase(clock)) {
        return;
    }

    DeliveryContext context{};
    std::array<Action, inbox_capacity> actions{};
    std::size_t action_count{};
    if (!capture_delivery_batch(clock.now(), context, actions, action_count)) {
        return;
    }
    if (context.burst_revision != processed_burst_revision_) {
        reset_delivery_history(context.burst_revision);
    }

    auto now = clock.now();
    EffectFrame frame{};
    if (context.playable) {
        frame = policy_.step(context.observation,
            std::span<const Action>{actions.data(), action_count},
            now);
    }
    inputnext::adapters::OutputFrame output_frame{};
    output_frame.left = frame.left;
    output_frame.right = frame.right;
    output_frame.rumble = frame.rumble;
    output_frame.body = make_body_program(frame.body);
    output_frame.leds = frame.leds;
    now = clock.now();
    static_cast<void>(output_session_.send(output_frame, now, clock));
}

void Coordinator::publish_delivery_diagnostics() noexcept {
    const auto output = output_session_.diagnostics();
    std::scoped_lock lock(ingress_gate_);
    diagnostics_.last_status = output.last_status;
    diagnostics_.body_owner_present = output.body_owner_present;
    diagnostics_.accepted_body_route = output.accepted_body_route;
    diagnostics_.body_fallback_reason = output.body_fallback_reason;
}

} // namespace skyrim_adapter
