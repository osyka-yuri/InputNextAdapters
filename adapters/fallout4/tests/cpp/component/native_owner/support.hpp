#pragma once

#include "cpp/component/native_dispatch/support.hpp"
#include "cpp/component/native_ui_capture/ui_capture_test.hpp"
#include "native/dispatch.hpp"
#include "native/owner.hpp"
#include "native/owner_services.hpp"
#include "native/ui_capture.hpp"

#include <fallout4_adapter/config.hpp>
#include <input_next_core.h>
#include <input_next_effects.h>
#include <inputnext_adapters/output.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>

namespace fallout4_adapter::native {

class NativeOwnerTestOutput final : public inputnext::adapters::OutputPort {
public:
    bool connect_success{true};
    bool throw_on_start{false};
    std::function<void()> on_start;
    std::size_t target_calls{};
    std::size_t trigger_submit_calls{};
    std::size_t trigger_clear_calls{};
    std::size_t body_submit_calls{};
    std::size_t body_clear_calls{};
    std::size_t close_calls{};
    std::size_t ack_close_calls{};
    bool body_leases_enabled{};
    std::function<void()> on_body_submit;
    std::uint64_t next_body_lease{40};
    std::uint64_t last_body_lease{};
    std::uint64_t last_body_clear_lease{};
    std::uint64_t next_context_id{1};
    input_next_device_target_v1 current_target{};
    input_next_effects_context_v1 current_context{};
    input_next_status open_status{INPUT_NEXT_STATUS_OK};
    input_next_status query_context_status{INPUT_NEXT_STATUS_OK};
    std::uint32_t query_context_state{INPUT_NEXT_EFFECTS_CONTEXT_OPEN};
    input_next_status close_status{INPUT_NEXT_STATUS_REVOKED};
    input_next_status ack_close_status{INPUT_NEXT_STATUS_OK};
    std::array<input_next_status, 4> close_status_script{};
    std::array<input_next_status, 4> ack_close_status_script{};
    std::size_t close_status_script_size{}, ack_close_status_script_size{};
    std::array<input_next_effects_close_v1, 4> close_requests{};
    std::array<input_next_effects_close_receipt_v1, 4> acknowledged_close_receipts{};
    input_next_effects_close_v1 last_close_request{};
    input_next_effects_close_receipt_v1 last_close_receipt{}, last_acked_close_receipt{};

    [[nodiscard]] inputnext::adapters::ConnectResult start(
        [[maybe_unused]] const std::filesystem::path& runtime_path) const {
        if (throw_on_start) {
            throw std::bad_alloc{};
        }
        if (on_start) {
            on_start();
        }
        return connect_success ? inputnext::adapters::ConnectResult{}
                               : inputnext::adapters::ConnectResult{
                                     .error = inputnext::adapters::ConnectError::load_failed,
                                     .native_error = 1,
                                 };
    }

    input_next_status resolve_target(const input_next_device_selector_v1& selector,
        input_next_device_target_v1& target,
        input_next_effects_capabilities_v1& capabilities) noexcept override {
        ++target_calls;
        target = {};
        target.struct_size = sizeof(target);
        target.runtime_id[0] = 1;
        target.runtime_id[1] = 2;
        target.device_key = static_cast<std::uint64_t>(selector.xinput_slot) + 1;
        target.connection_id = 1;
        current_target = target;
        capabilities.struct_size = sizeof(capabilities);
        capabilities.supported_channels = INPUT_NEXT_EFFECTS_ALL_CHANNELS;
        capabilities.feature_flags =
            INPUT_NEXT_EFFECTS_CAP_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_CAP_RIGHT_TRIGGER |
            INPUT_NEXT_EFFECTS_CAP_LOW_RUMBLE | INPUT_NEXT_EFFECTS_CAP_HIGH_RUMBLE |
            (body_leases_enabled ? INPUT_NEXT_EFFECTS_CAP_BODY_LEASE : 0U);
        capabilities.completion_capabilities = 0;
        capabilities.audio_instance_id = 0;
        capabilities.capabilities_revision = 1;
        return INPUT_NEXT_STATUS_OK;
    }

