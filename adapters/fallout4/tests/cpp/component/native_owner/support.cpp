#include "support.hpp"
#include "cpp/component/native_dispatch/support.hpp"
#include "cpp/component/native_ui_capture/ui_capture_test.hpp"
#include "native/dispatch.hpp"
#include "native/observation.hpp"
#include "native/owner.hpp"
#include "native/owner_services.hpp"
#include "native/ui_capture.hpp"

#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/session.hpp>

#include <F4SE/Interfaces.h>
#include <RE/B/BSFixedString.h>
#include <RE/C/ControlMap.h>
#include <RE/I/INPUT_DEVICE.h>
#include <RE/M/Main.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#undef FAIL

namespace fallout4_adapter::native_owner_test {
namespace {
thread_local OwnerStartupFixture* g_active_startup_fixture = nullptr;
std::atomic<ObservationProvider> g_active_observation_provider{nullptr};
std::atomic<OwnerPhaseAdmissionProvider> g_active_owner_phase_provider{nullptr};
std::atomic<UiGenerationProvider> g_active_ui_generation_provider{nullptr};
} // namespace

void set_observation_provider(ObservationProvider provider) noexcept {
    g_active_observation_provider.store(provider, std::memory_order_release);
}

void set_owner_phase_provider(OwnerPhaseAdmissionProvider provider) noexcept {
    g_active_owner_phase_provider.store(provider, std::memory_order_release);
}

void set_ui_generation_provider(UiGenerationProvider provider) noexcept {
    g_active_ui_generation_provider.store(provider, std::memory_order_release);
}

TestOwnerServices::TestOwnerServices(OwnerStartupFixture* fixture) noexcept : fixture_(fixture) {
}

bool TestOwnerServices::interfaces_available() noexcept {
    if (fixture_) {
        return fixture_->interfaces;
    }
    return true;
}

bool TestOwnerServices::pin_module([[maybe_unused]] const void* module_anchor) noexcept {
    if (fixture_) {
        return fixture_->pin;
    }
    return true;
}

bool TestOwnerServices::start_worker(std::function<void()> worker_func,
    std::thread& worker_thread) noexcept {
    if (fixture_) {
        ++fixture_->worker_calls;
        fixture_->worker_saw_escaped = false;
        if (!fixture_->worker) {
            return false;
        }
    }
    try {
        worker_thread = std::thread(std::move(worker_func));
        return true;
    } catch (...) {
        return false;
    }
}

bool TestOwnerServices::register_listener(
    [[maybe_unused]] F4SE::MessagingInterface::EventCallback callback) noexcept {
    if (fixture_) {
        ++fixture_->listener_calls;
        fixture_->listener_saw_escaped = true;
        fixture_->listener_saw_activation = native_dispatch_test::callback_active();
        if (!fixture_->listener) {
            return false;
        }
    }
    return true;
}

bool TestOwnerServices::activate_main_observation(
    native::MainObservationCallback callback) noexcept {
    return native_dispatch_test::activate_main_observation(callback);
}

std::uint64_t TestOwnerServices::current_ui_generation() noexcept {
    if (auto provider = g_active_ui_generation_provider.load(std::memory_order_acquire)) {
        return provider();
    }
    return ui_capture_test::generation();
}

NativeObservation TestOwnerServices::sample_observation(MonoTime sampled_at,
    bool focused,
    std::uint64_t ui_generation) noexcept {
    if (auto provider = g_active_observation_provider.load(std::memory_order_acquire)) {
        return provider(sampled_at, focused, ui_generation);
    }
    return provide_observation(sampled_at, focused, ui_generation);
}

std::uintptr_t TestOwnerServices::current_player_address() noexcept {
    return supplied_observation.player_address;
}

OwnerPhaseAdmission TestOwnerServices::admit_owner_phase(
    std::uint64_t expected_generation) noexcept {
    if (auto provider = g_active_owner_phase_provider.load(std::memory_order_acquire)) {
        return provider(expected_generation);
    }
    return fallout4_adapter::native::owner_phase_admission(expected_generation);
}

void TestOwnerServices::log_error(const char* message) noexcept {
    if (fixture_) {
        ++fixture_->error_calls;
        fixture_->last_error = message;
    }
}

void TestOwnerServices::log_info([[maybe_unused]] const char* message) noexcept {
}

namespace {
thread_local std::unique_ptr<std::deque<TestOwnerServices>> services_storage;
} // namespace

std::unique_ptr<NativeOwner> make_test_owner(Config config,
    OwnerStartupFixture* fixture,
    std::uint64_t initial_request_id,
    std::mutex* custom_inbox_mutex) {
    if (!services_storage) {
        services_storage = std::make_unique<std::deque<TestOwnerServices>>();
    }
    auto& services = services_storage->emplace_back(fixture ? fixture : g_active_startup_fixture);
    return std::make_unique<NativeOwner>(config, &services, initial_request_id, custom_inbox_mutex);
}

StartupServicesBinding::StartupServicesBinding(const StartupServices& services) noexcept {
    g_active_startup_fixture = services.fixture;
}

StartupServicesBinding::~StartupServicesBinding() noexcept {
    g_active_startup_fixture = nullptr;
}

} // namespace fallout4_adapter::native_owner_test

