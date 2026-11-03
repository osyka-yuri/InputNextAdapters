#include "native/owner_pipeline.hpp"

#include "fallout4_adapter/config.hpp"
#include "fallout4_adapter/effect_settings.hpp"
#include "fallout4_adapter/model.hpp"
#include "fallout4_adapter/session.hpp"

#include "native/bindings.hpp"
#include "native/observation.hpp"
#include "native/observation_requests.hpp"
#include "native/owner.hpp"
#include "native/owner_services.hpp"
#include "native/radiation_source.hpp"
#include "native/ui_capture.hpp"

#include <inputnext_adapters/led_policy.hpp>

#include <RE/B/BSAnimationGraphEvent.h>
#include <RE/B/BSTEvent.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/T/TESHitEvent.h>
#include <RE/U/UI.h>
#include <REX/LOG.h>

#include <Windows.h>
#ifdef ERROR
#    undef ERROR
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace fallout4_adapter::native {
namespace {
constexpr auto worker_interval = std::chrono::milliseconds{20};
constexpr auto sample_interval = std::chrono::milliseconds{40};
constexpr auto observation_max_age = std::chrono::milliseconds{100};

void log_fallback(const char* message) noexcept {
    OutputDebugStringA(message);
}

[[nodiscard]] bool step_events_can_output(const Config& config) noexcept {
    const auto can_output = [&config](BodyEffect effect) noexcept {
        const auto& step = config.policy.effects.body[static_cast<std::size_t>(effect)];
        const bool hd_enabled = !std::isfinite(step.hd_scale) || step.hd_scale > 0.0F;
        const bool classic_enabled =
            !std::isfinite(step.classic_scale) || step.classic_scale > 0.0F;
        return step.enabled && (hd_enabled || classic_enabled);
    };
    return config.policy.enabled && config.policy.effects.body_enabled &&
           std::isfinite(config.policy.body_scale) && config.policy.body_scale > 0.0F &&
           (can_output(BodyEffect::ordinary_step) || can_output(BodyEffect::sprint_step));
}

[[nodiscard]] const char* mode_name(Mode mode) noexcept {
    switch (mode) {
    case Mode::suspended:
        return "suspended";
    case Mode::menu:
        return "menu (neutral)";
    case Mode::gameplay:
        return "gameplay";
    case Mode::vats_selection:
        return "VATS selection";
    case Mode::vats_playback:
        return "VATS playback";
    case Mode::transition:
        return "transition";
    }
    return "unknown";
}

[[nodiscard]] const char* control_name(Control control) noexcept {
    switch (control) {
    case Control::data_ready:
        return "data ready";
    case Control::pre_load:
        return "pre-load / data unavailable";
    case Control::load_succeeded:
        return "load succeeded";
    case Control::load_failed:
        return "load failed";
    case Control::new_game:
        return "new game";
    case Control::focus_lost:
        return "focus lost";
    case Control::focus_gained:
        return "focus gained";
    case Control::context_changed:
        return "context changed";
    case Control::fault:
        return "fault";
    }
    return "unknown";
}
} // namespace

NativeOwnerPipeline::NativeOwnerPipeline(NativeOwnerServices& services,
    Config config,
    RE::BSTEventSink<RE::BSAnimationGraphEvent>* anim_sink,
    RE::BSTEventSink<WeaponFiredEvent>* weapon_sink,
    RE::BSTEventSink<RE::TESHitEvent>* hit_sink,
    RE::BSTEventSink<RE::MenuOpenCloseEvent>* menu_sink,
    std::uint64_t initial_request_id,
    std::mutex* custom_inbox_mutex) noexcept
    : services_(services),
      session_(services_.output_port(), config),
      diagnostics_enabled_(config.diagnostics),
      step_events_enabled_(step_events_can_output(config)),
      radiation_enabled_(config.policy.enabled &&
                         inputnext::adapters::valid_led_settings(config.policy.effects.leds) &&
                         config.policy.effects.leds.enabled &&
                         config.policy.effects.leds.radiation_enabled),
      animation_subscription_(anim_sink),
      weapon_sink_(weapon_sink),
      hit_sink_(hit_sink),
      menu_sink_(menu_sink),
      inbox_mutex_(custom_inbox_mutex ? *custom_inbox_mutex : default_inbox_mutex_),
      observation_requests_(initial_request_id) {
}

void NativeOwnerPipeline::queue_control(Control control, MonoTime now) noexcept {
    {
        std::scoped_lock lock(inbox_mutex_);
        request_animation_refresh();
        mailbox_.clear_task_provenance();
        static_cast<void>(observation_requests_.cancel_pending());
        mailbox_.clear_event_admission();
        mailbox_.enqueue_control(control, now);
    }
    wake_cv_.notify_one();
}

void NativeOwnerPipeline::request_animation_refresh() noexcept {
    animation_subscription_.deactivate();
    animation_refresh_generation_.fetch_add(1, std::memory_order_acq_rel);
}

std::optional<NativeOwnerPipeline::CachedPlayer>
NativeOwnerPipeline::callback_player() const noexcept {
    const auto current_generation = services_.current_ui_generation();
    std::scoped_lock lock(inbox_mutex_);
    return mailbox_.callback_player(current_generation);
}

bool NativeOwnerPipeline::queue_event(std::uint64_t epoch,
    std::uint64_t ui_generation,
    PolicyEvent event,
    const AnimationSubscription::Source* animation_source) noexcept {
    const auto current_generation = services_.current_ui_generation();
    bool enqueued{};
    {
        std::scoped_lock lock(inbox_mutex_);
        enqueued = mailbox_.enqueue_event(
            epoch,
            ui_generation,
            event,
            current_generation,
            animation_source != nullptr,
            [this] noexcept { return inert_.load(std::memory_order_acquire); },
            [this, animation_source] noexcept {
                return animation_subscription_.accepts(animation_source);
            });
    }
    if (!enqueued) {
        return false;
    }
    wake_cv_.notify_one();
    return true;
}

bool NativeOwnerPipeline::run_worker_iteration(MonoTime now,
    bool is_focused,
    MonoTime& next_sample,
    bool& event_overflow_reported) noexcept {
    if (stopping_.load(std::memory_order_acquire)) {
        return false;
    }

    const auto previous_ui_generation = worker_ui_generation_;
    if (!reconcile_worker_generation(now)) {
        return false;
    }
    if (worker_ui_generation_ != previous_ui_generation) {
        next_sample = now;
    }
    if (apply_focus_change(now, is_focused)) {
        next_sample = now;
    }

    process_pending();
    if (stopping_.load(std::memory_order_acquire)) {
        return false;
    }

    report_event_overflow_once(event_overflow_reported);

    const bool generation_current = pre_tick_generation_is_current(now);
    if (stopping_.load(std::memory_order_acquire)) {
        return false;
    }
    if (generation_current) {
        const auto ticket = session_.ticket();
        publish_task_provenance(ticket);
        request_observation_if_due(now, is_focused, ticket, next_sample);
    } else {
        next_sample = now;
    }

    if (stopping_.load(std::memory_order_acquire)) {
        return false;
    }
    const auto before_tick = std::chrono::steady_clock::now();
    if (!pre_tick_generation_is_current(before_tick)) {
        next_sample = before_tick;
    }
    if (stopping_.load(std::memory_order_acquire)) {
        return false;
    }

    session_.tick();
    const auto diagnostics_time = std::chrono::steady_clock::now();
    const auto current_ticket = session_.ticket();
    if (diagnostics_enabled_) {
        report_output_admission(diagnostics_time, is_focused, current_ticket.has_value());
    }
    return !stopping_.load(std::memory_order_acquire);
}

void NativeOwnerPipeline::finish_worker() noexcept {
    while (!session_.close_and_drain()) {
        std::unique_lock lock(wake_mutex_);
        wake_cv_.wait_for(lock, worker_interval);
    }
}

void NativeOwnerPipeline::report_event_overflow_once(bool& reported) noexcept {
    if (diagnostics_enabled_ && !reported && mailbox_.event_overflow_count() != 0) {
        services_.log_error(
            "Fallout 4 native event queue overflowed; the oldest queued signal was dropped.");
        reported = true;
    }
}

void NativeOwnerPipeline::report_output_admission(MonoTime now,
    bool is_focused,
    bool has_ticket) noexcept {
    const bool was_admitted = last_reported_admission_.value_or(false);
    const bool observation_fresh = last_observed_mode_ && now >= last_observation_at_ &&
                                   now - last_observation_at_ < observation_max_age;
    const bool observation_active = observation_fresh && *last_observed_mode_ != Mode::suspended &&
                                    *last_observed_mode_ != Mode::transition;
    const bool admitted = has_ticket && is_focused && observation_active;
    if (!last_reported_admission_ || admitted != was_admitted) {
        try {
            REX::INFO("Fallout 4 output admission {} (focused={}, lifecycle={}, mode={}).",
                admitted ? "opened" : "closed",
                is_focused,
                has_ticket,
                last_observed_mode_ ? mode_name(*last_observed_mode_) : "not sampled");
        } catch (...) {
            log_fallback(
                "InputNext Fallout 4: output admission changed, but diagnostic logging failed.\n");
        }
        last_reported_admission_ = admitted;
    }
}

void NativeOwnerPipeline::publish_task_provenance(const std::optional<Ticket>& ticket) noexcept {
    std::scoped_lock lock(inbox_mutex_);
    mailbox_.publish_task_provenance(ticket, worker_ui_generation_);
}

void NativeOwnerPipeline::publish_task_provenance(Ticket ticket,
    std::uint64_t ui_generation) noexcept {
    std::scoped_lock lock(inbox_mutex_);
    mailbox_.publish_task_provenance(ticket, ui_generation);
}

void NativeOwnerPipeline::request_observation_if_due(MonoTime now,
    bool is_focused,
    const std::optional<Ticket>& ticket,
    MonoTime& next_sample) noexcept {
    const bool install_needed = install_pending_.load(std::memory_order_acquire);
    const bool animation_refresh_due =
        animation_refresh_generation_.load(std::memory_order_acquire) !=
        animation_refresh_completed_.load(std::memory_order_acquire);
    const bool sample_due = ticket && is_focused && now >= next_sample;
    if ((install_needed || sample_due || animation_refresh_due) && now >= next_sample) {
        const auto epoch = ticket ? ticket->epoch : 0;
        static_cast<void>(publish_observation_request(epoch, worker_ui_generation_));
        next_sample = now + sample_interval;
    }
}

bool NativeOwnerPipeline::reconcile_worker_generation(MonoTime now) noexcept {
    const auto current_ui_generation = services_.current_ui_generation();
    if (current_ui_generation == 0) {
        enter_inert("The UI transition capture closed permanently; stopping output safely.");
        return false;
    }
    if (current_ui_generation == worker_ui_generation_) {
        return true;
    }

    worker_ui_generation_ = current_ui_generation;
    request_animation_refresh();
    {
        std::scoped_lock lock(inbox_mutex_);
        mailbox_.discard_generation_data();
        static_cast<void>(observation_requests_.cancel_pending());
    }
    session_.control(Control::context_changed, now);
    return true;
}

bool NativeOwnerPipeline::apply_focus_change(MonoTime now, bool is_focused) noexcept {
    const bool was_focused = focused_.exchange(is_focused, std::memory_order_acq_rel);
    if (was_focused == is_focused) {
        return false;
    }

    if (!is_focused) {
        request_animation_refresh();
        {
            std::scoped_lock lock(inbox_mutex_);
            mailbox_.clear_task_provenance();
            static_cast<void>(observation_requests_.cancel_pending());
            mailbox_.clear_event_admission();
        }
        session_.control(Control::focus_lost, now);
    } else {
        session_.control(Control::focus_gained, now);
    }
    return true;
}

bool NativeOwnerPipeline::pre_tick_generation_is_current(MonoTime now) noexcept {
    const auto current_ui_generation = services_.current_ui_generation();
    if (current_ui_generation == 0) {
        enter_inert("The UI transition capture closed permanently; stopping output safely.");
        return false;
    }
    if (current_ui_generation != worker_ui_generation_) {
        static_cast<void>(reconcile_worker_generation(now));
        return false;
    }
    return true;
}

void NativeOwnerPipeline::process_pending() noexcept {
    const auto batch = take_pending_batch();
    const auto validation_now = std::chrono::steady_clock::now();
    apply_pending_controls(batch);

    if (batch.overflow) {
        enter_inert("The bounded native lifecycle queue overflowed; stopping output safely.");
        return;
    }

    const auto current_ui_generation = services_.current_ui_generation();
    if (current_ui_generation == 0) {
        enter_inert("The UI transition capture closed permanently; stopping output safely.");
        return;
    }
    if (current_ui_generation != worker_ui_generation_) {
        return;
    }

    const auto ticket = session_.ticket();
    process_pending_sample(batch.sample, ticket, validation_now, current_ui_generation);
    process_pending_events(batch, ticket, validation_now);
}

NativeOwnerPipeline::PendingBatch NativeOwnerPipeline::take_pending_batch() noexcept {
    PendingBatch batch{};
    {
        std::scoped_lock lock(inbox_mutex_);
        mailbox_.take_pending_batch(batch);
    }
    return batch;
}

void NativeOwnerPipeline::apply_pending_controls(const PendingBatch& batch) noexcept {
    for (std::size_t index = 0; index < batch.control_count; ++index) {
        const auto control = batch.controls[index].control;
        if (diagnostics_enabled_ && control != Control::context_changed) {
            try {
                REX::INFO("Fallout 4 lifecycle control applied: {}.", control_name(control));
            } catch (...) {
                log_fallback("InputNext Fallout 4: lifecycle control applied, but diagnostic "
                             "logging failed.\n");
            }
        }
        last_observed_mode_.reset();
        if (control == Control::data_ready || control == Control::pre_load ||
            control == Control::load_succeeded || control == Control::load_failed ||
            control == Control::new_game) {
            invalidate_radiation_lifecycle();
        }
        session_.control(control, batch.controls[index].arrived_at);
    }
}

void NativeOwnerPipeline::process_pending_sample(const std::optional<PendingSample>& sample,
    const std::optional<Ticket>& ticket,
    MonoTime validation_now,
    std::uint64_t current_ui_generation) noexcept {
    if (!sample || !ticket ||
        !ui_provenance_matches(sample->ticket.epoch,
            ticket->epoch,
            sample->observation.ui_generation,
            worker_ui_generation_,
            current_ui_generation)) {
        return;
    }

    const bool accepted =
        session_.publish(sample->ticket, sample->observation.value, validation_now);
    if (!accepted) {
        std::scoped_lock lock(inbox_mutex_);
        mailbox_.reject_sample(*sample);
        return;
    }
    if (radiation_enabled_ && sample->observation.value.mode == Mode::gameplay) {
        static_cast<void>(enable_radiation_lifecycle());
    }
    if (diagnostics_enabled_ && last_observed_mode_ != sample->observation.value.mode) {
        try {
            REX::INFO("Fallout 4 observation mode changed to {}.",
                mode_name(sample->observation.value.mode));
        } catch (...) {
            log_fallback(
                "InputNext Fallout 4: observation updated, but diagnostic logging failed.\n");
        }
    }
    last_observed_mode_ = sample->observation.value.mode;
    last_observation_at_ = sample->observation.value.sampled_at;
}

void NativeOwnerPipeline::process_pending_events(const PendingBatch& batch,
    const std::optional<Ticket>& ticket,
    MonoTime validation_now) noexcept {
    if (!ticket) {
        return;
    }
    for (std::size_t index = 0; index < batch.event_count; ++index) {
        if (ui_provenance_matches(batch.events[index].epoch,
                ticket->epoch,
                batch.events[index].ui_generation,
                worker_ui_generation_,
                services_.current_ui_generation())) {
            static_cast<void>(session_.enqueue(*ticket, batch.events[index].event, validation_now));
        }
    }
}

bool NativeOwnerPipeline::publish_observation_request(std::uint64_t epoch,
    std::uint64_t ui_generation) noexcept {
    bool exhausted{};
    bool published{};
    bool coalesced{};
    const auto current_generation = services_.current_ui_generation();
    {
        std::scoped_lock lock(inbox_mutex_);
        if (stopping_.load(std::memory_order_acquire) || inert_.load(std::memory_order_acquire)) {
            return false;
        }
        if (!observation_requests_.idle()) {
            coalesced = true;
        } else {
            const PendingObservation candidate{
                .request_id = 0,
                .lifecycle_epoch = epoch,
                .ui_generation = ui_generation,
            };
            if (!mailbox_.observation_request_matches_controls(
                    candidate,
                    current_generation,
                    [this] noexcept { return stopping_.load(std::memory_order_acquire); },
                    [this] noexcept { return inert_.load(std::memory_order_acquire); })) {
                return false;
            }
            const auto result = observation_requests_.try_publish_idle(epoch, ui_generation);
            exhausted = result == ObservationRequestPublish::identity_exhausted;
            published = result == ObservationRequestPublish::published;
            coalesced = result == ObservationRequestPublish::coalesced;
        }
    }
    if (exhausted) {
        enter_inert(
            "The native observation request identity space was exhausted; stopping output safely.");
        return false;
    }
    if (published) {
        wake_cv_.notify_one();
    }
    return published || coalesced;
}

void NativeOwnerPipeline::complete_observation(std::uint64_t request_id) noexcept {
    bool completed{};
    {
        std::scoped_lock lock(inbox_mutex_);
        completed = observation_requests_.complete(request_id);
    }
    if (completed) {
        wake_cv_.notify_one();
    }
}

void NativeOwnerPipeline::consume_main_observation() noexcept {
    if (!observation_requests_.pending_hint()) {
        return;
    }

    PendingObservation observation{};
    {
        std::scoped_lock lock(inbox_mutex_);
        const auto pending = observation_requests_.pending_request();
        if (!pending) {
            return;
        }
        observation = *pending;
        const auto initial_generation = services_.current_ui_generation();
        if (!mailbox_.observation_request_matches_controls(
                observation,
                initial_generation,
                [this] noexcept { return stopping_.load(std::memory_order_acquire); },
                [this] noexcept { return inert_.load(std::memory_order_acquire); })) {
            static_cast<void>(observation_requests_.cancel_pending(observation.request_id));
            return;
        }
    }

    const auto phase = services_.admit_owner_phase(observation.ui_generation);
    if (phase == OwnerPhaseAdmission::deferred) {
        return;
    }
    if (phase == OwnerPhaseAdmission::retired) {
        std::scoped_lock lock(inbox_mutex_);
        static_cast<void>(observation_requests_.cancel_pending(observation.request_id));
        return;
    }

    const auto claim_generation = services_.current_ui_generation();
    {
        std::scoped_lock lock(inbox_mutex_);
        if (!observation_requests_.is_pending(observation)) {
            return;
        }
        if (!mailbox_.observation_request_matches_controls(
                observation,
                claim_generation,
                [this] noexcept { return stopping_.load(std::memory_order_acquire); },
                [this] noexcept { return inert_.load(std::memory_order_acquire); })) {
            static_cast<void>(observation_requests_.cancel_pending(observation.request_id));
            return;
        }
        static_cast<void>(observation_requests_.claim(observation));
    }

    try {
        run_main_observation(observation);
    } catch (...) {
        enter_inert("Native Main-phase observation failed; stopping output safely.");
    }
    complete_observation(observation.request_id);
}

OwnerPhaseAdmission NativeOwnerPipeline::check_observation_group(
    const PendingObservation& observation) noexcept {
    if (stopping_.load(std::memory_order_acquire) || inert_.load(std::memory_order_acquire)) {
        return OwnerPhaseAdmission::retired;
    }
    const auto phase = services_.admit_owner_phase(observation.ui_generation);
    if (phase != OwnerPhaseAdmission::ready) {
        return phase;
    }
    const auto current_generation = services_.current_ui_generation();
    std::scoped_lock lock(inbox_mutex_);
    if (!observation_requests_.is_executing(observation) ||
        !mailbox_.observation_request_matches_controls(
            observation,
            current_generation,
            [this] noexcept { return stopping_.load(std::memory_order_acquire); },
            [this] noexcept { return inert_.load(std::memory_order_acquire); })) {
        return OwnerPhaseAdmission::retired;
    }
    return OwnerPhaseAdmission::ready;
}

void NativeOwnerPipeline::run_main_observation(const PendingObservation& observation) {
    const auto refresh_generation = animation_refresh_generation_.load(std::memory_order_acquire);
    const auto ui_generation = observation.ui_generation;

    if (!prepare_main_observation(observation, refresh_generation)) {
        return;
    }

    if (check_observation_group(observation) != OwnerPhaseAdmission::ready) {
        return;
    }
    const auto sampled_at = std::chrono::steady_clock::now();
    const auto native_observation = services_.sample_observation(sampled_at,
        focused_.load(std::memory_order_acquire),
        ui_generation);
    if (!ui_generation_matches(ui_generation, native_observation.ui_generation) ||
        !ui_generation_matches(ui_generation, services_.current_ui_generation())) {
        return;
    }
    publish_main_observation(observation, refresh_generation, native_observation);
}

bool NativeOwnerPipeline::prepare_main_observation(const PendingObservation& observation,
    std::uint64_t refresh_generation) {
    if (!data_ready_notice_.load(std::memory_order_acquire)) {
        detach_animation_if_admitted(observation, refresh_generation);
        return false;
    }

    if (install_pending_.load(std::memory_order_acquire)) {
        if (check_observation_group(observation) != OwnerPhaseAdmission::ready) {
            return false;
        }
        const auto status = install_sinks();
        if (status == SinkInstall::failed) {
            enter_inert("Required native event registration failed; stopping output safely.");
            return false;
        }
        if (status == SinkInstall::pending) {
            return false;
        }
        install_pending_.store(false, std::memory_order_release);
        services_.log_info(
            "Fallout 4 native event sources registered for the checked 1.11.240 runtime.");
    }

    if (observation.lifecycle_epoch == 0 || !focused_.load(std::memory_order_acquire)) {
        detach_animation_if_admitted(observation, refresh_generation);
        return false;
    }
    if (radiation_enabled_ && !radiation_registered_) {
        radiation_registered_ = install_radiation_source();
    }
    return true;
}

void NativeOwnerPipeline::detach_animation_if_admitted(const PendingObservation& observation,
    std::uint64_t refresh_generation) noexcept {
    if (check_observation_group(observation) != OwnerPhaseAdmission::ready) {
        return;
    }
    if (animation_subscription_.detach()) {
        animation_refresh_completed_.store(refresh_generation, std::memory_order_release);
    }
    report_animation_subscription();
}

bool NativeOwnerPipeline::callback_admission_is_current_locked(
    const PendingObservation& observation) const noexcept {
    return mailbox_.callback_admission_is_current(
        observation,
        [this] noexcept { return services_.current_ui_generation(); },
        [this] noexcept { return focused_.load(std::memory_order_acquire); });
}

void NativeOwnerPipeline::publish_main_observation(const PendingObservation& observation,
    std::uint64_t refresh_generation,
    const NativeObservation& native_observation) {
    if (check_observation_group(observation) != OwnerPhaseAdmission::ready) {
        return;
    }
    const auto player_address = services_.current_player_address();
    auto* player = reinterpret_cast<RE::PlayerCharacter*>(player_address);
    const bool player_matches = player != nullptr && reinterpret_cast<std::uintptr_t>(player) ==
                                                         native_observation.player_address;
    const bool subscribe = player_matches && native_observation.value.mode == Mode::gameplay &&
                           (native_observation.value.power_armor || step_events_enabled_);

    static_cast<void>(animation_subscription_.refresh(subscribe ? player : nullptr, subscribe));
    report_animation_subscription();
    {
        std::scoped_lock lock(inbox_mutex_);
        if (!callback_admission_is_current_locked(observation)) {
            animation_subscription_.deactivate();
            return;
        }
        mailbox_.publish_observation(observation, native_observation);
    }
    animation_refresh_completed_.store(refresh_generation, std::memory_order_release);
    if (check_observation_group(observation) != OwnerPhaseAdmission::ready) {
        std::scoped_lock lock(inbox_mutex_);
        mailbox_.retract_observation(observation);
        return;
    }
    wake_cv_.notify_one();
}

void NativeOwnerPipeline::report_animation_subscription() noexcept {
    if (!diagnostics_enabled_) {
        return;
    }
    const bool active = animation_subscription_.active();
    if (last_reported_animation_subscription_ && *last_reported_animation_subscription_ == active) {
        return;
    }
    try {
        REX::INFO("Fallout 4 power armor animation cue source {}.", active ? "active" : "inactive");
    } catch (...) {
        log_fallback("InputNext Fallout 4: animation subscription state changed, but diagnostic "
                     "logging failed.\n");
    }
    last_reported_animation_subscription_ = active;
}

SinkInstall NativeOwnerPipeline::install_sinks() {
    if (!bindings_available()) {
        services_.log_error(
            "The Fallout 4 executable does not match the guarded 1.11.240 weapon-event binding.");
        return SinkInstall::failed;
    }

    if (!weapon_fired_registered_) {
        if (!register_weapon_fired(weapon_sink_)) {
            services_.log_error(
                "Unable to register the verified Fallout 4 weapon-fired event source.");
            return SinkInstall::failed;
        }
        weapon_fired_registered_ = true;
    }

    if (!hit_registered_) {
        auto* source = RE::TESHitEvent::GetEventSource();
        if (source) {
            source->RegisterSink(hit_sink_);
            hit_registered_ = true;
        } else if (!hit_missing_logged_) {
            try {
                REX::WARN(
                    "Fallout 4 hit-event source is not ready; native registration will retry.");
            } catch (...) {
                log_fallback("InputNext Fallout 4: hit-event registration will retry; diagnostic "
                             "logging failed.\n");
            }
            hit_missing_logged_ = true;
        }
    }

    if (!menu_registered_) {
        if (auto* user_interface = RE::UI::GetSingleton()) {
            if (auto* source = user_interface->GetEventSource<RE::MenuOpenCloseEvent>()) {
                source->RegisterSink(menu_sink_);
                menu_registered_ = true;
            }
        }
    }

    if (radiation_enabled_ && !radiation_registered_) {
        radiation_registered_ = install_radiation_source();
    }

    return weapon_fired_registered_ && hit_registered_ && menu_registered_ ? SinkInstall::ready
                                                                           : SinkInstall::pending;
}

void NativeOwnerPipeline::enter_inert(const char* reason) noexcept {
    if (!inert_.exchange(true, std::memory_order_acq_rel)) {
        invalidate_radiation_lifecycle();
        services_.log_error(reason);
    }
    disable_ui_capture();
    stopping_.store(true, std::memory_order_release);
    {
        std::scoped_lock lock(inbox_mutex_);
        request_animation_refresh();
        mailbox_.clear_task_provenance();
        static_cast<void>(observation_requests_.cancel_pending());
        mailbox_.discard_inert_publication();
    }
    wake_cv_.notify_all();
}

void NativeOwnerPipeline::request_stop() noexcept {
    stopping_.store(true, std::memory_order_release);
    wake_cv_.notify_all();
}

void NativeOwnerPipeline::wait_for_work(MonoTime deadline) noexcept {
    std::unique_lock lock(wake_mutex_);
    wake_cv_.wait_until(lock, deadline, [this] {
        return stopping_.load(std::memory_order_acquire) || observation_requests_.pending_hint();
    });
}

void NativeOwnerPipeline::wake() noexcept {
    wake_cv_.notify_all();
}

std::uint64_t NativeOwnerPipeline::request_id() const noexcept {
    std::scoped_lock lock(inbox_mutex_);
    return observation_requests_.active_id();
}

bool NativeOwnerPipeline::request_pending() const noexcept {
    std::scoped_lock lock(inbox_mutex_);
    return observation_requests_.pending();
}

bool NativeOwnerPipeline::request_pending_hint() const noexcept {
    return observation_requests_.pending_hint();
}

bool NativeOwnerPipeline::request_executing() const noexcept {
    std::scoped_lock lock(inbox_mutex_);
    return observation_requests_.executing();
}

bool NativeOwnerPipeline::has_sample() const noexcept {
    std::scoped_lock lock(inbox_mutex_);
    return mailbox_.has_sample();
}

bool NativeOwnerPipeline::has_cached_player() const noexcept {
    std::scoped_lock lock(inbox_mutex_);
    return mailbox_.has_cached_player();
}
} // namespace fallout4_adapter::native