    input_next_status open_context(const input_next_effects_open_v1& request,
        input_next_effects_context_v1& context) noexcept override {
        if (open_status != INPUT_NEXT_STATUS_OK) {
            return open_status;
        }
        input_next_effects_context_v1 opened{};
        opened.struct_size = sizeof(opened);
        opened.runtime_id[0] = request.target.runtime_id[0];
        opened.runtime_id[1] = request.target.runtime_id[1];
        opened.id = next_context_id++;
        current_context = opened;
        current_target = request.target;
        context = opened;
        return INPUT_NEXT_STATUS_OK;
    }

    input_next_status query_context(const input_next_effects_context_v1& context,
        input_next_effects_context_info_v1& info) noexcept override {
        if (context.id != current_context.id ||
            context.runtime_id[0] != current_context.runtime_id[0] ||
            context.runtime_id[1] != current_context.runtime_id[1]) {
            return INPUT_NEXT_STATUS_STALE_CONTEXT;
        }
        if (query_context_status != INPUT_NEXT_STATUS_OK &&
            query_context_status != INPUT_NEXT_STATUS_REVOKED) {
            return query_context_status;
        }
        info = {};
        info.struct_size = sizeof(info);
        info.state = query_context_state;
        info.context = current_context;
        return query_context_status;
    }

    input_next_status submit(const input_next_effects_request_v1& request,
        input_next_effects_receipt_v1& receipt) noexcept override {
        ++trigger_submit_calls;
        receipt = {};
        receipt.struct_size = sizeof(receipt);
        receipt.accepted_mask = request.channel_mask;
        receipt.context = request.context;
        receipt.mutation_sequence = request.mutation_sequence;
        receipt.publication_revision = request.mutation_sequence;
        for (std::size_t index = 0; index < 4; ++index) {
            if ((request.channel_mask & (UINT32_C(1) << index)) != 0) {
                receipt.channel_revisions[index] = request.mutation_sequence;
            }
        }
        return INPUT_NEXT_STATUS_OK;
    }

    input_next_status clear(
        [[maybe_unused]] const input_next_effects_clear_v1& request) noexcept override {
        ++trigger_clear_calls;
        return INPUT_NEXT_STATUS_OK;
    }

    input_next_status submit_body(const input_next_effects_body_request_v1& request,
        input_next_effects_body_receipt_v1& receipt) noexcept override {
        ++body_submit_calls;
        if (on_body_submit) {
            on_body_submit();
        }
        if (!body_leases_enabled) {
            return INPUT_NEXT_STATUS_UNAVAILABLE;
        }
        receipt = {};
        receipt.struct_size = sizeof(receipt);
        receipt.effective_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC;
        receipt.fallback_reason = request.route_preference == INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC
                                      ? INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE
                                      : INPUT_NEXT_EFFECTS_BODY_FALLBACK_NOT_READY;
        receipt.context = request.context;
        receipt.target = current_target;
        last_body_lease = request.lease_id == 0 ? next_body_lease++ : request.lease_id;
        receipt.lease_id = last_body_lease;
        receipt.audio_instance_id = request.audio_instance_id;
        receipt.sequence = request.sequence;
        receipt.publication_revision = request.sequence;
        return INPUT_NEXT_STATUS_OK;
    }

    input_next_status clear_body(
        [[maybe_unused]] const input_next_effects_body_clear_v1& request) noexcept override {
        ++body_clear_calls;
        last_body_clear_lease = request.lease_id;
        return INPUT_NEXT_STATUS_OK;
    }