namespace fallout4_adapter::native {
bool NativeOwnerTestAccess::set_worker_generation(NativeOwner& owner,
    std::uint64_t expected_generation) noexcept {
    if (expected_generation == 0 || ui_capture_test::generation() != expected_generation) {
        return false;
    }
    if (!owner.pipeline().reconcile_worker_generation(MonoTime{})) {
        return false;
    }
    return owner.pipeline().worker_ui_generation() == expected_generation;
}

void NativeOwnerTestAccess::set_data_ready(NativeOwner& owner) noexcept {
    owner.pipeline().set_data_ready();
}

void NativeOwnerTestAccess::publish_task_provenance(NativeOwner& owner,
    TaskProvenance provenance) noexcept {
    owner.pipeline().publish_task_provenance(Ticket{provenance.epoch}, provenance.generation);
}

bool NativeOwnerTestAccess::publish(NativeOwner& owner,
    std::uint64_t epoch,
    std::uint64_t generation) noexcept {
    return owner.pipeline().publish_observation_request(epoch, generation);
}

void NativeOwnerTestAccess::consume_main(NativeOwner& owner) noexcept {
    owner.pipeline().consume_main_observation();
}

void NativeOwnerTestAccess::complete(NativeOwner& owner, std::uint64_t request_id) noexcept {
    owner.pipeline().complete_observation(request_id);
}

std::uint64_t NativeOwnerTestAccess::request_id(const NativeOwner& owner) noexcept {
    return owner.pipeline().request_id();
}

bool NativeOwnerTestAccess::request_pending(const NativeOwner& owner) noexcept {
    return owner.pipeline().request_pending();
}

bool NativeOwnerTestAccess::request_pending_hint(const NativeOwner& owner) noexcept {
    return owner.pipeline().request_pending_hint();
}

bool NativeOwnerTestAccess::request_executing(const NativeOwner& owner) noexcept {
    return owner.pipeline().request_executing();
}

void NativeOwnerTestAccess::set_last_request_id([[maybe_unused]] NativeOwner& owner,
    [[maybe_unused]] std::uint64_t request_id) noexcept {
}

std::optional<NativeOwnerTestAccess::CachedPlayer> NativeOwnerTestAccess::callback_player(
    const NativeOwner& owner) noexcept {
    return owner.pipeline().callback_player();
}

bool NativeOwnerTestAccess::queue_event(NativeOwner& owner,
    std::uint64_t epoch,
    std::uint64_t generation,
    PolicyEvent event) noexcept {
    return owner.pipeline().queue_event(epoch, generation, event);
}

void NativeOwnerTestAccess::queue_control(NativeOwner& owner,
    Control control,
    MonoTime now) noexcept {
    owner.pipeline().queue_control(control, now);
}

void NativeOwnerTestAccess::process_pending(NativeOwner& owner) noexcept {
    owner.pipeline().process_pending();
}

bool NativeOwnerTestAccess::reconcile_generation(NativeOwner& owner, MonoTime now) noexcept {
    return owner.pipeline().reconcile_worker_generation(now);
}

bool NativeOwnerTestAccess::focus(NativeOwner& owner, MonoTime now, bool focused) noexcept {
    return owner.pipeline().apply_focus_change(now, focused);
}

bool NativeOwnerTestAccess::pre_tick(NativeOwner& owner, MonoTime now) noexcept {
    return owner.pipeline().pre_tick_generation_is_current(now);
}

bool NativeOwnerTestAccess::worker_iteration(NativeOwner& owner,
    MonoTime now,
    bool focused,
    MonoTime& next_sample,
    bool& event_overflow_reported) noexcept {
    return owner.pipeline().run_worker_iteration(now,
        focused,
        next_sample,
        event_overflow_reported);
}

void NativeOwnerTestAccess::request_stop(NativeOwner& owner) noexcept {
    owner.pipeline().request_stop();
}

void NativeOwnerTestAccess::finish_worker(NativeOwner& owner) noexcept {
    owner.pipeline().finish_worker();
}

void NativeOwnerTestAccess::enter_inert(NativeOwner& owner) noexcept {
    owner.pipeline().enter_inert("test owner shutdown");
}

Session& NativeOwnerTestAccess::session(NativeOwner& owner) noexcept {
    return owner.pipeline().session();
}

NativeOwnerTestOutput& NativeOwnerTestAccess::output(NativeOwner& owner) noexcept {
    return static_cast<native_owner_test::TestOwnerServices&>(owner.services()).output();
}

bool NativeOwnerTestAccess::has_sample(const NativeOwner& owner) noexcept {
    return owner.pipeline().has_sample();
}

bool NativeOwnerTestAccess::has_cached_player(const NativeOwner& owner) noexcept {
    return owner.pipeline().has_cached_player();
}

bool NativeOwnerTestAccess::stopping(const NativeOwner& owner) noexcept {
    return owner.pipeline().stopping();
}

bool NativeOwnerTestAccess::inert(const NativeOwner& owner) noexcept {
    return owner.pipeline().inert();
}

bool NativeOwnerTestAccess::callbacks_escaped(const NativeOwner& owner) noexcept {
    return owner.callbacks_escaped();
}
} // namespace fallout4_adapter::native

