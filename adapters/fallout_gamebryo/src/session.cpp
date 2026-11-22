#include <fallout_gamebryo/detail/trigger_effects.hpp>
#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/session.hpp>
#include <fallout_gamebryo/settings.hpp>
#include <input_next_core.h>
#include <inputnext_adapters/output.hpp>
#include <inputnext_adapters/output_session.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <span>

namespace fallout_gamebryo {
namespace {
using inputnext::adapters::FrameRestriction;
using inputnext::adapters::TickClock;

bool fresh(MonoTime sampled_at, MonoTime now) noexcept {
    return sampled_at <= now && sampled_at >= now - snapshot_timeout;
}

bool usable(const Observation& observation) noexcept {
    return observation.focused && observation.alive && observation.player != 0 &&
           observation.mode != Mode::unavailable;
}

bool same_presentation(const Observation& left, const Observation& right) noexcept {
    return left.mode == right.mode && (left.mode != Mode::menu || left.menu == right.menu);
}

bool before(const Observation& observation, const Observation& previous) noexcept {
    return observation.at < previous.at || observation.epoch < previous.epoch ||
           (observation.epoch == previous.epoch && observation.frame < previous.frame);
}

bool event_allowed(const Event& event,
    const Observation& observation,
    const Observation* previous,
    bool menu_enabled) noexcept {
    if (event.epoch != observation.epoch || event.frame > observation.frame ||
        !fresh(event.at, observation.at) ||
        (event.source != observation.player && event.target != observation.player)) {
        return false;
    }
    if (event.mode == observation.mode) {
        return event.mode != Mode::menu || (menu_enabled && event.menu == observation.menu &&
                                               menu_allows(event.menu, event.kind));
    }
    if (previous == nullptr || event.mode != previous->mode || event.frame < previous->frame ||
        event.at < previous->at) {
        return false;
    }
    // A confirmed result can be the action that closes a menu or starts V.A.T.S. playback.
    return (menu_enabled && event.mode == Mode::menu && observation.mode == Mode::gameplay &&
               event.menu == previous->menu && menu_allows(event.menu, event.kind)) ||
           (event.mode == Mode::vats_selection && observation.mode == Mode::vats_playback &&
               event.kind == EventKind::vats_accept);
}

bool same_held_inputs(const Observation& left, const Observation& right) noexcept {
    return left.weapon.id == right.weapon.id && left.weapon.kind == right.weapon.kind &&
           left.weapon.drawn == right.weapon.drawn && left.weapon.ready == right.weapon.ready &&
           left.attack_trigger == right.attack_trigger && left.aim_trigger == right.aim_trigger &&
           left.aiming == right.aiming && left.reloading == right.reloading &&
           detail::attack_resistance_side(left) == detail::attack_resistance_side(right);
}
} // namespace

Session::Session(inputnext::adapters::OutputPort& output, const Settings& settings) noexcept
    : enabled_(
          settings.enabled && ((settings.body_enabled && settings.body_scale > 0.0F) ||
                                  (settings.triggers_enabled && settings.trigger_scale > 0.0F) ||
                                  settings.led.enabled)),
      menu_enabled_(settings.menu_effects),
      policy_(settings),
      output_(output,
          *this,
          settings.controller_slot,
          settings.body_enabled && settings.body_scale > 0.0F) {
}

void Session::invalidate_locked(MonoTime now) noexcept {
    latest_.observation.reset();
    event_count_ = 0;
    earliest_sample_ = std::max(earliest_sample_, now);
    event_floor_ = std::max(event_floor_, now);
    ++latest_.revision;
}

void Session::invalidate(MonoTime now) noexcept {
    const std::scoped_lock lock(ingress_);
    invalidate_locked(now);
}

void Session::set_foreground(bool focused, MonoTime now) noexcept {
    const std::scoped_lock lock(ingress_);
    if (foreground_ != focused) {
        foreground_ = focused;
        invalidate_locked(now);
    }
}

void Session::stop(MonoTime now) noexcept {
    const std::scoped_lock lock(ingress_);
    stopped_ = true;
    invalidate_locked(now);
}

void Session::publish(const Observation& observation, std::span<const Event> events) noexcept {
    const std::scoped_lock lock(ingress_);
    if (!enabled_ || stopped_ || !foreground_ || observation.at < earliest_sample_ ||
        (latest_.observation && before(observation, *latest_.observation))) {
        return;
    }
    if (!usable(observation)) {
        if (latest_.observation) {
            invalidate_locked(observation.at);
        }
        return;
    }

    auto previous = latest_.observation;
    if (previous &&
        (observation.epoch != previous->epoch || observation.player != previous->player ||
            !fresh(previous->at, observation.at))) {
        invalidate_locked(observation.at);
        previous.reset();
    }
    const auto allowed = [&](const Event& event) {
        return event.at > event_floor_ &&
               event_allowed(event, observation, previous ? &*previous : nullptr, menu_enabled_);
    };
    if (previous && !same_presentation(observation, *previous)) {
        ++latest_.presentation;
        const auto end = events_.begin() + static_cast<std::ptrdiff_t>(event_count_);
        event_count_ =
            static_cast<std::size_t>(std::remove_if(events_.begin(), end, [&](const Event& event) {
                return !allowed(event);
            }) - events_.begin());
    }
    latest_.observation = observation;
    for (const auto& event : events) {
        if (!allowed(event)) {
            continue;
        }
        if (event_count_ == events_.size()) {
            invalidate_locked(observation.at);
            return;
        }
        events_[event_count_++] = event;
    }
}

void Session::discard_events_through(MonoTime cutoff) noexcept {
    const std::scoped_lock lock(ingress_);
    event_floor_ = std::max(event_floor_, cutoff);
    const auto end = events_.begin() + static_cast<std::ptrdiff_t>(event_count_);
    event_count_ =
        static_cast<std::size_t>(std::remove_if(events_.begin(), end, [&](const Event& event) {
            return event.at <= event_floor_;
        }) - events_.begin());
}

Session::Snapshot Session::capture(MonoTime now) noexcept {
    const std::scoped_lock lock(ingress_);
    if (latest_.observation && latest_.observation->at < now - snapshot_timeout) {
        invalidate_locked(now);
    }
    auto snapshot = latest_;
    if (snapshot.observation && snapshot.observation->at > now) {
        snapshot.observation.reset();
    }
    return snapshot;
}

bool Session::current_locked(MonoTime now) const noexcept {
    return !stopped_ && foreground_ && active_.observation && latest_.observation &&
           active_.revision == latest_.revision && fresh(active_.observation->at, now) &&
           fresh(latest_.observation->at, now);
}

std::optional<std::size_t> Session::take_frame(MonoTime now,
    std::array<Event, event_capacity>& events) noexcept {
    const std::scoped_lock lock(ingress_);
    if (!current_locked(now) || active_.presentation != latest_.presentation) {
        return std::nullopt;
    }
    active_.observation = latest_.observation;
    std::size_t count = 0;
    for (std::size_t index = 0; index < event_count_; ++index) {
        if (fresh(events_[index].at, now)) {
            events[count++] = events_[index];
        }
    }
    event_count_ = 0;
    return count;
}

bool Session::withdraw(TickClock& clock) noexcept {
    const bool triggers = output_.withdraw_triggers();
    const bool leds = output_.withdraw_leds();
    const bool body = output_.clear_body(clock);
    return triggers && leds && body;
}

void Session::tick(TickClock clock) noexcept {
    if (!enabled_) {
        return;
    }
    active_ = capture(clock.now());
    if (active_.revision != applied_revision_) {
        applied_revision_ = active_.revision;
        policy_.reset();
        close_pending_ = true;
    }
    if (close_pending_) {
        close_pending_ = !output_.close_and_drain(clock);
        if (close_pending_) {
            return;
        }
    }
    if (!active_.observation) {
        output_.maintain_cleanup();
        return;
    }
    if (active_.presentation != applied_presentation_) {
        policy_.reset();
        if (!withdraw(clock)) {
            output_.maintain_cleanup();
            return;
        }
        applied_presentation_ = active_.presentation;
    }
    if (!admitted(clock.now()) || !output_.reconcile(active_.observation->at, clock)) {
        return;
    }
    const auto now = clock.now();
    std::array<Event, event_capacity> events{};
    if (const auto count = take_frame(now, events); count && active_.observation) {
        const auto frame = policy_.update(*active_.observation, {events.data(), *count}, now);
        static_cast<void>(output_.send(frame, now, clock));
    }
}

bool Session::drain(TickClock clock) noexcept {
    return output_.close_and_drain(clock);
}

inputnext::adapters::DeliveryDiagnostics Session::diagnostics() const noexcept {
    return output_.diagnostics();
}

bool Session::admitted(MonoTime now) const noexcept {
    const std::scoped_lock lock(ingress_);
    return current_locked(now);
}

FrameRestriction Session::frame_restriction(MonoTime now) const noexcept {
    const std::scoped_lock lock(ingress_);
    if (!active_.observation || !latest_.observation || !current_locked(now) ||
        active_.presentation != latest_.presentation ||
        !same_held_inputs(*active_.observation, *latest_.observation)) {
        return FrameRestriction::neutral;
    }
    return FrameRestriction::none;
}

void Session::binding_lost(input_next_status /*status*/) noexcept {
    policy_.reset();
    if (active_.observation) {
        discard_events_through(active_.observation->at);
    }
}

void Session::binding_changed(MonoTime started_at) noexcept {
    policy_.reset();
    discard_events_through(started_at);
}

void Session::body_rebound(MonoTime baseline_at) noexcept {
    policy_.reset();
    discard_events_through(baseline_at);
}

void Session::admission_expired() noexcept {
    policy_.reset();
}
} // namespace fallout_gamebryo
