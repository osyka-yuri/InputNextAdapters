#pragma once

#include <inputnext_adapters/output.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include "support/test_runner.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace fallout4_adapter::session_tests {

struct BodySubmission {
    input_next_effects_body_request_v1 request{};
    std::vector<float> samples;
    std::uint32_t route{};
};

class FakeOutput final : public inputnext::adapters::OutputPort {
public:
    input_next_device_target_v1 target{
        .struct_size = sizeof(target),
        .reserved = 0,
        .runtime_id = {1, 2},
        .device_key = 1,
        .connection_id = 1,
    };
    std::vector<input_next_effects_request_v1> triggers;
    std::vector<input_next_effects_request_v1> leds;
    std::vector<input_next_effects_clear_v1> clears;
    std::vector<BodySubmission> bodies;
    bool accept_hd_route{};
    input_next_effects_capabilities_v1 configured_capabilities{
        .struct_size = sizeof(configured_capabilities),
        .supported_channels = INPUT_NEXT_EFFECTS_ALL_CHANNELS,
        .feature_flags = INPUT_NEXT_EFFECTS_CAP_LEFT_TRIGGER |
                         INPUT_NEXT_EFFECTS_CAP_RIGHT_TRIGGER | INPUT_NEXT_EFFECTS_CAP_LOW_RUMBLE |
                         INPUT_NEXT_EFFECTS_CAP_HIGH_RUMBLE | INPUT_NEXT_EFFECTS_CAP_BODY_LEASE |
                         INPUT_NEXT_EFFECTS_CAP_LIGHTBAR,
        .completion_capabilities = 0,
        .audio_instance_id = 0,
        .capabilities_revision = 1,
    };
    unsigned trigger_clears{}, led_clears{}, body_clears{};
    unsigned target_calls{}, query_context_calls{}, open_calls{}, close_calls{}, ack_close_calls{};
    std::chrono::milliseconds next_get_target_delay{};
    input_next_status next_resolve_status{INPUT_NEXT_STATUS_OK};
    input_next_status open_status{INPUT_NEXT_STATUS_OK}, query_context_status{INPUT_NEXT_STATUS_OK},
        close_status{INPUT_NEXT_STATUS_REVOKED}, ack_close_status{INPUT_NEXT_STATUS_OK};
    std::uint32_t query_context_state{INPUT_NEXT_EFFECTS_CONTEXT_OPEN};
    std::uint64_t next_context{100};
    std::vector<std::pair<input_next_effects_context_v1, input_next_device_target_v1>> contexts;
    std::vector<input_next_effects_close_v1> close_requests;
    std::vector<input_next_effects_close_receipt_v1> close_receipts, acknowledged_close_receipts;
    std::vector<input_next_status> close_status_script, ack_close_status_script;

    input_next_status resolve_target(const input_next_device_selector_v1& selector,
        input_next_device_target_v1& result,
        input_next_effects_capabilities_v1& capabilities) noexcept override {
        ++target_calls;
        const auto delay = std::exchange(next_get_target_delay, std::chrono::milliseconds{});
        if (delay > std::chrono::milliseconds::zero()) {
            std::this_thread::sleep_for(delay);
        }
        if (selector.kind != INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT || selector.xinput_slot > 3U) {
            return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
        }
        const auto status = std::exchange(next_resolve_status, INPUT_NEXT_STATUS_OK);
        if (status != INPUT_NEXT_STATUS_OK) {
            return status;
        }
        result = target;
        capabilities = configured_capabilities;
        return INPUT_NEXT_STATUS_OK;
    }

    input_next_status open_context(const input_next_effects_open_v1& request,
        input_next_effects_context_v1& context) noexcept override {
        return inputnext::test::run_noexcept("Fallout 4 FakeOutput::open_context", [&] {
            ++open_calls;
            if (open_status != INPUT_NEXT_STATUS_OK) {
                return open_status;
            }

            input_next_effects_context_v1 opened{};
            opened.struct_size = sizeof(opened);
            opened.runtime_id[0] = request.target.runtime_id[0];
            opened.runtime_id[1] = request.target.runtime_id[1];
            opened.id = next_context;
            contexts.emplace_back(opened, request.target);
            ++next_context;
            context = opened;
            return INPUT_NEXT_STATUS_OK;
        });
    }

