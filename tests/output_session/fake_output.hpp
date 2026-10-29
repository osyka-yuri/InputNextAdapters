#pragma once

#include "support/test_runner.hpp"

#include <inputnext_adapters/output.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace inputnext::test::output_session {
using inputnext::adapters::OutputPort;

inline bool same_target(const input_next_device_target_v1& left,
    const input_next_device_target_v1& right) noexcept {
    return left.struct_size == right.struct_size && left.reserved == right.reserved &&
           left.runtime_id[0] == right.runtime_id[0] && left.runtime_id[1] == right.runtime_id[1] &&
           left.device_key == right.device_key && left.connection_id == right.connection_id;
}

inline bool same_close_receipt(const input_next_effects_close_receipt_v1& left,
    const input_next_effects_close_receipt_v1& right) noexcept {
    return left.struct_size == right.struct_size && left.disposition == right.disposition &&
           left.context.struct_size == right.context.struct_size &&
           left.context.reserved == right.context.reserved &&
           left.context.runtime_id[0] == right.context.runtime_id[0] &&
           left.context.runtime_id[1] == right.context.runtime_id[1] &&
           left.context.id == right.context.id && same_target(left.target, right.target) &&
           left.close_request_id == right.close_request_id &&
           left.publication_revision == right.publication_revision;
}

struct BodyCall {
    struct Request : input_next_effects_body_request_v1 {
        input_next_device_target_v1 target{};
    } request{};

    std::vector<float> samples;
};

struct TriggerCall : input_next_effects_request_v1 {
    input_next_device_target_v1 target{};
};

struct LedCall : input_next_effects_request_v1 {
    input_next_device_target_v1 target{};
};

struct BodyClearCall : input_next_effects_body_clear_v1 {
    input_next_device_target_v1 target{};
};

enum class ReceiptFault : std::uint8_t {
    none,
    size,
    context,
    target,
    revision,
};

enum class CloseReceiptFault : std::uint8_t {
    none,
    size,
    context,
    target,
    close_request_id,
    unknown_disposition,
    zero_revision,
    nonzero_revision,
};

struct FakeContext {
    input_next_effects_context_v1 context{};
    input_next_device_target_v1 target{};
    input_next_effects_close_receipt_v1 close_receipt{};
    std::array<std::uint64_t, 4> channel_revisions{};
    std::uint64_t publication_revision{1};
    std::uint64_t last_mutation_sequence{};
    std::uint64_t body_lease{};
    std::uint64_t body_sequence{};
    std::uint32_t state{INPUT_NEXT_EFFECTS_CONTEXT_OPEN};
    bool has_close_receipt{};
};

class FakeOutput final : public OutputPort {
public:
    input_next_device_target_v1 target{
        .struct_size = sizeof(target),
        .reserved = 0,
        .runtime_id = {1, 2},
        .device_key = 17,
        .connection_id = 23,
    };
    input_next_effects_capabilities_v1 capabilities{
        .struct_size = sizeof(capabilities),
        .supported_channels = INPUT_NEXT_EFFECTS_ALL_CHANNELS,
        .feature_flags = INPUT_NEXT_EFFECTS_CAP_LEFT_TRIGGER |
                         INPUT_NEXT_EFFECTS_CAP_RIGHT_TRIGGER | INPUT_NEXT_EFFECTS_CAP_LOW_RUMBLE |
                         INPUT_NEXT_EFFECTS_CAP_HIGH_RUMBLE | INPUT_NEXT_EFFECTS_CAP_BODY_LEASE,
        .completion_capabilities = 0,
        .audio_instance_id = 0,
        .capabilities_revision = 1,
    };
    input_next_status target_status{INPUT_NEXT_STATUS_OK};
    input_next_status open_status{INPUT_NEXT_STATUS_OK};
    input_next_status query_context_status{INPUT_NEXT_STATUS_OK};
    input_next_status trigger_status{INPUT_NEXT_STATUS_OK};
    input_next_status trigger_clear_status{INPUT_NEXT_STATUS_OK};
    input_next_status led_status{INPUT_NEXT_STATUS_OK};
    input_next_status led_clear_status{INPUT_NEXT_STATUS_OK};
    input_next_status body_status{INPUT_NEXT_STATUS_OK};
    input_next_status body_clear_status{INPUT_NEXT_STATUS_OK};
    input_next_status close_status{INPUT_NEXT_STATUS_REVOKED};
    input_next_status ack_close_status{INPUT_NEXT_STATUS_OK};
    std::uint32_t close_disposition{INPUT_NEXT_EFFECTS_CLOSE_NORMAL};
    input_next_status progress_status{INPUT_NEXT_STATUS_OK};
    std::uint32_t query_context_state{INPUT_NEXT_EFFECTS_CONTEXT_OPEN};
    std::uint32_t body_route{INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC};
    std::uint64_t next_lease{400};
    std::uint64_t next_context{100};
    ReceiptFault receipt_fault{ReceiptFault::none};
    CloseReceiptFault close_receipt_fault{CloseReceiptFault::none};
    std::size_t target_calls{};
    std::size_t open_calls{};
    std::size_t query_context_calls{};
    std::size_t close_calls{};
    std::size_t ack_close_calls{};
    std::size_t progress_calls{};
    std::vector<TriggerCall> triggers;
    std::vector<LedCall> leds;
    std::vector<std::pair<input_next_device_target_v1, std::uint32_t>> trigger_clears;
    std::vector<input_next_effects_clear_v1> trigger_clear_requests;
    std::vector<input_next_effects_clear_v1> led_clear_requests;
    std::vector<BodyCall> body_calls;
    std::vector<BodyClearCall> body_clears;
    std::vector<input_next_effects_open_v1> opens;
    std::vector<input_next_effects_context_v1> context_queries;
    std::vector<input_next_effects_close_v1> closes;
    std::vector<input_next_effects_close_receipt_v1> acknowledgements;
    std::vector<input_next_effects_progress_query_v1> progress_queries;
    std::vector<FakeContext> contexts;
    std::function<void()> after_body_submit;
    std::function<void()> after_trigger_submit;
    std::function<void()> after_trigger_clear;
    std::function<void()> after_led_submit;
    std::function<void()> after_led_clear;

