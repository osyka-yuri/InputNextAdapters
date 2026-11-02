#include "fake_output.hpp"
#include "support.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <input_next_core.h>
#include <input_next_effects.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fallout4_adapter::session_tests {
namespace {
input_next_effects_context_v1 open_fake_context(FakeOutput& output) {
    input_next_effects_open_v1 request{};
    request.struct_size = sizeof(request);
    request.target = output.target;
    input_next_effects_context_v1 context{};
    REQUIRE(output.open_context(request, context) == INPUT_NEXT_STATUS_OK);
    return context;
}
} // namespace

TEST_CASE("fallout4.session.body_submissions_keep_requests_and_owned_samples_together",
    "[component][fallout4][session]") {
    FakeOutput output;
    const auto context = open_fake_context(output);

    std::array<float, 4> samples{};
    input_next_effects_body_request_v1 request{};
    request.struct_size = sizeof(request);
    request.context = context;
    request.ttl_ms = 20;
    request.route_preference = INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC;
    request.pcm_command = INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE;
    request.pcm.struct_size = sizeof(request.pcm);
    request.pcm.channels = 2;
    request.pcm.frame_count = 2;
    request.pcm.sample_rate = 48000;
    request.pcm.gain = 1.0F;
    request.pcm.samples = samples.data();
    constexpr std::uint64_t count = 128;
    for (std::uint64_t sequence = 1; sequence <= count; ++sequence) {
        request.sequence = sequence;
        const auto amplitude = static_cast<float>(sequence) / 256.0F;
        samples = {amplitude, -amplitude, amplitude, -amplitude};
        input_next_effects_body_receipt_v1 receipt{};
        REQUIRE(output.submit_body(request, receipt) == INPUT_NEXT_STATUS_OK);
    }
    samples.fill(0.0F);
    REQUIRE(output.bodies.size() == count);
    for (std::size_t index = 0; index < output.bodies.size(); ++index) {
        CAPTURE(index);
        const auto& body = output.bodies[index];
        const auto sequence = index + 1;
        const auto amplitude = static_cast<float>(sequence) / 256.0F;
        CHECK(body.request.sequence == sequence);
        CHECK(body.request.pcm.samples == nullptr);
        CHECK(body.samples == std::vector<float>{amplitude, -amplitude, amplitude, -amplitude});
        CHECK(body.route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC);
    }
}

TEST_CASE("fallout4.session.failed_target_resolution_preserves_outputs",
    "[component][fallout4][session]") {
    input_next_device_selector_v1 selector{};
    selector.struct_size = sizeof(selector);
    selector.target.struct_size = sizeof(selector.target);
    selector.kind = INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT;
    selector.xinput_slot = 0;

    FakeOutput resolving;
    resolving.next_resolve_status = INPUT_NEXT_STATUS_BUSY;
    input_next_device_target_v1 unresolved_target{
        .struct_size = sizeof(unresolved_target),
        .reserved = 17,
        .runtime_id = {91, 92},
        .device_key = 93,
        .connection_id = 94,
    };
    input_next_effects_capabilities_v1 unresolved_capabilities{
        .struct_size = sizeof(unresolved_capabilities),
        .supported_channels = 95,
        .feature_flags = 96,
        .completion_capabilities = 97,
        .audio_instance_id = 98,
        .capabilities_revision = 99,
    };
    require(resolving.resolve_target(selector, unresolved_target, unresolved_capabilities) ==
                INPUT_NEXT_STATUS_BUSY,
        "BUSY target resolution is preserved");
    require(unresolved_target.device_key == 93 && unresolved_target.connection_id == 94 &&
                unresolved_capabilities.supported_channels == 95 &&
                unresolved_capabilities.capabilities_revision == 99,
        "a failed target resolution preserves caller-owned outputs");
}

