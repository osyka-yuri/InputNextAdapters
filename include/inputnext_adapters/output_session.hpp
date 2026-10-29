#pragma once

#include <inputnext_adapters/output.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace inputnext::adapters {
struct TickClock {
    MonoTime supplied;
    bool use_steady_clock{};

    [[nodiscard]] MonoTime now() const noexcept {
        return use_steady_clock ? std::chrono::steady_clock::now() : supplied;
    }
};

enum class FrameRestriction : std::uint8_t { none, neutral };

class DeliveryObserver {
protected:
    DeliveryObserver() = default;

public:
    virtual ~DeliveryObserver() = default;

    DeliveryObserver(const DeliveryObserver&) = delete;
    DeliveryObserver& operator=(const DeliveryObserver&) = delete;
    DeliveryObserver(DeliveryObserver&&) = delete;
    DeliveryObserver& operator=(DeliveryObserver&&) = delete;

    [[nodiscard]] virtual bool admitted(MonoTime now) const noexcept = 0;

    [[nodiscard]] virtual FrameRestriction frame_restriction(
        [[maybe_unused]] MonoTime now) const noexcept {
        return FrameRestriction::none;
    }

    virtual void binding_lost(input_next_status status) noexcept = 0;
    virtual void binding_changed(MonoTime started_at) noexcept = 0;
    virtual void body_rebound(MonoTime baseline_at) noexcept = 0;
    virtual void admission_expired() noexcept = 0;
};

struct DeliveryDiagnostics {
    input_next_status last_status{INPUT_NEXT_STATUS_UNAVAILABLE};
    bool body_owner_present{};
    std::uint32_t accepted_body_route{INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC};
    std::uint32_t body_fallback_reason{INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE};
};

// The borrowed OutputPort and DeliveryObserver must outlive this session. All calls must be
// serialized by the caller's existing output gate. Destruction performs no output I/O; callers
// must close_and_drain until every context close receipt is acknowledged. TTL expiration does not
// release context capacity. Synchronous observer callbacks may reset attempt history and caller
// state, but must not recursively reconcile, send, withdraw, clear, close, or call OutputPort.
class OutputSession final {
public:
    OutputSession(OutputPort& output, DeliveryObserver& observer, std::uint32_t slot) noexcept;
    ~OutputSession() = default;

    OutputSession(const OutputSession&) = delete;
    OutputSession& operator=(const OutputSession&) = delete;
    OutputSession(OutputSession&&) = delete;
    OutputSession& operator=(OutputSession&&) = delete;

    [[nodiscard]] std::optional<MonoTime> binding_started_at() const noexcept;
    [[nodiscard]] bool bound() const noexcept;
    [[nodiscard]] bool has_body_owner() const noexcept;
    void reset_attempt_history() noexcept;
    [[nodiscard]] bool withdraw_triggers() noexcept;
    [[nodiscard]] bool withdraw_leds() noexcept;
    [[nodiscard]] bool clear_body(TickClock& clock) noexcept;
    [[nodiscard]] bool reconcile(std::optional<MonoTime> observation_at, TickClock& clock) noexcept;
    [[nodiscard]] bool send(OutputFrame frame, MonoTime sampled_at, TickClock& clock) noexcept;
    // Advance one pending cleanup record without resolving or opening a target.
    void maintain_cleanup() noexcept;
    // Visit the cleanup pool once. Repeated calls retry pending close/ACK operations;
    // false means an exact context or receipt remains retained.
    [[nodiscard]] bool close_and_drain(TickClock& clock) noexcept;
    [[nodiscard]] DeliveryDiagnostics diagnostics() const noexcept;

private:
    static constexpr std::size_t cleanup_capacity = 32;

    struct ChannelCache {
        input_next_effects_request_v1 request{};
        std::uint64_t revision{};
        MonoTime sent_at, expires_at;
    };

    struct LedCache {
        input_next_effects_request_v1 request{};
        std::uint64_t revision{};
        MonoTime source_at, sent_at, expires_at;
    };

    struct Binding {
        input_next_device_target_v1 target{};
        input_next_effects_context_v1 context{};
        input_next_effects_capabilities_v1 capabilities{};
        MonoTime started_at;
        MonoTime body_baseline_at;
        std::uint64_t observed_audio_instance_id{};
        std::uint64_t next_mutation_sequence{1};
        std::uint32_t context_state{INPUT_NEXT_EFFECTS_CONTEXT_OPEN};
        std::uint32_t accepted_channels{};
        std::array<ChannelCache, 2> channels{};
        LedCache led{};
        bool delivery_ready{};
        bool body_baseline_required{};
        bool body_lease_supported{};
    };

    struct BodyCache {
        std::uint64_t lease_id{}, audio_instance_id{}, next_sequence{1};
        MonoTime installed_window_end, last_lease_submit_at, retention_deadline;
        BodyProgram installed_pcm_plan{};
        std::uint32_t effective_route{INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC};
        std::uint32_t fallback_reason{INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE};
        bool pcm_plan_accepted{}, pcm_installed{}, installed_pcm_plan_valid{},
            neutral_stop_accepted{};
    };