    input_next_status resolve_target(const input_next_device_selector_v1& selector,
        input_next_device_target_v1& out_target,
        input_next_effects_capabilities_v1& out_capabilities) noexcept override {
        ++target_calls;
        if (selector.struct_size != sizeof(selector) ||
            selector.kind != INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT || selector.xinput_slot > 3U ||
            selector.target.struct_size != sizeof(selector.target)) {
            return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
        }
        if (target_status != INPUT_NEXT_STATUS_OK) {
            return target_status;
        }
        auto candidate_target = target;
        candidate_target.device_key = 17U + selector.xinput_slot;
        const auto candidate_capabilities = capabilities;
        out_target = candidate_target;
        out_capabilities = candidate_capabilities;
        return INPUT_NEXT_STATUS_OK;
    }

    input_next_status open_context(const input_next_effects_open_v1& request,
        input_next_effects_context_v1& context) noexcept override {
        return inputnext::test::run_noexcept("OutputSession FakeOutput::open_context", [&] {
            ++open_calls;
            opens.push_back(request);
            if (open_status != INPUT_NEXT_STATUS_OK) {
                return open_status;
            }
            FakeContext record{};
            record.context.struct_size = sizeof(record.context);
            record.context.runtime_id[0] = target.runtime_id[0];
            record.context.runtime_id[1] = target.runtime_id[1];
            record.context.id = next_context++;
            record.target = request.target;
            record.state = INPUT_NEXT_EFFECTS_CONTEXT_OPEN;
            context = record.context;
            contexts.push_back(record);
            return open_status;
        });
    }

    input_next_status query_context(const input_next_effects_context_v1& context,
        input_next_effects_context_info_v1& info) noexcept override {
        return inputnext::test::run_noexcept("OutputSession FakeOutput::query_context", [&] {
            ++query_context_calls;
            context_queries.push_back(context);
            auto* record = find_context(context);
            if (record == nullptr) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            if (query_context_status != INPUT_NEXT_STATUS_OK &&
                query_context_status != INPUT_NEXT_STATUS_REVOKED) {
                return query_context_status;
            }
            if (record->state == INPUT_NEXT_EFFECTS_CONTEXT_ATTACHING) {
                record->state = query_context_state;
            }
            info = {};
            info.struct_size = sizeof(info);
            info.state = record->state;
            info.context = record->context;
            return query_context_status;
        });
    }