    input_next_status query_context(const input_next_effects_context_v1& context,
        input_next_effects_context_info_v1& info) noexcept override {
        ++query_context_calls;
        const auto found = find_context(context);
        if (found == contexts.end()) {
            return INPUT_NEXT_STATUS_STALE_CONTEXT;
        }
        if (context.struct_size != sizeof(context) || context.reserved != 0) {
            return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
        }
        const auto status = query_context_status;
        if (status != INPUT_NEXT_STATUS_OK && status != INPUT_NEXT_STATUS_REVOKED) {
            return status;
        }
        info = {};
        info.struct_size = sizeof(info);
        info.state = query_context_state;
        info.context = found->first;
        return status;
    }

    input_next_status submit(const input_next_effects_request_v1& request,
        input_next_effects_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("Fallout 4 FakeOutput::submit", [&] {
            triggers.push_back(request);
            if ((request.channel_mask & INPUT_NEXT_EFFECTS_LEDS) != 0U) {
                leds.push_back(request);
            }
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
        });
    }

    input_next_status clear(const input_next_effects_clear_v1& request) noexcept override {
        return inputnext::test::run_noexcept("Fallout 4 FakeOutput::clear", [&] {
            clears.push_back(request);
            if ((request.channel_mask & INPUT_NEXT_EFFECTS_LEDS) != 0U) {
                ++led_clears;
            }
            if ((request.channel_mask &
                    (INPUT_NEXT_EFFECTS_LEFT_TRIGGER | INPUT_NEXT_EFFECTS_RIGHT_TRIGGER)) != 0U) {
                ++trigger_clears;
            }
            return INPUT_NEXT_STATUS_OK;
        });
    }

    input_next_status submit_body(const input_next_effects_body_request_v1& request,
        input_next_effects_body_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("Fallout 4 FakeOutput::submit_body", [&] {
            const auto found = find_context(request.context);
            if (found == contexts.end()) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            if (request.route_preference != INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC &&
                request.route_preference != INPUT_NEXT_EFFECTS_BODY_ROUTE_PREFER_HD) {
                return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
            }

            std::vector<float> copied_samples;
            if (request.pcm.samples != nullptr && request.pcm.frame_count != 0 &&
                request.pcm.channels != 0) {
                const auto frames = static_cast<std::size_t>(request.pcm.frame_count);
                const auto channels = static_cast<std::size_t>(request.pcm.channels);
                if (frames > std::numeric_limits<std::size_t>::max() / channels) {
                    return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
                }
                const auto sample_count = frames * channels;
                copied_samples.assign(request.pcm.samples, request.pcm.samples + sample_count);
            }

            auto copied = request;
            copied.pcm.samples = nullptr;
            const input_next_effects_body_receipt_v1 accepted{
                .struct_size = sizeof(receipt),
                .effective_route = accept_hd_route ? INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD
                                                   : INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC,
                .context = request.context,
                .target = found->second,
                .lease_id = request.lease_id == 0 ? 9 : request.lease_id,
                .audio_instance_id = request.audio_instance_id,
                .sequence = request.sequence,
                .publication_revision = request.sequence,
                .fallback_reason = accept_hd_route || request.route_preference ==
                                                          INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC
                                       ? INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE
                                       : INPUT_NEXT_EFFECTS_BODY_FALLBACK_NOT_READY,
                .reserved = 0,
            };
            bodies.push_back(BodySubmission{.request = copied,
                .samples = std::move(copied_samples),
                .route = accepted.effective_route});
            receipt = accepted;
            return INPUT_NEXT_STATUS_OK;
        });
    }

    input_next_status clear_body(
        [[maybe_unused]] const input_next_effects_body_clear_v1& request) noexcept override {
        ++body_clears;
        return INPUT_NEXT_STATUS_OK;
    }

