#pragma once

#include "support/test_runner.hpp"

#include <inputnext_adapters/output.hpp>
#include <skyrim_adapter/model.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace skyrim_adapter::session_test {
using inputnext::adapters::OutputPort;

struct Clear {
    input_next_effects_context_v1 context{};
    input_next_device_target_v1 target{};
    std::uint32_t mask{};
};

inline bool same_device_target(const input_next_device_target_v1& left,
    const input_next_device_target_v1& right) noexcept {
    return left.struct_size == right.struct_size && left.reserved == right.reserved &&
           left.runtime_id[0] == right.runtime_id[0] && left.runtime_id[1] == right.runtime_id[1] &&
           left.device_key == right.device_key && left.connection_id == right.connection_id;
}

inline bool same_effects_context(const input_next_effects_context_v1& left,
    const input_next_effects_context_v1& right) noexcept {
    return left.struct_size == right.struct_size && left.reserved == right.reserved &&
           left.runtime_id[0] == right.runtime_id[0] && left.runtime_id[1] == right.runtime_id[1] &&
           left.id == right.id;
}

struct BodySubmission {
    struct Request : input_next_effects_body_request_v1 {
        input_next_device_target_v1 target{};
    } request{};

    std::vector<float> samples;
};

struct TriggerSubmission : input_next_effects_request_v1 {
    input_next_device_target_v1 target{};
};

struct BodyClear : input_next_effects_body_clear_v1 {
    input_next_device_target_v1 target{};
};

enum class ReceiptFault : std::uint8_t {
    none,
    size,
    reserved,
    target,
    instance,
    sequence,
    route_reason
};

