#include <fallout4_adapter/session.hpp>

#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/model.hpp>

#include <inputnext_adapters/output.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <input_next_core.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <span>

namespace fallout4_adapter {
namespace {
constexpr auto maximum_age = std::chrono::milliseconds{150};
constexpr auto body_rebound_led_grace = std::chrono::milliseconds{200};

bool fresh(MonoTime sampled_at, MonoTime now) noexcept {
    return sampled_at <= now && now - sampled_at <= maximum_age;
}

const Config& default_session_config() noexcept {
    static const Config k_default{};
    return k_default;
}
} // namespace

Session::Session(inputnext::adapters::OutputPort& output) noexcept
    : Session(output, default_session_config()) {
}

Session::Session(inputnext::adapters::OutputPort& output, const Config& config) noexcept
    : policy_(config.policy),
      output_(output, *this, config.slot),
      enabled_(config.policy.enabled) {
    diagnostics_.faulted = config.slot > 3U;
}

void Session::discard(bool preserve_mode, bool preserve_led) noexcept {
    count_ = 0;
    if (preserve_mode) {
        if (preserve_led) {
            policy_.reset_effects_preserving_led();
        } else {
            policy_.reset_effects();
        }
    } else {
        policy_.reset();
        context_pending_ = false;
        ui_sample_pending_ = false;
        last_context_pause_at_.reset();
    }
    output_.reset_attempt_history();
}

void Session::discard_preserving_led() noexcept {
    count_ = 0;
    policy_.reset_preserving_led();
    output_.reset_attempt_history();
}

void Session::invalidate(bool preserve_mode) noexcept {
    if (epoch_ == std::numeric_limits<std::uint64_t>::max()) {
        diagnostics_.faulted = true;
    } else {
        ++epoch_;
    }
    // A context switch needs silence immediately, while the next UI sample is pending.
    // Keep only a still-fresh sample as a finite neutral bridge; hard losses discard it.
    if (!preserve_mode) {
        observation_.reset();
    }
    ui_sample_pending_ = preserve_mode;
    preserve_led_for_next_observation_ = false;
    preserve_led_deadline_.reset();
    event_context_started_at_.reset();
    context_pending_ = preserve_mode;
    discard(preserve_mode, preserve_mode);
    withdrawn_ = false;
}

void Session::control(Control control_value, MonoTime now) noexcept {
    std::scoped_lock lock(gate_);
    if (closing_) {
        return;
    }
    bool changed = true;
    switch (control_value) {
    case Control::data_ready:
        changed = !data_ready_;
        data_ready_ = true;
        break;
    case Control::pre_load:
    case Control::load_failed:
        load_ready_ = false;
        break;
    case Control::load_succeeded:
    case Control::new_game:
        load_ready_ = true;
        break;
    case Control::focus_lost:
        changed = focused_;
        focused_ = false;
        break;
    case Control::focus_gained:
        changed = !focused_;
        focused_ = true;
        break;
    case Control::context_changed:
        break;
    case Control::fault:
        diagnostics_.faulted = true;
        break;
    }
    if (changed) {
        if (control_value == Control::context_changed &&
            (!last_context_pause_at_ || now > *last_context_pause_at_)) {
            // Opening and closing can both precede the next menu observation.
            // Ignore a delayed duplicate whose timestamp predates the latest fence.
            policy_.pause_led_for_context(now);
            last_context_pause_at_ = now;
        }
        invalidate(control_value == Control::context_changed);
    }
}

std::optional<Ticket> Session::ticket() const noexcept {
    std::scoped_lock lock(gate_);
    if (closing_ || !enabled_ || diagnostics_.faulted || !data_ready_ || !load_ready_) {
        return std::nullopt;
    }
    return Ticket{epoch_};
}

bool Session::publish(Ticket ticket, Observation observation, MonoTime now) noexcept {
    std::scoped_lock lock(gate_);
    if (closing_ || ticket.epoch != epoch_ || !enabled_ || diagnostics_.faulted || !data_ready_ ||
        !load_ready_ || !focused_ || !fresh(observation.sampled_at, now)) {
        return false;
    }
    if (observation_ && observation.sampled_at <= observation_->sampled_at) {
        return false;
    }
    // A failed UI capture is not a new baseline. Keep existing menu silence only
    // until the last valid sample expires; an invalid sample cannot extend it.
    if (observation.mode == Mode::transition && observation_ &&
        (ui_sample_pending_ || observation_->mode == Mode::menu) &&
        fresh(observation_->sampled_at, now)) {
        return false;
    }
    if (!observation_ || observation_->mode != observation.mode || ui_sample_pending_) {
        const bool preserve_mode =
            observation.mode != Mode::suspended && (context_pending_ || observation_.has_value());
        const bool preserve_menu_led = preserve_mode && observation.mode != Mode::transition &&
                                       (observation.mode == Mode::menu || ui_sample_pending_ ||
                                           (observation_ && observation_->mode == Mode::menu));
        if (preserve_led_for_next_observation_) {
            discard_preserving_led();
        } else {
            discard(preserve_mode, preserve_menu_led);
        }
        event_context_started_at_ = observation.sampled_at;
    }
    observation_ = observation;
    ui_sample_pending_ = false;
    preserve_led_for_next_observation_ = false;
    preserve_led_deadline_.reset();
    withdrawn_ = false;
    return true;
}

bool Session::enqueue(Ticket ticket, PolicyEvent event, MonoTime now) noexcept {
    std::scoped_lock lock(gate_);
    if (ticket.epoch != epoch_ || !observation_ || !admitted(now) || ui_sample_pending_ ||
        observation_->mode == Mode::menu || !fresh(event.arrived_at, now) ||
        (event_context_started_at_ && event.arrived_at < *event_context_started_at_)) {
        ++diagnostics_.rejected_events;
        return false;
    }
    const auto baseline = output_.binding_started_at();
    if ((baseline && event.arrived_at <= *baseline) || next_sequence_ == 0) {
        ++diagnostics_.rejected_events;
        return false;
    }
    if (count_ >= inbox_.size()) {
        ++diagnostics_.overflows;
        discard();
    }
    const auto write_index = count_;
    if (write_index >= inbox_.size()) {
        ++diagnostics_.rejected_events;
        return false;
    }
    event.sequence = next_sequence_++;
    inbox_[write_index] = event;
    count_ = write_index + 1;
    ++diagnostics_.accepted_events;
    return true;
}

bool Session::admitted(MonoTime now) const noexcept {
    return !closing_ && enabled_ && !diagnostics_.faulted && data_ready_ && load_ready_ &&
           focused_ && observation_ && observation_->mode != Mode::suspended &&
           observation_->mode != Mode::transition && fresh(observation_->sampled_at, now);
}

inputnext::adapters::FrameRestriction Session::frame_restriction(MonoTime now) const noexcept {
    return observation_ && admitted(now) && (ui_sample_pending_ || observation_->mode == Mode::menu)
               ? inputnext::adapters::FrameRestriction::neutral
               : inputnext::adapters::FrameRestriction::none;
}

void Session::binding_lost([[maybe_unused]] input_next_status status) noexcept {
    discard();
    withdrawn_ = false;
    preserve_led_for_next_observation_ = false;
    preserve_led_deadline_.reset();
}

void Session::binding_changed([[maybe_unused]] MonoTime started_at) noexcept {
    discard();
    observation_.reset();
    withdrawn_ = false;
    preserve_led_for_next_observation_ = false;
    preserve_led_deadline_.reset();
}

void Session::body_rebound(MonoTime baseline_at) noexcept {
    discard_preserving_led();
    observation_.reset();
    withdrawn_ = false;
    body_rebound_during_tick_ = true;
    preserve_led_for_next_observation_ = true;
    const auto latest_safe_start = MonoTime::max() - body_rebound_led_grace;
    preserve_led_deadline_ =
        baseline_at > latest_safe_start ? MonoTime::max() : baseline_at + body_rebound_led_grace;
}

void Session::admission_expired() noexcept {
    discard();
    observation_.reset();
    withdrawn_ = false;
    preserve_led_for_next_observation_ = false;
    preserve_led_deadline_.reset();
}

void Session::tick() noexcept {
    std::scoped_lock lock(gate_);
    inputnext::adapters::TickClock clock{.supplied = {}, .use_steady_clock = true};
    tick_locked(clock);
}

void Session::tick(MonoTime now) noexcept {
    std::scoped_lock lock(gate_);
    inputnext::adapters::TickClock clock{.supplied = now, .use_steady_clock = false};
    tick_locked(clock);
}

bool Session::close_and_drain() noexcept {
    std::scoped_lock lock(gate_);
    if (!closing_) {
        closing_ = true;
        if (epoch_ != std::numeric_limits<std::uint64_t>::max()) {
            ++epoch_;
        }
        observation_.reset();
        preserve_led_for_next_observation_ = false;
        preserve_led_deadline_.reset();
        event_context_started_at_.reset();
        count_ = 0;
        context_pending_ = false;
        withdrawn_ = true;
        policy_.reset();
    }
    inputnext::adapters::TickClock clock{.supplied = {}, .use_steady_clock = true};
    return output_.close_and_drain(clock);
}

void Session::tick_locked(inputnext::adapters::TickClock& clock) noexcept {
    if (closing_) {
        return;
    }
    body_rebound_during_tick_ = false;
    const auto withdraw_unadmitted = [this, &clock] noexcept {
        const auto now = clock.now();
        const bool preserve_led = body_rebound_during_tick_ ||
                                  (preserve_led_for_next_observation_ && preserve_led_deadline_ &&
                                      now < *preserve_led_deadline_);
        body_rebound_during_tick_ = false;
        if (!preserve_led) {
            preserve_led_for_next_observation_ = false;
            preserve_led_deadline_.reset();
            discard(context_pending_);
        }
        if (!withdrawn_) {
            const bool triggers = output_.withdraw_triggers();
            const bool leds = preserve_led || output_.withdraw_leds();
            const bool body = output_.clear_body(clock);
            withdrawn_ = triggers && leds && body && !preserve_led;
        }
    };
    if (!admitted(clock.now()) || !observation_) {
        withdraw_unadmitted();
        output_.maintain_cleanup();
        return;
    }
    const auto& initial_observation = *observation_;
    if (!output_.reconcile(initial_observation.sampled_at, clock)) {
        body_rebound_during_tick_ = false;
        if (output_.diagnostics().last_status != INPUT_NEXT_STATUS_BUSY) {
            count_ = 0;
        }
        return;
    }
    if (!admitted(clock.now()) || !observation_) {
        withdraw_unadmitted();
        return;
    }
    const auto& current_observation = *observation_;
    const auto frame_started_at = clock.now();
    // A pending UI sample must not reuse gameplay effects. Restricted delivery
    // keeps body ownership neutral until a fresh observation resolves the context.
    OutputFrame frame{};
    if (!ui_sample_pending_) {
        frame = policy_.update(current_observation,
            std::span<const PolicyEvent>{inbox_.data(), count_},
            frame_started_at);
    }
    context_pending_ = false;
    count_ = 0;
    static_cast<void>(output_.send(frame, frame_started_at, clock));
}

SessionDiagnostics Session::diagnostics() const noexcept {
    std::scoped_lock lock(gate_);
    auto result = diagnostics_;
    result.delivery = output_.diagnostics();
    return result;
}
} // namespace fallout4_adapter
