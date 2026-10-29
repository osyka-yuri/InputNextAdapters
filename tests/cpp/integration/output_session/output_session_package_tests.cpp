#include <catch2/catch_test_macros.hpp>

#include "output_session/fake_output.hpp"
#include "output_session/support.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>
#include <inputnext_adapters/output_frame.hpp>
#include <inputnext_adapters/output_session.hpp>
#include <inputnext_adapters/runtime_client.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>

namespace {
using namespace std::chrono_literals;
using namespace inputnext::adapters;
using namespace inputnext::test::output_session;

constexpr std::size_t status_capacity = 4;

RuntimeClient connect_packaged_runtime() {
    RuntimeClient client;
    const auto runtime_path = std::filesystem::current_path() / L"inputnext_runtime.dll";
    const auto result = client.connect(runtime_path);
    REQUIRE(static_cast<bool>(result));
    return client;
}

struct BodyStatusCapture {
    std::array<input_next_status, status_capacity> statuses{};
    std::size_t count{};

    void submit_last(const RuntimeClient& client, const FakeOutput& output) noexcept {
        if (count >= statuses.size() || output.body_calls.empty()) {
            return;
        }

        const auto& call = output.body_calls.back();
        auto request = static_cast<const input_next_effects_body_request_v1&>(call.request);
        request.pcm.samples = call.samples.empty() ? nullptr : call.samples.data();
        input_next_effects_body_receipt_v1 receipt{};
        statuses[count++] = client.submit_body(request, receipt);
    }
};

struct TriggerStatusCapture {
    std::array<input_next_status, status_capacity> submits{};
    std::array<input_next_status, status_capacity> clears{};
    std::size_t submit_count{};
    std::size_t clear_count{};

    void submit_last(const RuntimeClient& client, const FakeOutput& output) noexcept {
        if (submit_count >= submits.size() || output.triggers.empty()) {
            return;
        }

        const auto& call = output.triggers.back();
        const auto& request = static_cast<const input_next_effects_request_v1&>(call);
        input_next_effects_receipt_v1 receipt{};
        submits[submit_count++] = client.submit(request, receipt);
    }