    struct BodyAttemptHistory {
        input_next_effects_context_v1 context{};
        std::uint64_t audio_instance_id{};
        BodyProgram plan{};
        input_next_effects_rumble_v1 fallback_payload{};
        MonoTime last_replace_at;
        MonoTime fallback_sampled_until;
        bool scoped{}, has_plan{}, has_replace_at{}, has_fallback_sample{}, fallback_sample_valid{};
    };

    struct AcceptedFallback {
        input_next_effects_context_v1 context{};
        std::uint64_t audio_instance_id{};
        input_next_effects_rumble_v1 payload{};
        MonoTime sampled_until;
        bool scoped{}, has_sample{}, sample_valid{};
    };

    struct BodyOwner {
        input_next_device_target_v1 target{};
        input_next_effects_context_v1 context{};
        BodyCache cache{};
    };

    struct PendingCleanup {
        input_next_effects_context_v1 context{};
        input_next_device_target_v1 target{};
        input_next_effects_close_receipt_v1 receipt{};
        bool active{}, has_receipt{};
    };

    struct BodySubmissionTimes {
        MonoTime started_at;
        MonoTime returned_at;
    };

    struct BodySubmissionPlan {
        input_next_effects_body_request_v1 request{};
        BodyProgram frame{};
        input_next_effects_rumble_v1 fallback{};
        MonoTime fallback_until;
        bool acquiring{}, fallback_valid{}, submit{};
    };

    struct TriggerSubmissionPlan {
        std::array<input_next_effects_request_v1, 2> channel_requests{};
        std::uint32_t pending_channels{};
    };

    struct TriggerDeliveryProgress {
        std::uint32_t processed_channels{};
        bool all_submissions_succeeded{true};
        input_next_status first_failure{INPUT_NEXT_STATUS_OK};
    };

    struct SendProgress {
        input_next_status first_failure{INPUT_NEXT_STATUS_OK};
        bool body_sent{}, triggers_sent{true}, leds_sent{true};
    };
    enum class BodyReadiness : std::uint8_t { ready, skipped, failed };