    input_next_status close_context(const input_next_effects_close_v1& request,
        input_next_effects_close_receipt_v1& receipt) noexcept override {
        const auto status = close_calls < close_status_script_size
                                ? close_status_script[close_calls]
                                : close_status;
        if (close_calls < close_requests.size()) {
            close_requests[close_calls] = request;
        }
        ++close_calls;
        last_close_request = request;
        if (request.context.id != current_context.id) {
            return INPUT_NEXT_STATUS_STALE_CONTEXT;
        }
        if (status != INPUT_NEXT_STATUS_REVOKED) {
            return status;
        }
        receipt = {};
        receipt.struct_size = sizeof(receipt);
        receipt.disposition = INPUT_NEXT_EFFECTS_CLOSE_NORMAL;
        receipt.context = request.context;
        receipt.target = current_target;
        receipt.close_request_id = request.close_request_id;
        receipt.publication_revision = request.close_request_id + 1;
        last_close_receipt = receipt;
        return status;
    }

    input_next_status ack_close(
        const input_next_effects_close_receipt_v1& receipt) noexcept override {
        const auto status = ack_close_calls < ack_close_status_script_size
                                ? ack_close_status_script[ack_close_calls]
                                : ack_close_status;
        if (ack_close_calls < acknowledged_close_receipts.size()) {
            acknowledged_close_receipts[ack_close_calls] = receipt;
        }
        ++ack_close_calls;
        last_acked_close_receipt = receipt;
        return status;
    }

    input_next_status query_progress(const input_next_effects_progress_query_v1& query,
        input_next_effects_progress_v1& progress) noexcept override {
        progress = {};
        progress.struct_size = sizeof(progress);
        progress.stage = INPUT_NEXT_EFFECTS_PROGRESS_CURRENT_COMPLETED;
        progress.target = query.target;
        progress.requested_publication_revision = query.publication_revision;
        return INPUT_NEXT_STATUS_OK;
    }
};

struct NativeOwnerTestAccess {
    using CachedPlayer = NativeOwnerPipeline::CachedPlayer;

    struct TaskProvenance {
        std::uint64_t epoch{};
        std::uint64_t generation{};
    };

    [[nodiscard]] static bool set_worker_generation(NativeOwner& owner,
        std::uint64_t expected_generation) noexcept;
    static void set_data_ready(NativeOwner& owner) noexcept;
    static void publish_task_provenance(NativeOwner& owner, TaskProvenance provenance) noexcept;
    static bool publish(NativeOwner& owner, std::uint64_t epoch, std::uint64_t generation) noexcept;
    static void consume_main(NativeOwner& owner) noexcept;
    static void complete(NativeOwner& owner, std::uint64_t request_id) noexcept;
    static std::uint64_t request_id(const NativeOwner& owner) noexcept;
    static bool request_pending(const NativeOwner& owner) noexcept;
    static bool request_pending_hint(const NativeOwner& owner) noexcept;
    static bool request_executing(const NativeOwner& owner) noexcept;
    static void set_last_request_id(NativeOwner& owner, std::uint64_t request_id) noexcept;
    static std::optional<CachedPlayer> callback_player(const NativeOwner& owner) noexcept;
    static bool queue_event(NativeOwner& owner,
        std::uint64_t epoch,
        std::uint64_t generation,
        PolicyEvent event) noexcept;
    static void queue_control(NativeOwner& owner, Control control, MonoTime now) noexcept;
    static void process_pending(NativeOwner& owner) noexcept;
    static bool reconcile_generation(NativeOwner& owner, MonoTime now) noexcept;
    static bool focus(NativeOwner& owner, MonoTime now, bool focused) noexcept;
    static bool pre_tick(NativeOwner& owner, MonoTime now) noexcept;
    static bool worker_iteration(NativeOwner& owner,
        MonoTime now,
        bool focused,
        MonoTime& next_sample,
        bool& event_overflow_reported) noexcept;
    static void request_stop(NativeOwner& owner) noexcept;
    static void finish_worker(NativeOwner& owner) noexcept;
    static void enter_inert(NativeOwner& owner) noexcept;
    static Session& session(NativeOwner& owner) noexcept;
    static NativeOwnerTestOutput& output(NativeOwner& owner) noexcept;
    static bool has_sample(const NativeOwner& owner) noexcept;
    static bool has_cached_player(const NativeOwner& owner) noexcept;
    static bool stopping(const NativeOwner& owner) noexcept;
    static bool inert(const NativeOwner& owner) noexcept;
    static bool callbacks_escaped(const NativeOwner& owner) noexcept;
};

} // namespace fallout4_adapter::native