    input_next_status submit(const input_next_effects_request_v1& request,
        input_next_effects_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("OutputSession FakeOutput::submit", [&] {
            auto* record = find_context(request.context);
            if (record == nullptr || record->state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            const bool is_led = (request.channel_mask & INPUT_NEXT_EFFECTS_LEDS) != 0;
            record_effect_submission(request, *record, is_led);
            const auto status = is_led ? led_status : trigger_status;
            if (status != INPUT_NEXT_STATUS_OK) {
                run_effect_submit_checkpoint(is_led);
                return status;
            }
            const auto accept_status = accept_effect_submission(request, *record, receipt, is_led);
            if (accept_status != INPUT_NEXT_STATUS_OK) {
                return accept_status;
            }
            run_effect_submit_checkpoint(is_led);
            return INPUT_NEXT_STATUS_OK;
        });
    }

    input_next_status clear(const input_next_effects_clear_v1& request) noexcept override {
        return inputnext::test::run_noexcept("OutputSession FakeOutput::clear", [&] {
            auto* record = find_context(request.context);
            const bool is_led = (request.channel_mask & INPUT_NEXT_EFFECTS_LEDS) != 0;
            record_effect_clear(request, record, is_led);
            run_effect_clear_checkpoint(is_led);
            const auto clear_status = is_led ? led_clear_status : trigger_clear_status;
            if (clear_status != INPUT_NEXT_STATUS_OK) {
                return clear_status;
            }
            return apply_effect_clear(request, record);
        });
    }

    input_next_status submit_body(const input_next_effects_body_request_v1& request,
        input_next_effects_body_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("OutputSession FakeOutput::submit_body", [&] {
            if (request.struct_size != sizeof(request) ||
                request.pcm.struct_size != sizeof(request.pcm)) {
                return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
            }
            auto* record = find_context(request.context);
            if (record == nullptr || record->state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            BodyCall call{};
            static_cast<input_next_effects_body_request_v1&>(call.request) = request;
            call.request.target = record->target;
            call.request.pcm.samples = nullptr;
            if (request.pcm.samples != nullptr && request.pcm.frame_count != 0) {
                const auto sample_count =
                    static_cast<std::size_t>(request.pcm.frame_count) * request.pcm.channels;
                call.samples.assign(request.pcm.samples, request.pcm.samples + sample_count);
            }
            body_calls.push_back(std::move(call));
            if (after_body_submit) {
                after_body_submit();
            }
            if (body_status != INPUT_NEXT_STATUS_OK) {
                return body_status;
            }

            if (request.lease_id == 0) {
                if (request.sequence != 1) {
                    return INPUT_NEXT_STATUS_STALE_SEQUENCE;
                }
                record->body_sequence = 0;
            }
            if (request.sequence == 0 || request.sequence <= record->body_sequence) {
                return INPUT_NEXT_STATUS_STALE_SEQUENCE;
            }
            record->body_sequence = request.sequence;
            ++record->publication_revision;

            make_accepted_body_receipt(request, *record, receipt);
            return INPUT_NEXT_STATUS_OK;
        });
    }

    input_next_status clear_body(
        const input_next_effects_body_clear_v1& request) noexcept override {
        return inputnext::test::run_noexcept("OutputSession FakeOutput::clear_body", [&] {
            if (body_clear_status != INPUT_NEXT_STATUS_OK) {
                BodyClearCall call{};
                static_cast<input_next_effects_body_clear_v1&>(call) = request;
                if (const auto* record = find_context(request.context)) {
                    call.target = record->target;
                }
                body_clears.push_back(call);
                return body_clear_status;
            }
            auto* record = find_context(request.context);
            if (record == nullptr || record->state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            BodyClearCall call{};
            static_cast<input_next_effects_body_clear_v1&>(call) = request;
            call.target = record->target;
            body_clears.push_back(call);
            if (request.lease_id != record->body_lease ||
                request.sequence != record->body_sequence + 1) {
                return INPUT_NEXT_STATUS_STALE_LEASE;
            }
            record->body_lease = 0;
            ++record->publication_revision;
            return INPUT_NEXT_STATUS_OK;
        });
    }

    input_next_status close_context(const input_next_effects_close_v1& request,
        input_next_effects_close_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("OutputSession FakeOutput::close_context", [&] {
            ++close_calls;
            closes.push_back(request);
            auto* record = find_context(request.context);
            if (record == nullptr) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            const auto status = close_status;
            if (status == INPUT_NEXT_STATUS_TIMED_OUT_UNKNOWN) {
                record->state = INPUT_NEXT_EFFECTS_CONTEXT_REVOKED;
                record->close_receipt = {};
                record->close_receipt.struct_size = sizeof(record->close_receipt);
                record->close_receipt.disposition = INPUT_NEXT_EFFECTS_CLOSE_NORMAL;
                record->close_receipt.context = record->context;
                record->close_receipt.target = record->target;
                record->close_receipt.close_request_id = request.close_request_id;
                record->close_receipt.publication_revision = ++record->publication_revision;
                record->has_close_receipt = true;
                return status;
            }
            if (status != INPUT_NEXT_STATUS_REVOKED) {
                return status;
            }
            if (request.close_request_id != request.context.id) {
                return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
            }
            record->state = INPUT_NEXT_EFFECTS_CONTEXT_REVOKED;
            if (!record->has_close_receipt) {
                record->close_receipt = {};
                record->close_receipt.struct_size = sizeof(record->close_receipt);
                record->close_receipt.disposition = close_disposition;
                record->close_receipt.context = record->context;
                record->close_receipt.target = record->target;
                record->close_receipt.close_request_id = request.close_request_id;
                record->close_receipt.publication_revision =
                    close_disposition == INPUT_NEXT_EFFECTS_CLOSE_CANCELLED_BEFORE_CREATION
                        ? 0
                        : ++record->publication_revision;
                record->has_close_receipt = true;
            }
            receipt = record->close_receipt;
            switch (close_receipt_fault) {
            case CloseReceiptFault::none:
                break;
            case CloseReceiptFault::size:
                --receipt.struct_size;
                break;
            case CloseReceiptFault::context:
                ++receipt.context.id;
                break;
            case CloseReceiptFault::target:
                ++receipt.target.connection_id;
                break;
            case CloseReceiptFault::close_request_id:
                ++receipt.close_request_id;
                break;
            case CloseReceiptFault::unknown_disposition:
                receipt.disposition = UINT32_MAX;
                break;
            case CloseReceiptFault::zero_revision:
                receipt.publication_revision = 0;
                break;
            case CloseReceiptFault::nonzero_revision:
                receipt.publication_revision = 1;
                break;
            }
            return status;
        });
    }

    input_next_status ack_close(
        const input_next_effects_close_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("OutputSession FakeOutput::ack_close", [&] {
            ++ack_close_calls;
            acknowledgements.push_back(receipt);
            if (ack_close_status != INPUT_NEXT_STATUS_OK) {
                return ack_close_status;
            }
            const auto found = std::ranges::find_if(contexts, [&](const auto& record) {
                return record.has_close_receipt &&
                       same_close_receipt(record.close_receipt, receipt);
            });
            if (found == contexts.end()) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            contexts.erase(found);
            return INPUT_NEXT_STATUS_OK;
        });
    }

    input_next_status query_progress(const input_next_effects_progress_query_v1& query,
        input_next_effects_progress_v1& progress) noexcept override {
        return inputnext::test::run_noexcept("OutputSession FakeOutput::query_progress", [&] {
            ++progress_calls;
            progress_queries.push_back(query);
            if (progress_status == INPUT_NEXT_STATUS_OK) {
                progress = {};
                progress.struct_size = sizeof(progress);
                progress.target = query.target;
                progress.requested_publication_revision = query.publication_revision;
                progress.current_publication_revision = query.publication_revision;
                progress.stage = INPUT_NEXT_EFFECTS_PROGRESS_CURRENT_COMPLETED;
            }
            return progress_status;
        });
    }

    static bool same_context(const input_next_effects_context_v1& left,
        const input_next_effects_context_v1& right) noexcept {
        return left.struct_size == right.struct_size && left.reserved == right.reserved &&
               left.runtime_id[0] == right.runtime_id[0] &&
               left.runtime_id[1] == right.runtime_id[1] && left.id == right.id;
    }

    FakeContext* find_context(const input_next_effects_context_v1& context) noexcept {
        const auto found = std::ranges::find_if(contexts,
            [&](const auto& record) { return same_context(record.context, context); });
        return found == contexts.end() ? nullptr : &*found;
    }

    void disable_body_leases() noexcept {
        capabilities.feature_flags &= ~INPUT_NEXT_EFFECTS_CAP_BODY_LEASE;
    }

private:
    void record_effect_submission(const input_next_effects_request_v1& request,
        const FakeContext& record,
        bool is_led) {
        if (is_led) {
            LedCall call{};
            static_cast<input_next_effects_request_v1&>(call) = request;
            call.target = record.target;
            leds.push_back(call);
            return;
        }
        TriggerCall call{};
        static_cast<input_next_effects_request_v1&>(call) = request;
        call.target = record.target;
        triggers.push_back(call);
    }

    input_next_status accept_effect_submission(const input_next_effects_request_v1& request,
        FakeContext& record,
        input_next_effects_receipt_v1& receipt,
        bool is_led) noexcept {
        if (request.mutation_sequence <= record.last_mutation_sequence) {
            return INPUT_NEXT_STATUS_STALE_SEQUENCE;
        }
        record.last_mutation_sequence = request.mutation_sequence;
        ++record.publication_revision;
        receipt = {};
        receipt.struct_size = sizeof(receipt);
        receipt.accepted_mask = request.channel_mask;
        receipt.context = request.context;
        receipt.mutation_sequence = request.mutation_sequence;
        receipt.publication_revision = record.publication_revision;
        for (std::size_t index = 0; index < record.channel_revisions.size(); ++index) {
            const auto channel = UINT32_C(1) << index;
            if ((request.channel_mask & channel) != 0) {
                receipt.channel_revisions[index] = ++record.channel_revisions[index];
            }
        }
        apply_receipt_fault(receipt, is_led);
        return INPUT_NEXT_STATUS_OK;
    }

    void apply_receipt_fault(input_next_effects_receipt_v1& receipt, bool is_led) const noexcept {
        if (receipt_fault == ReceiptFault::context) {
            ++receipt.context.id;
        } else if (receipt_fault == ReceiptFault::revision) {
            receipt.channel_revisions[is_led ? 3U : 0U] = 0;
        } else if (receipt_fault == ReceiptFault::size) {
            --receipt.struct_size;
        }
    }

    void run_effect_submit_checkpoint(bool is_led) {
        const auto& checkpoint = is_led ? after_led_submit : after_trigger_submit;
        if (checkpoint) {
            checkpoint();
        }
    }

    void record_effect_clear(const input_next_effects_clear_v1& request,
        const FakeContext* record,
        bool is_led) {
        if (record != nullptr && !is_led) {
            trigger_clears.emplace_back(record->target, request.channel_mask);
        }
        auto& requests = is_led ? led_clear_requests : trigger_clear_requests;
        requests.push_back(request);
    }

    void run_effect_clear_checkpoint(bool is_led) {
        const auto& checkpoint = is_led ? after_led_clear : after_trigger_clear;
        if (checkpoint) {
            checkpoint();
        }
    }

    static input_next_status apply_effect_clear(const input_next_effects_clear_v1& request,
        FakeContext* record) noexcept {
        if (record == nullptr || record->state != INPUT_NEXT_EFFECTS_CONTEXT_OPEN) {
            return INPUT_NEXT_STATUS_STALE_CONTEXT;
        }
        if (request.mutation_sequence <= record->last_mutation_sequence) {
            return INPUT_NEXT_STATUS_STALE_SEQUENCE;
        }
        for (std::size_t index = 0; index < record->channel_revisions.size(); ++index) {
            const auto channel = UINT32_C(1) << index;
            if ((request.channel_mask & channel) != 0 &&
                request.channel_revisions[index] != record->channel_revisions[index]) {
                return INPUT_NEXT_STATUS_STALE_EFFECT;
            }
        }
        record->last_mutation_sequence = request.mutation_sequence;
        ++record->publication_revision;
        for (std::size_t index = 0; index < record->channel_revisions.size(); ++index) {
            if ((request.channel_mask & (UINT32_C(1) << index)) != 0) {
                ++record->channel_revisions[index];
            }
        }
        return INPUT_NEXT_STATUS_OK;
    }

    void make_accepted_body_receipt(const input_next_effects_body_request_v1& request,
        FakeContext& record,
        input_next_effects_body_receipt_v1& receipt) noexcept {
        receipt = {};
        receipt.struct_size = sizeof(receipt);
        receipt.effective_route = request.route_preference == INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC
                                      ? INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC
                                      : body_route;
        receipt.context = request.context;
        receipt.target = record.target;
        receipt.lease_id = request.lease_id == 0 ? next_lease++ : request.lease_id;
        record.body_lease = receipt.lease_id;
        receipt.audio_instance_id = request.audio_instance_id;
        receipt.sequence = request.sequence;
        receipt.publication_revision = record.publication_revision;
        const bool no_fallback = receipt.effective_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD ||
                                 request.route_preference == INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC;
        receipt.fallback_reason = no_fallback ? INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE
                                              : INPUT_NEXT_EFFECTS_BODY_FALLBACK_NOT_READY;
        if (receipt_fault == ReceiptFault::context) {
            ++receipt.context.id;
        } else if (receipt_fault == ReceiptFault::target) {
            ++receipt.target.connection_id;
        } else if (receipt_fault == ReceiptFault::revision) {
            receipt.publication_revision = 0;
        } else if (receipt_fault == ReceiptFault::size) {
            --receipt.struct_size;
        }
    }
};

} // namespace inputnext::test::output_session