TEST_CASE("fallout4.session.failed_open_preserves_outputs_without_allocation",
    "[component][fallout4][session]") {
    constexpr std::array failed_open_statuses{
        INPUT_NEXT_STATUS_PENDING,
        INPUT_NEXT_STATUS_BUSY,
        INPUT_NEXT_STATUS_UNAVAILABLE,
    };
    for (const auto failure : failed_open_statuses) {
        FakeOutput opening;
        opening.open_status = failure;
        input_next_effects_open_v1 request{};
        request.struct_size = sizeof(request);
        request.target = opening.target;
        input_next_effects_context_v1 context{
            .struct_size = sizeof(context),
            .reserved = 11,
            .runtime_id = {12, 13},
            .id = 14,
        };
        const auto original_context = context;
        require(opening.open_context(request, context) == failure,
            "a non-OK open status is returned unchanged");
        require(same_context(context, original_context) && opening.contexts.empty() &&
                    opening.next_context == 100,
            "a failed open preserves the context output and reserves no cleanup record");
    }
}

TEST_CASE("fallout4.session.context_query_validates_identity_and_data_status",
    "[component][fallout4][session]") {
    FakeOutput output;
    const auto context = open_fake_context(output);
    auto mismatched_context = context;
    ++mismatched_context.runtime_id[0];
    input_next_effects_context_info_v1 mismatched_info{
        .struct_size = 81,
        .state = 82,
        .context = {.struct_size = 83, .reserved = 84, .runtime_id = {85, 86}, .id = 87},
    };
    const auto original_mismatched_info = mismatched_info;
    require(output.query_context(mismatched_context, mismatched_info) ==
                    INPUT_NEXT_STATUS_STALE_CONTEXT &&
                mismatched_info.struct_size == original_mismatched_info.struct_size &&
                mismatched_info.state == original_mismatched_info.state &&
                same_context(mismatched_info.context, original_mismatched_info.context),
        "context lookup matches runtime words and ID while preserving stale outputs");
    constexpr std::array non_data_query_statuses{
        INPUT_NEXT_STATUS_PENDING,
        INPUT_NEXT_STATUS_BUSY,
        INPUT_NEXT_STATUS_UNAVAILABLE,
    };
    for (const auto status : non_data_query_statuses) {
        output.query_context_status = status;
        input_next_effects_context_info_v1 info{
            .struct_size = 71,
            .state = 72,
            .context = {.struct_size = 73, .reserved = 74, .runtime_id = {75, 76}, .id = 77},
        };
        const auto original_info = info;
        require(output.query_context(context, info) == status,
            "a non-data query status is returned unchanged");
        require(info.struct_size == original_info.struct_size &&
                    info.state == original_info.state &&
                    same_context(info.context, original_info.context),
            "non-data query statuses preserve caller-owned outputs");
    }
    output.query_context_status = INPUT_NEXT_STATUS_REVOKED;
    output.query_context_state = INPUT_NEXT_EFFECTS_CONTEXT_REVOKED;
    input_next_effects_context_info_v1 revoked_info{};
    require(output.query_context(context, revoked_info) == INPUT_NEXT_STATUS_REVOKED &&
                revoked_info.struct_size == sizeof(revoked_info) &&
                revoked_info.state == INPUT_NEXT_EFFECTS_CONTEXT_REVOKED &&
                same_context(revoked_info.context, context),
        "REVOKED is a data-bearing context query status");
}