namespace fallout4_adapter::native_owner_test {
NativeObservation supplied_observation{};
std::atomic<OwnerPhaseAdmission> phase_admission{OwnerPhaseAdmission::ready};
std::atomic<std::uint32_t> phase_admission_calls{};
std::atomic<std::uint32_t> observation_provider_calls{};
std::atomic<bool> block_observation_provider{};
std::atomic<bool> observation_provider_entered{};
std::atomic<bool> release_observation_provider{};
std::atomic<NativeOwner*> reentrant_owner{};
std::atomic<std::uint32_t> startup_main_calls{};
std::atomic<std::uint32_t> startup_provider_calls{};

namespace {
void reset_hooks() noexcept {
    release_observation_provider.store(true, std::memory_order_release);
    block_observation_provider.store(false, std::memory_order_release);
    observation_provider_entered.store(false, std::memory_order_release);
    reentrant_owner.store(nullptr, std::memory_order_release);
    phase_admission.store(OwnerPhaseAdmission::ready, std::memory_order_release);
    phase_admission_calls.store(0, std::memory_order_relaxed);
    observation_provider_calls.store(0, std::memory_order_relaxed);
    startup_main_calls.store(0, std::memory_order_relaxed);
    startup_provider_calls.store(0, std::memory_order_relaxed);
    supplied_observation = {};

    g_active_startup_fixture = nullptr;
    set_observation_provider(nullptr);
    set_ui_generation_provider(nullptr);
    native_dispatch_test::reset();
    ui_capture_test::reset(1);
    ui_capture_test::clear_originals();
    ui_capture_test::set_remap_original(nullptr);
    set_owner_phase_provider(nullptr);
}
} // namespace

void check(bool condition, const char* message) {
    INFO(message);
    CHECK(condition);
}

void configure_owner_hooks(std::uint64_t generation) noexcept {
    ui_capture_test::reset(generation);
    set_owner_phase_provider(&provide_owner_phase);
    set_observation_provider(&provide_observation);
    phase_admission.store(OwnerPhaseAdmission::ready, std::memory_order_release);
    phase_admission_calls.store(0, std::memory_order_relaxed);
    observation_provider_calls.store(0, std::memory_order_relaxed);
}

void supply_observation(ObservationFixture fixture) {
    supplied_observation = {};
    supplied_observation.value.weapon = {
        .identity = fixture.weapon_identity,
        .family = WeaponFamily::rifle,
        .automatic = false,
        .drawn = true,
        .ready = true,
    };
    supplied_observation.value.weapon.identity = fixture.weapon_identity;
    supplied_observation.value.sprinting = fixture.sprinting;
    if (fixture.set_triggers) {
        supplied_observation.value.attack_trigger = TriggerSide::right;
        supplied_observation.value.aim_trigger = TriggerSide::left;
    }
    supplied_observation.player_handle = fixture.player_handle;
    supplied_observation.player_address = fixture.player_address;
    supplied_observation.player_form_id = fixture.player_form_id;
}

Environment::Environment() noexcept {
    reset_hooks();
}

Environment::~Environment() noexcept {
    reset_hooks();
    services_storage.reset();
}

std::optional<Ticket> prepare_owner(NativeOwner& owner, std::uint64_t generation, MonoTime now) {
    if (!NativeOwnerTestAccess::set_worker_generation(owner, generation)) {
        check(false, "prepare_owner: UI generation does not match expected generation");
        return std::nullopt;
    }
    NativeOwnerTestAccess::set_data_ready(owner);
    NativeOwnerTestAccess::queue_control(owner, Control::data_ready, now);
    NativeOwnerTestAccess::queue_control(owner, Control::load_succeeded, now);
    static_cast<void>(NativeOwnerTestAccess::focus(owner, now, true));
    NativeOwnerTestAccess::process_pending(owner);
    return NativeOwnerTestAccess::session(owner).ticket();
}

void schedule_and_run(NativeOwner& owner, Ticket ticket, std::uint64_t generation) {
    NativeOwnerTestAccess::publish_task_provenance(owner,
        NativeOwnerTestAccess::TaskProvenance{.epoch = ticket.epoch, .generation = generation});
    check(NativeOwnerTestAccess::publish(owner, ticket.epoch, generation),
        "worker publishes one pending observation identity");
    const auto request_id = NativeOwnerTestAccess::request_id(owner);
    check(request_id != 0 && NativeOwnerTestAccess::request_pending(owner),
        "request has a nonzero immutable identity");
    check(NativeOwnerTestAccess::publish(owner, ticket.epoch, generation),
        "overlapping demand coalesces into the pending request");
    check(NativeOwnerTestAccess::request_id(owner) == request_id,
        "coalescing preserves the pending request identity");
    NativeOwnerTestAccess::process_pending(owner);
    check(NativeOwnerTestAccess::request_pending(owner),
        "worker inbox drain cannot consume Main observation work");
    NativeOwnerTestAccess::consume_main(owner);
    check(NativeOwnerTestAccess::request_id(owner) == 0,
        "matched Main consumption completes its exact request");
}

bool queue_shot(NativeOwner& owner,
    const std::optional<NativeOwnerTestAccess::CachedPlayer>& cached) {
    if (!cached) {
        return false;
    }
    PolicyEvent event{};
    event.kind = EventKind::shot;
    event.arrived_at = std::chrono::steady_clock::now();
    event.weapon_identity = cached->weapon_identity;
    return NativeOwnerTestAccess::queue_event(owner, cached->epoch, cached->ui_generation, event);
}

BlockedMainObservation::BlockedMainObservation(NativeOwner& owner) {
    block_observation_provider.store(true, std::memory_order_release);
    observation_provider_entered.store(false, std::memory_order_release);
    release_observation_provider.store(false, std::memory_order_release);
    thread_ = std::thread([&owner] { NativeOwnerTestAccess::consume_main(owner); });
}

BlockedMainObservation::~BlockedMainObservation() noexcept {
    release_and_join();
}

bool BlockedMainObservation::wait_for_entry(std::chrono::seconds timeout) noexcept {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!observation_provider_entered.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

void BlockedMainObservation::release_and_join() noexcept {
    release_observation_provider.store(true, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
    block_observation_provider.store(false, std::memory_order_release);
    reentrant_owner.store(nullptr, std::memory_order_release);
}

ReentrantOwnerBinding::ReentrantOwnerBinding(NativeOwner& owner) noexcept {
    reentrant_owner.store(&owner, std::memory_order_release);
}

ReentrantOwnerBinding::~ReentrantOwnerBinding() noexcept {
    reentrant_owner.store(nullptr, std::memory_order_release);
}

OwnerPhaseAdmission provide_owner_phase([[maybe_unused]] std::uint64_t generation) noexcept {
    phase_admission_calls.fetch_add(1, std::memory_order_relaxed);
    return phase_admission.load(std::memory_order_acquire);
}

NativeObservation provide_observation(MonoTime now, bool focused, std::uint64_t generation) {
    observation_provider_calls.fetch_add(1, std::memory_order_relaxed);
    if (auto* owner = reentrant_owner.load(std::memory_order_acquire)) {
        NativeOwnerTestAccess::consume_main(*owner);
    }
    if (block_observation_provider.load(std::memory_order_acquire)) {
        observation_provider_entered.store(true, std::memory_order_release);
        while (!release_observation_provider.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
    auto result = supplied_observation;
    result.value.sampled_at = now;
    result.value.mode = focused ? Mode::gameplay : Mode::suspended;
    result.ui_generation = generation;
    return result;
}

namespace {
void startup_main_message_loop_original_impl(RE::Main* /*main*/) {
    startup_main_calls.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

MainMessageLoopOriginal startup_main_message_loop_original() noexcept {
    static_assert(std::is_same_v<decltype(&startup_main_message_loop_original_impl),
        MainMessageLoopOriginal>);
    return &startup_main_message_loop_original_impl;
}

NativeObservation count_startup_observation([[maybe_unused]] MonoTime timestamp,
    [[maybe_unused]] bool focused,
    [[maybe_unused]] std::uint64_t generation) {
    startup_provider_calls.fetch_add(1, std::memory_order_relaxed);
    return {};
}

bool owner_remap_original(RE::ControlMap* /*control_map*/,
    const RE::BSFixedString& /*event_name*/,
    RE::INPUT_DEVICE /*device*/,
    std::int32_t /*button*/) {
    return true;
}

RE::ControlMap* fake_control_map_address() noexcept {
    static std::byte storage{};
    return reinterpret_cast<RE::ControlMap*>(std::addressof(storage));
}

const RE::BSFixedString& empty_remap_event_argument() {
    // This empty fixture avoids the game-only string-pool release relocation at shutdown.
    alignas(RE::BSFixedString) static std::array<std::byte, sizeof(RE::BSFixedString)> storage{};
    static const auto* value =
        std::construct_at(reinterpret_cast<RE::BSFixedString*>(storage.data()));
    return *value;
}
} // namespace fallout4_adapter::native_owner_test