namespace fallout4_adapter::native_owner_test {
using namespace std::chrono_literals;
using namespace fallout4_adapter;
using namespace fallout4_adapter::native;

struct OwnerStartupFixture {
    bool interfaces{true};
    bool pin{true};
    bool worker{true};
    bool listener{true};
    bool worker_saw_escaped{};
    bool listener_saw_escaped{};
    bool listener_saw_activation{};
    std::uint32_t worker_calls{};
    std::uint32_t listener_calls{};
    std::uint32_t start_output_calls{};
    bool throw_on_start_output{false};
    std::function<void()> start_output_hook;
    std::uint32_t error_calls{};
    const char* last_error{nullptr};
};

class TestOwnerServices final : public NativeOwnerServices {
public:
    explicit TestOwnerServices(OwnerStartupFixture* fixture = nullptr) noexcept;
    ~TestOwnerServices() override = default;
    TestOwnerServices(const TestOwnerServices&) = delete;
    TestOwnerServices& operator=(const TestOwnerServices&) = delete;
    TestOwnerServices(TestOwnerServices&&) = delete;
    TestOwnerServices& operator=(TestOwnerServices&&) = delete;

    NativeOwnerTestOutput& output() noexcept { return output_; }

    inputnext::adapters::ConnectResult start_output(
        const std::filesystem::path& runtime_path) override {
        if (fixture_) {
            ++fixture_->start_output_calls;
            if (fixture_->throw_on_start_output) {
                throw std::bad_alloc{};
            }
            if (fixture_->start_output_hook) {
                fixture_->start_output_hook();
            }
        }
        return output_.start(runtime_path);
    }

    inputnext::adapters::OutputPort& output_port() noexcept override { return output_; }

    bool interfaces_available() noexcept override;
    bool pin_module(const void* module_anchor) noexcept override;
    bool start_worker(std::function<void()> worker_func,
        std::thread& worker_thread) noexcept override;
    bool register_listener(F4SE::MessagingInterface::EventCallback callback) noexcept override;
    bool activate_main_observation(MainObservationCallback callback) noexcept override;

    std::uint64_t current_ui_generation() noexcept override;
    NativeObservation sample_observation(MonoTime sampled_at,
        bool focused,
        std::uint64_t ui_generation) noexcept override;
    std::uintptr_t current_player_address() noexcept override;

    bool foreground_is_game() noexcept override { return true; }

    OwnerPhaseAdmission admit_owner_phase(std::uint64_t expected_generation) noexcept override;

    void log_error(const char* message) noexcept override;
    void log_info(const char* message) noexcept override;