class FakeOutput final : public OutputPort {
public:
    input_next_device_target_v1 target{
        .struct_size = sizeof(target),
        .reserved = 0,
        .runtime_id = {1, 2},
        .device_key = 11,
        .connection_id = 22,
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
    input_next_status target_status{INPUT_NEXT_STATUS_OK}, submit_status{INPUT_NEXT_STATUS_OK},
        clear_status{INPUT_NEXT_STATUS_OK}, body_submit_status{INPUT_NEXT_STATUS_OK},
        body_clear_status{INPUT_NEXT_STATUS_OK}, close_status{INPUT_NEXT_STATUS_REVOKED},
        ack_close_status{INPUT_NEXT_STATUS_OK}, open_status{INPUT_NEXT_STATUS_OK},
        query_context_status{INPUT_NEXT_STATUS_OK};
    std::uint32_t query_context_state{INPUT_NEXT_EFFECTS_CONTEXT_OPEN};
    std::vector<TriggerSubmission> submissions;
    std::vector<Clear> clears;
    std::vector<BodySubmission> body_submissions;
    std::vector<BodyClear> body_clears;
    std::vector<input_next_effects_close_v1> close_requests;
    std::vector<input_next_effects_close_receipt_v1> close_receipts;
    std::vector<input_next_effects_close_receipt_v1> acknowledged_close_receipts;
    std::vector<input_next_status> close_status_script;
    std::vector<input_next_status> ack_close_status_script;
    std::function<void()> on_query;
    std::function<void()> on_body_submit;
    std::function<void()> on_body_clear;
    std::size_t target_queries{};
    std::size_t open_calls{};
    std::size_t query_context_calls{};
    std::size_t close_calls{};
    std::size_t ack_close_calls{};
    std::uint64_t active_body_lease{};
    std::uint64_t last_accepted_body_lease{};
    std::uint64_t next_body_lease{101};
    std::uint64_t next_context{100};
    std::vector<std::pair<input_next_effects_context_v1, input_next_device_target_v1>> contexts;
    MonoTime last_body_accepted_at;
    std::uint32_t last_body_ttl_ms{};
    std::uint32_t body_route{INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC};
    ReceiptFault receipt_fault{ReceiptFault::none};

    input_next_status resolve_target(const input_next_device_selector_v1& selector,
        input_next_device_target_v1& out,
        input_next_effects_capabilities_v1& caps) noexcept override {
        return inputnext::test::run_noexcept("Skyrim FakeOutput::resolve_target", [&] {
            ++target_queries;
            if (on_query) {
                on_query();
            }
            if (selector.kind != INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT ||
                selector.xinput_slot > 3U) {
                return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
            }
            if (target_status != INPUT_NEXT_STATUS_OK) {
                return target_status;
            }
            out = target;
            caps = capabilities;
            return INPUT_NEXT_STATUS_OK;
        });
    }

    input_next_status open_context(const input_next_effects_open_v1& request,
        input_next_effects_context_v1& context) noexcept override {
        return inputnext::test::run_noexcept("Skyrim FakeOutput::open_context", [&] {
            ++open_calls;
            if (open_status != INPUT_NEXT_STATUS_OK) {
                return open_status;
            }
            context = {};
            context.struct_size = sizeof(context);
            context.runtime_id[0] = request.target.runtime_id[0];
            context.runtime_id[1] = request.target.runtime_id[1];
            context.id = next_context++;
            contexts.emplace_back(context, request.target);
            return INPUT_NEXT_STATUS_OK;
        });
    }

    input_next_status query_context(const input_next_effects_context_v1& context,
        input_next_effects_context_info_v1& info) noexcept override {
        ++query_context_calls;
        const auto found =
            std::ranges::find_if(contexts.begin(), contexts.end(), [&](const auto& record) {
                return record.first.id == context.id &&
                       record.first.runtime_id[0] == context.runtime_id[0] &&
                       record.first.runtime_id[1] == context.runtime_id[1];
            });
        if (found == contexts.end()) {
            return INPUT_NEXT_STATUS_STALE_CONTEXT;
        }
        if (query_context_status != INPUT_NEXT_STATUS_OK &&
            query_context_status != INPUT_NEXT_STATUS_REVOKED) {
            return query_context_status;
        }
        info = {};
        info.struct_size = sizeof(info);
        info.state = query_context_state;
        info.context = context;
        return query_context_status;
    }

    input_next_status submit(const input_next_effects_request_v1& request,
        input_next_effects_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("Skyrim FakeOutput::submit", [&] {
            const auto found = std::ranges::find_if(contexts.begin(),
                contexts.end(),
                [&](const auto& record) { return record.first.id == request.context.id; });
            if (found == contexts.end()) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            TriggerSubmission submission{};
            static_cast<input_next_effects_request_v1&>(submission) = request;
            submission.target = found->second;
            submissions.push_back(submission);
            if (submit_status == INPUT_NEXT_STATUS_OK) {
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
            }
            return submit_status;
        });
    }

    input_next_status clear(const input_next_effects_clear_v1& request) noexcept override {
        return inputnext::test::run_noexcept("Skyrim FakeOutput::clear", [&] {
            const auto found = std::ranges::find_if(contexts.begin(),
                contexts.end(),
                [&](const auto& record) { return record.first.id == request.context.id; });
            if (found == contexts.end()) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            clears.push_back({.context = request.context,
                .target = found->second,
                .mask = request.channel_mask});
            return clear_status;
        });
    }

    input_next_status submit_body(const input_next_effects_body_request_v1& request,
        input_next_effects_body_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("Skyrim FakeOutput::submit_body", [&] {
            if (on_body_submit) {
                on_body_submit();
            }
            BodySubmission submission{};
            static_cast<input_next_effects_body_request_v1&>(submission.request) = request;
            const auto found = std::ranges::find_if(contexts.begin(),
                contexts.end(),
                [&](const auto& record) { return record.first.id == request.context.id; });
            if (found == contexts.end()) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            submission.request.target = found->second;
            submission.request.pcm.samples = nullptr;
            if (request.pcm.samples && request.pcm.frame_count > 0) {
                const auto count =
                    static_cast<std::size_t>(request.pcm.frame_count) * request.pcm.channels;
                submission.samples.assign(request.pcm.samples, request.pcm.samples + count);
            }
            body_submissions.push_back(std::move(submission));
            if (body_submit_status != INPUT_NEXT_STATUS_OK) {
                return body_submit_status;
            }

            receipt = {};
            receipt.struct_size = sizeof(receipt);
            receipt.effective_route =
                request.route_preference == INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC
                    ? INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC
                    : body_route;
            if (request.lease_id == 0) {
                active_body_lease = next_body_lease++;
                receipt.lease_id = active_body_lease;
            } else {
                receipt.lease_id = request.lease_id;
                active_body_lease = request.lease_id;
            }
            last_accepted_body_lease = active_body_lease;
            last_body_accepted_at = std::chrono::steady_clock::now();
            last_body_ttl_ms = request.ttl_ms;
            receipt.context = request.context;
            receipt.target = found->second;
            receipt.audio_instance_id = request.audio_instance_id;
            receipt.sequence = request.sequence;
            receipt.publication_revision = request.sequence;
            const bool no_fallback =
                receipt.effective_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD ||
                request.route_preference == INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC;

            receipt.fallback_reason = no_fallback ? INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE
                                                  : INPUT_NEXT_EFFECTS_BODY_FALLBACK_NOT_READY;
            switch (receipt_fault) {
            case ReceiptFault::none:
                break;
            case ReceiptFault::size:
                --receipt.struct_size;
                break;
            case ReceiptFault::reserved:
                receipt.reserved = 1;
                break;
            case ReceiptFault::target:
                ++receipt.target.connection_id;
                break;
            case ReceiptFault::instance:
                ++receipt.audio_instance_id;
                break;
            case ReceiptFault::sequence:
                ++receipt.sequence;
                break;
            case ReceiptFault::route_reason:
                receipt.effective_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
                receipt.fallback_reason = INPUT_NEXT_EFFECTS_BODY_FALLBACK_NOT_READY;
                break;
            }
            return INPUT_NEXT_STATUS_OK;
        });
    }

    input_next_status clear_body(
        const input_next_effects_body_clear_v1& request) noexcept override {
        return inputnext::test::run_noexcept("Skyrim FakeOutput::clear_body", [&] {
            if (on_body_clear) {
                on_body_clear();
            }
            const auto found = std::ranges::find_if(contexts.begin(),
                contexts.end(),
                [&](const auto& record) { return record.first.id == request.context.id; });
            BodyClear clear{};
            static_cast<input_next_effects_body_clear_v1&>(clear) = request;
            if (found != contexts.end()) {
                clear.target = found->second;
            }
            body_clears.push_back(clear);
            if (body_clear_status == INPUT_NEXT_STATUS_OK &&
                request.lease_id == active_body_lease) {
                active_body_lease = 0;
            }
            return body_clear_status;
        });
    }

    input_next_status close_context(const input_next_effects_close_v1& request,
        input_next_effects_close_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("Skyrim FakeOutput::close_context", [&] {
            close_requests.push_back(request);
            const auto status = close_calls < close_status_script.size()
                                    ? close_status_script[close_calls]
                                    : close_status;
            ++close_calls;
            const auto found = std::ranges::find_if(contexts.begin(),
                contexts.end(),
                [&](const auto& record) { return record.first.id == request.context.id; });
            if (found == contexts.end()) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            if (status == INPUT_NEXT_STATUS_REVOKED) {
                receipt = {};
                receipt.struct_size = sizeof(receipt);
                receipt.disposition = INPUT_NEXT_EFFECTS_CLOSE_NORMAL;
                receipt.context = request.context;
                receipt.target = found->second;
                receipt.close_request_id = request.close_request_id;
                receipt.publication_revision = request.close_request_id + 1;
                close_receipts.push_back(receipt);
            }
            return status;
        });
    }

    input_next_status ack_close(
        const input_next_effects_close_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("Skyrim FakeOutput::ack_close", [&] {
            acknowledged_close_receipts.push_back(receipt);
            const auto status = ack_close_calls < ack_close_status_script.size()
                                    ? ack_close_status_script[ack_close_calls]
                                    : ack_close_status;
            ++ack_close_calls;
            if (status != INPUT_NEXT_STATUS_OK) {
                return status;
            }

            const auto accepted_receipt = std::ranges::find_if(close_receipts.begin(),
                close_receipts.end(),
                [&](const auto& accepted) {
                    return accepted.struct_size == receipt.struct_size &&
                           same_effects_context(accepted.context, receipt.context) &&
                           same_device_target(accepted.target, receipt.target) &&
                           accepted.close_request_id == receipt.close_request_id &&
                           accepted.publication_revision == receipt.publication_revision &&
                           accepted.disposition == receipt.disposition;
                });
            if (accepted_receipt == close_receipts.end()) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }

            const auto context_record =
                std::ranges::find_if(contexts.begin(), contexts.end(), [&](const auto& record) {
                    return same_effects_context(record.first, receipt.context) &&
                           same_device_target(record.second, receipt.target);
                });
            if (context_record == contexts.end()) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            contexts.erase(context_record);
            return INPUT_NEXT_STATUS_OK;
        });
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

inline bool has_acked_close(const FakeOutput& output,
    const input_next_effects_context_v1& context,
    const input_next_device_target_v1& target) noexcept {
    for (const auto& receipt : output.close_receipts) {
        if (!same_effects_context(receipt.context, context) ||
            !same_device_target(receipt.target, target) || receipt.close_request_id != context.id) {
            continue;
        }
        if (std::ranges::any_of(output.acknowledged_close_receipts, [&](const auto& acknowledged) {
                return same_effects_context(acknowledged.context, receipt.context) &&
                       same_device_target(acknowledged.target, receipt.target) &&
                       acknowledged.close_request_id == receipt.close_request_id &&
                       acknowledged.publication_revision == receipt.publication_revision;
            })) {
            return true;
        }
    }
    return false;
}

inline void advertise_hd(FakeOutput& output, std::uint64_t audio_instance_id) noexcept {
    output.capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.capabilities.audio_instance_id = audio_instance_id;
}
} // namespace skyrim_adapter::session_test

namespace skyrim_adapter {
using session_test::advertise_hd;
using session_test::BodyClear;
using session_test::BodySubmission;
using session_test::Clear;
using session_test::FakeOutput;
using session_test::ReceiptFault;
using session_test::same_device_target;
using session_test::same_effects_context;
using session_test::TriggerSubmission;
} // namespace skyrim_adapter