    void clear_last(const RuntimeClient& client, const FakeOutput& output) noexcept {
        if (clear_count >= clears.size() || output.trigger_clear_requests.empty()) {
            return;
        }

        clears[clear_count++] = client.clear(output.trigger_clear_requests.back());
    }
};

TEST_CASE("output_session.package.classic_keep_validates_cold", "[integration][effects][package]") {
    const auto client = connect_packaged_runtime();
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 0);
    TickClock clock{.supplied = MonoTime{} + 10s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    BodyStatusCapture capture;
    output.after_body_submit = [&] noexcept {
        capture.submit_last(client, output);
    };
    auto frame = body_frame(clock.now(), 101, 100ms);
    frame.rumble = RumbleIntent{
        .payload = {.low_frequency = 0.4F, .high_frequency = 0.2F},
        .ttl_ms = 80,
    };

    REQUIRE(session->send(frame, clock.now(), clock));
    REQUIRE(output.body_calls.size() == 1);
    REQUIRE(output.body_calls.front().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP);
    REQUIRE(output.body_calls.front().samples.empty());
    REQUIRE(capture.count == 1);
    REQUIRE(capture.statuses[0] == INPUT_NEXT_STATUS_UNAVAILABLE);

    auto invalid =
        static_cast<const input_next_effects_body_request_v1&>(output.body_calls.front().request);
    input_next_effects_body_receipt_v1 receipt{};
    receipt.publication_revision = 901;
    invalid.pcm.struct_size = 0;
    REQUIRE(client.submit_body(invalid, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(receipt.publication_revision == 901);
    invalid.pcm.struct_size = sizeof(invalid.pcm);
    invalid.ttl_ms = 0;
    REQUIRE(client.submit_body(invalid, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(receipt.publication_revision == 901);
}

TEST_CASE("output_session.package.hd_replace_stop_and_clear_validate_cold",
    "[integration][effects][package]") {
    const auto client = connect_packaged_runtime();
    FakeOutput output;
    advertise_hd(output, 920);
    output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 1);
    TickClock clock{.supplied = MonoTime{} + 20s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    BodyStatusCapture capture;
    output.after_body_submit = [&] noexcept {
        capture.submit_last(client, output);
    };
    REQUIRE(session->send(body_frame(clock.now(), 102, 100ms), clock.now(), clock));
    REQUIRE(output.body_calls.size() == 1);
    REQUIRE(output.body_calls.front().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
    REQUIRE_FALSE(output.body_calls.front().samples.empty());
    REQUIRE(capture.count == 1);
    REQUIRE(capture.statuses[0] == INPUT_NEXT_STATUS_UNAVAILABLE);

    auto invalid =
        static_cast<const input_next_effects_body_request_v1&>(output.body_calls.front().request);
    auto samples = output.body_calls.front().samples;
    invalid.pcm.samples = samples.data();
    input_next_effects_body_receipt_v1 receipt{};
    receipt.lease_id = 902;
    invalid.pcm.channels = 3;
    REQUIRE(client.submit_body(invalid, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(receipt.lease_id == 902);
    invalid.pcm.channels = output.body_calls.front().request.pcm.channels;
    samples.front() = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(client.submit_body(invalid, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(receipt.lease_id == 902);

    clock.supplied += 101ms;
    REQUIRE(session->send(OutputFrame{}, clock.now(), clock));
    REQUIRE(output.body_calls.size() == 2);
    REQUIRE(output.body_calls.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_STOP);
    REQUIRE(capture.count == 2);
    REQUIRE(capture.statuses[1] == INPUT_NEXT_STATUS_UNAVAILABLE);

    REQUIRE(session->clear_body(clock));
    REQUIRE(output.body_clears.size() == 1);
    const auto& clear =
        static_cast<const input_next_effects_body_clear_v1&>(output.body_clears.back());
    REQUIRE(client.clear_body(clear) == INPUT_NEXT_STATUS_UNAVAILABLE);
    auto invalid_clear = clear;
    invalid_clear.reserved = 1;
    REQUIRE(client.clear_body(invalid_clear) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
}

TEST_CASE("output_session.package.trigger_submit_and_clear_validate_cold",
    "[integration][effects][package]") {
    const auto client = connect_packaged_runtime();
    FakeOutput output;
    output.disable_body_leases();
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 2);
    TickClock clock{.supplied = MonoTime{} + 30s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);

    TriggerStatusCapture capture;
    output.after_trigger_submit = [&] noexcept {
        capture.submit_last(client, output);
    };
    output.after_trigger_clear = [&] noexcept {
        capture.clear_last(client, output);
    };

    OutputFrame frame{};
    frame.left = trigger();
    REQUIRE(session->send(frame, clock.now(), clock));
    REQUIRE(output.triggers.size() == 1);
    REQUIRE(capture.submit_count == 1);
    REQUIRE(capture.submits[0] == INPUT_NEXT_STATUS_UNAVAILABLE);

    auto invalid = static_cast<const input_next_effects_request_v1&>(output.triggers.front());
    input_next_effects_receipt_v1 receipt{};
    receipt.publication_revision = 903;
    invalid.context.struct_size = 0;
    REQUIRE(client.submit(invalid, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(receipt.publication_revision == 903);
    invalid.context.struct_size = sizeof(invalid.context);
    invalid.ttl_ms = 0;
    REQUIRE(client.submit(invalid, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(receipt.publication_revision == 903);

    clock.supplied += 1ms;
    REQUIRE(session->send(OutputFrame{}, clock.now(), clock));
    REQUIRE(output.trigger_clear_requests.size() == 1);
    REQUIRE(capture.clear_count == 1);
    REQUIRE(capture.clears[0] == INPUT_NEXT_STATUS_UNAVAILABLE);
    auto invalid_clear = output.trigger_clear_requests.back();
    invalid_clear.mutation_sequence = 0;
    REQUIRE(client.clear(invalid_clear) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
}

TEST_CASE("output_session.package.close_ack_and_progress_validate_cold",
    "[integration][effects][package]") {
    const auto client = connect_packaged_runtime();
    FakeOutput output;
    FakeObserver observer;
    const auto session = std::make_unique<OutputSession>(output, observer, 3);
    TickClock clock{.supplied = MonoTime{} + 40s, .use_steady_clock = false};
    bind_with_fresh_sample(*session, observer, clock);
    REQUIRE(output.contexts.size() == 1);
    const auto target = output.contexts.front().target;

    REQUIRE(session->close_and_drain(clock));
    REQUIRE(output.closes.size() == 1);
    REQUIRE(output.acknowledgements.size() == 1);
    const auto& recorded_close = output.closes.back();
    const auto& recorded_ack = output.acknowledgements.back();

    input_next_effects_close_receipt_v1 close_receipt{};
    close_receipt.publication_revision = 904;
    REQUIRE(client.close_context(recorded_close, close_receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
    REQUIRE(close_receipt.publication_revision == 904);
    auto invalid_close = recorded_close;
    invalid_close.reserved = 1;
    REQUIRE(
        client.close_context(invalid_close, close_receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(close_receipt.publication_revision == 904);
    REQUIRE(client.ack_close(recorded_ack) == INPUT_NEXT_STATUS_UNAVAILABLE);
    auto invalid_ack = recorded_ack;
    invalid_ack.close_request_id = 0;
    REQUIRE(client.ack_close(invalid_ack) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);

    input_next_effects_progress_query_v1 query{};
    query.struct_size = sizeof(query);
    query.target = target;
    query.publication_revision = recorded_ack.publication_revision;
    REQUIRE(query.publication_revision > 0);
    input_next_effects_progress_v1 progress{};
    progress.attempt_serial = 905;
    REQUIRE(client.query_progress(query, progress) == INPUT_NEXT_STATUS_UNAVAILABLE);
    REQUIRE(progress.attempt_serial == 905);
    query.target.reserved = 1;
    REQUIRE(client.query_progress(query, progress) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(progress.attempt_serial == 905);
}
} // namespace