    void set_fixture(OwnerStartupFixture* fixture) noexcept { fixture_ = fixture; }

private:
    NativeOwnerTestOutput output_;
    OwnerStartupFixture* fixture_{nullptr};
};

[[nodiscard]] std::unique_ptr<NativeOwner> make_test_owner(Config config = {},
    OwnerStartupFixture* fixture = nullptr,
    std::uint64_t initial_request_id = 0,
    std::mutex* custom_inbox_mutex = nullptr);

using ObservationProvider = NativeObservation (*)(MonoTime now,
    bool focused,
    std::uint64_t generation);
void set_observation_provider(ObservationProvider provider) noexcept;
using OwnerPhaseAdmissionProvider = OwnerPhaseAdmission (*)(
    std::uint64_t expected_generation) noexcept;
void set_owner_phase_provider(OwnerPhaseAdmissionProvider provider) noexcept;
using UiGenerationProvider = std::uint64_t (*)() noexcept;
void set_ui_generation_provider(UiGenerationProvider provider) noexcept;

extern NativeObservation supplied_observation;
extern std::atomic<OwnerPhaseAdmission> phase_admission;
extern std::atomic<std::uint32_t> phase_admission_calls;
extern std::atomic<std::uint32_t> observation_provider_calls;
extern std::atomic<bool> block_observation_provider;
extern std::atomic<bool> observation_provider_entered;
extern std::atomic<bool> release_observation_provider;
extern std::atomic<NativeOwner*> reentrant_owner;
extern std::atomic<std::uint32_t> startup_main_calls;
extern std::atomic<std::uint32_t> startup_provider_calls;

void check(bool condition, const char* message);
void configure_owner_hooks(std::uint64_t generation) noexcept;

struct ObservationFixture {
    std::uint64_t weapon_identity{};
    std::uint32_t player_handle{};
    std::uintptr_t player_address{};
    std::uint32_t player_form_id{};
    bool set_triggers{};
    bool sprinting{};
};

void supply_observation(ObservationFixture fixture);

struct Environment final {
    Environment() noexcept;
    ~Environment() noexcept;

    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;
    Environment(Environment&&) = delete;
    Environment& operator=(Environment&&) = delete;
};

std::optional<Ticket> prepare_owner(NativeOwner& owner, std::uint64_t generation, MonoTime now);
void schedule_and_run(NativeOwner& owner, Ticket ticket, std::uint64_t generation);
[[nodiscard]] bool queue_shot(NativeOwner& owner,
    const std::optional<NativeOwnerTestAccess::CachedPlayer>& cached);

struct BlockedMainObservation final {
    explicit BlockedMainObservation(NativeOwner& owner);
    ~BlockedMainObservation() noexcept;

    BlockedMainObservation(const BlockedMainObservation&) = delete;
    BlockedMainObservation& operator=(const BlockedMainObservation&) = delete;
    BlockedMainObservation(BlockedMainObservation&&) = delete;
    BlockedMainObservation& operator=(BlockedMainObservation&&) = delete;

    [[nodiscard]] static bool wait_for_entry(
        std::chrono::seconds timeout = std::chrono::seconds{5}) noexcept;
    void release_and_join() noexcept;

private:
    std::thread thread_;
};

struct ReentrantOwnerBinding final {
    explicit ReentrantOwnerBinding(NativeOwner& owner) noexcept;
    ~ReentrantOwnerBinding() noexcept;

    ReentrantOwnerBinding(const ReentrantOwnerBinding&) = delete;
    ReentrantOwnerBinding& operator=(const ReentrantOwnerBinding&) = delete;
    ReentrantOwnerBinding(ReentrantOwnerBinding&&) = delete;
    ReentrantOwnerBinding& operator=(ReentrantOwnerBinding&&) = delete;
};

struct StartupServices {
    OwnerStartupFixture* fixture{};
};

inline StartupServices startup_services(OwnerStartupFixture& fixture) noexcept {
    return StartupServices{&fixture};
}

class StartupServicesBinding final {
public:
    explicit StartupServicesBinding(const StartupServices& services) noexcept;
    ~StartupServicesBinding() noexcept;

    StartupServicesBinding(const StartupServicesBinding&) = delete;
    StartupServicesBinding& operator=(const StartupServicesBinding&) = delete;
    StartupServicesBinding(StartupServicesBinding&&) = delete;
    StartupServicesBinding& operator=(StartupServicesBinding&&) = delete;
};

OwnerPhaseAdmission provide_owner_phase(std::uint64_t generation) noexcept;
NativeObservation provide_observation(MonoTime now, bool focused, std::uint64_t generation);
MainMessageLoopOriginal startup_main_message_loop_original() noexcept;
NativeObservation
count_startup_observation(MonoTime timestamp, bool focused, std::uint64_t generation);
bool owner_remap_original(RE::ControlMap* control_map,
    const RE::BSFixedString& event_name,
    RE::INPUT_DEVICE device,
    std::int32_t button);
RE::ControlMap* fake_control_map_address() noexcept;
const RE::BSFixedString& empty_remap_event_argument();
} // namespace fallout4_adapter::native_owner_test