    input_next_status close_context(const input_next_effects_close_v1& request,
        input_next_effects_close_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("Fallout 4 FakeOutput::close_context", [&] {
            close_requests.push_back(request);
            const auto status = close_calls < close_status_script.size()
                                    ? close_status_script[close_calls]
                                    : close_status;
            ++close_calls;
            const auto found = find_context(request.context);
            if (found == contexts.end()) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            if (!same_context(found->first, request.context) ||
                request.struct_size != sizeof(request) || request.reserved != 0 ||
                request.close_request_id != request.context.id) {
                return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
            }
            if (status != INPUT_NEXT_STATUS_REVOKED) {
                return status;
            }
            input_next_effects_close_receipt_v1 closed{};
            closed.struct_size = sizeof(closed);
            closed.disposition = INPUT_NEXT_EFFECTS_CLOSE_NORMAL;
            closed.context = request.context;
            closed.target = found->second;
            closed.close_request_id = request.close_request_id;
            closed.publication_revision = request.close_request_id + 1;
            close_receipts.push_back(closed);
            receipt = closed;
            return status;
        });
    }

    input_next_status ack_close(
        const input_next_effects_close_receipt_v1& receipt) noexcept override {
        return inputnext::test::run_noexcept("Fallout 4 FakeOutput::ack_close", [&] {
            acknowledged_close_receipts.push_back(receipt);
            const auto status = ack_close_calls < ack_close_status_script.size()
                                    ? ack_close_status_script[ack_close_calls]
                                    : ack_close_status;
            ++ack_close_calls;

            const auto known_receipt = std::ranges::find_if(close_receipts,
                [&](const auto& candidate) { return same_receipt(candidate, receipt); });
            if (known_receipt == close_receipts.end()) {
                return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
            }
            const auto found = find_context(receipt.context);
            if (found == contexts.end()) {
                return INPUT_NEXT_STATUS_STALE_CONTEXT;
            }
            if (!same_context(found->first, receipt.context) ||
                !same_target(found->second, receipt.target)) {
                return INPUT_NEXT_STATUS_INVALID_ARGUMENT;
            }
            if (status == INPUT_NEXT_STATUS_OK || status == INPUT_NEXT_STATUS_STALE_CONTEXT) {
                contexts.erase(found);
            }
            return status;
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

private:
    using ContextIterator = decltype(contexts)::const_iterator;

    [[nodiscard]] ContextIterator find_context(
        const input_next_effects_context_v1& context) const noexcept {
        return std::ranges::find_if(contexts, [&](const auto& record) {
            return record.first.id == context.id &&
                   record.first.runtime_id[0] == context.runtime_id[0] &&
                   record.first.runtime_id[1] == context.runtime_id[1];
        });
    }

    [[nodiscard]] static bool same_context(const input_next_effects_context_v1& left,
        const input_next_effects_context_v1& right) noexcept {
        return left.struct_size == right.struct_size && left.reserved == right.reserved &&
               left.runtime_id[0] == right.runtime_id[0] &&
               left.runtime_id[1] == right.runtime_id[1] && left.id == right.id;
    }

    [[nodiscard]] static bool same_target(const input_next_device_target_v1& left,
        const input_next_device_target_v1& right) noexcept {
        return left.struct_size == right.struct_size && left.reserved == right.reserved &&
               left.runtime_id[0] == right.runtime_id[0] &&
               left.runtime_id[1] == right.runtime_id[1] && left.device_key == right.device_key &&
               left.connection_id == right.connection_id;
    }

    [[nodiscard]] static bool same_receipt(const input_next_effects_close_receipt_v1& left,
        const input_next_effects_close_receipt_v1& right) noexcept {
        return left.struct_size == right.struct_size && left.disposition == right.disposition &&
               same_context(left.context, right.context) &&
               same_target(left.target, right.target) &&
               left.close_request_id == right.close_request_id &&
               left.publication_revision == right.publication_revision;
    }
};

} // namespace fallout4_adapter::session_tests