    void handle_binding_failure(input_next_status status) noexcept;
    void expire_admission(TickClock& clock) noexcept;
    void reset_accepted_fallback() noexcept;
    [[nodiscard]] bool withdraw(std::uint32_t channels) noexcept;
    [[nodiscard]] bool
    send_triggers(OutputFrame frame, MonoTime sampled_at, TickClock& clock) noexcept;
    [[nodiscard]] bool send_leds(const OutputFrame& frame, TickClock& clock) noexcept;
    [[nodiscard]] bool submit_led(const OutputFrame& frame,
        const input_next_effects_led_v1& payload,
        MonoTime source_at,
        MonoTime submitted_at,
        MonoTime expires_at,
        std::uint32_t ttl_ms,
        TickClock& clock) noexcept;
    [[nodiscard]] bool sync_obsolete_leds(const OutputFrame& frame,
        TickClock& clock,
        bool allow_expired_replacement = false) noexcept;
    [[nodiscard]] bool process_trigger_channel(Binding& binding,
        OutputFrame& frame,
        const OutputFrame& original,
        MonoTime sampled_at,
        TickClock& clock,
        std::size_t index,
        TriggerDeliveryProgress& progress) noexcept;
    void record_trigger_failure(TriggerDeliveryProgress& progress) const noexcept;
    void restore_trigger_failure(const TriggerDeliveryProgress& progress) noexcept;
    [[nodiscard]] bool sync_obsolete_triggers(Binding& binding,
        OutputFrame& frame,
        const OutputFrame& original,
        MonoTime sampled_at,
        TickClock& clock) noexcept;
    [[nodiscard]] TriggerSubmissionPlan plan_trigger_submissions(Binding& binding,
        OutputFrame& frame,
        const OutputFrame& original,
        MonoTime sampled_at,
        MonoTime now,
        std::uint32_t processed_channels) noexcept;
    [[nodiscard]] bool submit_trigger_channels(Binding& binding,
        const std::array<input_next_effects_request_v1, 2>& channel_requests,
        std::uint32_t channels,
        MonoTime submitted_at,
        TickClock& clock) noexcept;
    [[nodiscard]] OutputFrame apply_frame_restriction(OutputFrame frame, MonoTime now) noexcept;
    void update_frame_restriction(MonoTime now) noexcept;
    [[nodiscard]] bool refresh_send_frame(OutputFrame& frame, TickClock& clock) noexcept;
    [[nodiscard]] bool trigger_body_admission_open(bool triggers_sent, TickClock& clock) noexcept;
    [[nodiscard]] bool deliver_body_frame(OutputFrame& frame,
        MonoTime sampled_at,
        TickClock& clock,
        bool& body_sent) noexcept;
    [[nodiscard]] bool finish_body_frame(OutputFrame& frame,
        MonoTime sampled_at,
        TickClock& clock,
        bool& body_sent) noexcept;
    void restore_trigger_failure(bool triggers_sent, input_next_status trigger_status) noexcept;
    void remember_first_failure(input_next_status& first_failure) const noexcept;
    [[nodiscard]] bool deliver_send_body(OutputFrame& frame,
        MonoTime sampled_at,
        TickClock& clock,
        SendProgress& progress) noexcept;
    [[nodiscard]] bool finish_send_channels(OutputFrame& frame,
        const OutputFrame& original,
        MonoTime sampled_at,
        TickClock& clock,
        SendProgress& progress) noexcept;
    [[nodiscard]] bool neutralize_body_if_restricted(OutputFrame& frame,
        MonoTime sampled_at,
        TickClock& clock,
        bool& body_sent) noexcept;
    [[nodiscard]] bool finish_trigger_delivery(OutputFrame& frame,
        const OutputFrame& original,
        MonoTime sampled_at,
        TickClock& clock,
        bool& body_sent) noexcept;
    [[nodiscard]] BodyReadiness prepare_body_owner(TickClock& clock) noexcept;
    void bind_new_target(const input_next_device_target_v1& target,
        const input_next_effects_capabilities_v1& capabilities,
        std::uint64_t audio_instance,
        bool body_supported,
        TickClock& clock) noexcept;
    void reconcile_audio_instance(Binding& binding,
        std::uint64_t audio_instance,
        TickClock& clock) noexcept;
    void reconcile_body_support(Binding& binding, bool body_supported, TickClock& clock) noexcept;
    [[nodiscard]] bool plan_body_submission(OutputFrame frame,
        MonoTime sampled_at,
        TickClock& clock,
        BodySubmissionPlan& plan) noexcept;
    static void sample_body_fallback(const Binding& binding,
        const OutputFrame& frame,
        MonoTime now,
        BodySubmissionPlan& plan) noexcept;
    [[nodiscard]] bool accepted_fallback_changed(const Binding& binding,
        MonoTime now,
        const BodySubmissionPlan& plan) const noexcept;
    [[nodiscard]] bool attempted_fallback_changed(MonoTime now,
        const BodySubmissionPlan& plan) const noexcept;
    [[nodiscard]] bool configure_body_pcm_command(const Binding& binding,
        BodySubmissionPlan& plan,
        const BodyCache& cache,
        MonoTime now,
        bool active,
        bool held) noexcept;
    [[nodiscard]] bool should_wait_for_body_submission(const BodySubmissionPlan& plan,
        const BodyCache& cache,
        MonoTime now,
        bool fallback_changed,
        bool attempted_fallback_changed,
        bool semantic_retry) const noexcept;
    enum class NeutralResolution : std::uint8_t { proceed, complete, failed };
    [[nodiscard]] NeutralResolution resolve_neutral_submission(BodySubmissionPlan& plan,
        MonoTime sampled_at,
        TickClock& clock) noexcept;
    [[nodiscard]] bool
    submit_body(BodySubmissionPlan& plan, MonoTime sampled_at, TickClock& clock) noexcept;
    [[nodiscard]] bool prepare_body_pcm(BodySubmissionPlan& plan, MonoTime now) noexcept;
    void record_body_attempt(const BodySubmissionPlan& plan, MonoTime now) noexcept;
    void record_accepted_body_submission(const BodySubmissionPlan& plan,
        const input_next_effects_body_receipt_v1& receipt,
        BodyCache& accepted,
        const BodySubmissionTimes& times) noexcept;
    [[nodiscard]] bool send_body(OutputFrame frame, MonoTime sampled_at, TickClock& clock) noexcept;
    [[nodiscard]] bool retain_cleanup(const Binding& binding) noexcept;
    [[nodiscard]] bool close_binding(input_next_status status, bool notify_observer) noexcept;
    void advance_pending_cleanup(std::size_t max_records = 1) noexcept;
    void advance_cleanup_record(PendingCleanup& pending) noexcept;
    [[nodiscard]] bool refresh_context() noexcept;
    [[nodiscard]] static std::uint64_t consume_mutation_sequence(Binding& binding) noexcept;

    OutputPort& output_;
    DeliveryObserver& observer_;
    std::uint32_t slot_{};
    std::optional<MonoTime> observation_at_;
    std::optional<Binding> binding_;
    std::optional<BodyOwner> body_owner_;
    std::array<PendingCleanup, cleanup_capacity> pending_cleanup_{};
    std::size_t next_cleanup_{};
    BodyAttemptHistory body_attempt_history_{};
    AcceptedFallback accepted_fallback_{};
    std::array<float, std::size_t{body_pcm_max_frames} * 2U> body_pcm_scratch_{};
    MonoTime last_body_attempt_at_;
    bool body_attempted_{}, body_last_attempt_failed_{};
    bool frame_neutral_{};
    DeliveryDiagnostics diagnostics_{};
};
} // namespace inputnext::adapters