TEST_CASE("fallout4.session.body_submission_copies_pcm_and_reports_fallback",
    "[component][fallout4][session]") {
    FakeOutput output;
    const auto context = open_fake_context(output);
    input_next_effects_body_request_v1 body{};
    body.struct_size = sizeof(body);
    body.ttl_ms = 20;
    body.route_preference = INPUT_NEXT_EFFECTS_BODY_ROUTE_CLASSIC;
    body.context = context;
    body.sequence = 1;
    constexpr std::array pcm{0.25F, -0.5F, 0.75F, 1.0F};
    body.pcm.struct_size = sizeof(body.pcm);
    body.pcm.frame_count = 2;
    body.pcm.channels = 2;
    body.pcm.sample_rate = 48000;
    body.pcm.gain = 1.0F;
    body.pcm.samples = pcm.data();
    input_next_effects_body_receipt_v1 body_receipt{};
    require(output.submit_body(body, body_receipt) == INPUT_NEXT_STATUS_OK &&
                output.bodies.back().samples == std::vector<float>(pcm.begin(), pcm.end()) &&
                output.bodies.back().request.pcm.samples == nullptr &&
                body_receipt.fallback_reason == INPUT_NEXT_EFFECTS_BODY_FALLBACK_NONE,
        "body samples are copied at call entry and classic fallback is reported accurately");
    body.route_preference = INPUT_NEXT_EFFECTS_BODY_ROUTE_PREFER_HD;
    body.pcm.samples = nullptr;
    body.pcm.frame_count = 0;
    ++body.sequence;
    require(output.submit_body(body, body_receipt) == INPUT_NEXT_STATUS_OK &&
                body_receipt.effective_route == INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_CLASSIC &&
                body_receipt.fallback_reason == INPUT_NEXT_EFFECTS_BODY_FALLBACK_NOT_READY,
        "an HD preference falling back to classic reports NOT_READY");
}

TEST_CASE("fallout4.session.close_ack_requires_the_complete_retained_receipt",
    "[component][fallout4][session]") {
    FakeOutput output;
    const auto context = open_fake_context(output);
    input_next_effects_close_v1 close{};
    close.struct_size = sizeof(close);
    close.context = context;
    close.close_request_id = context.id;
    input_next_effects_close_receipt_v1 receipt{};
    require(output.close_context(close, receipt) == INPUT_NEXT_STATUS_REVOKED,
        "the close fixture retains one exact receipt");
    const auto rejects_without_retiring = [&](input_next_effects_close_receipt_v1 forged) {
        require(output.ack_close(forged) == INPUT_NEXT_STATUS_INVALID_ARGUMENT &&
                    output.contexts.size() == 1,
            "ACK rejects a non-exact receipt without retiring its context");
    };
    auto forged_receipt = receipt;
    ++forged_receipt.struct_size;
    rejects_without_retiring(forged_receipt);
    forged_receipt = receipt;
    forged_receipt.disposition = INPUT_NEXT_EFFECTS_CLOSE_CONNECTION_RETIRED;
    rejects_without_retiring(forged_receipt);
    forged_receipt = receipt;
    ++forged_receipt.context.reserved;
    rejects_without_retiring(forged_receipt);
    forged_receipt = receipt;
    ++forged_receipt.context.runtime_id[0];
    rejects_without_retiring(forged_receipt);
    forged_receipt = receipt;
    ++forged_receipt.context.id;
    rejects_without_retiring(forged_receipt);
    forged_receipt = receipt;
    ++forged_receipt.target.reserved;
    rejects_without_retiring(forged_receipt);
    forged_receipt = receipt;
    ++forged_receipt.target.runtime_id[1];
    rejects_without_retiring(forged_receipt);
    forged_receipt = receipt;
    ++forged_receipt.target.device_key;
    rejects_without_retiring(forged_receipt);
    forged_receipt = receipt;
    ++forged_receipt.target.connection_id;
    rejects_without_retiring(forged_receipt);
    forged_receipt = receipt;
    ++forged_receipt.close_request_id;
    rejects_without_retiring(forged_receipt);
    forged_receipt = receipt;
    ++forged_receipt.publication_revision;
    rejects_without_retiring(forged_receipt);
    require(output.ack_close(receipt) == INPUT_NEXT_STATUS_OK && output.contexts.empty(),
        "ACK retires a context only after validating its complete known receipt");
}

} // namespace fallout4_adapter::session_tests
