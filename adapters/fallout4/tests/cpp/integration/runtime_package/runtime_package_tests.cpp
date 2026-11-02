#include "cpp/component/session/fake_output.hpp"
#include "cpp/component/session/support.hpp"

#include <fallout4_adapter/config.hpp>
#include <fallout4_adapter/model.hpp>
#include <fallout4_adapter/session.hpp>
#include <inputnext_adapters/runtime_client.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <memory>

namespace {
using namespace std::chrono_literals;
using namespace fallout4_adapter;
using namespace fallout4_adapter::session_tests;

inputnext::adapters::RuntimeClient connect_runtime() {
    inputnext::adapters::RuntimeClient client;
    REQUIRE(client.connect(std::filesystem::current_path() / L"inputnext_runtime.dll"));
    return client;
}

void validate_frames(const inputnext::adapters::RuntimeClient& client, const FakeOutput& output) {
    for (const auto& request : output.triggers) {
        input_next_effects_receipt_v1 receipt{};
        receipt.publication_revision = 71;
        REQUIRE(client.submit(request, receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
        REQUIRE(receipt.publication_revision == 71);
        auto invalid = request;
        invalid.context.struct_size = 0;
        REQUIRE(client.submit(invalid, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
        REQUIRE(receipt.publication_revision == 71);
    }
    for (const auto& body : output.bodies) {
        auto request = body.request;
        const auto& samples = body.samples;
        request.pcm.samples = samples.empty() ? nullptr : samples.data();
        input_next_effects_body_receipt_v1 receipt{};
        receipt.lease_id = 72;
        REQUIRE(client.submit_body(request, receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
        REQUIRE(receipt.lease_id == 72);
        request.pcm.struct_size = 0;
        REQUIRE(client.submit_body(request, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
        REQUIRE(receipt.lease_id == 72);
    }
}

void publish_shot(Session& session, Ticket ticket, MonoTime now) {
    PolicyEvent shot{};
    shot.kind = EventKind::shot;
    shot.arrived_at = now;
    shot.weapon_identity = 17;
    REQUIRE(session.enqueue(ticket, shot, now));
    REQUIRE(session.publish(ticket, observation(now), now));
    session.tick(now + 1ms);
}

TEST_CASE("fallout4.package.classic_requests_validate_cold", "[integration][fallout4][package]") {
    const auto client = connect_runtime();
    FakeOutput output;
    const auto session = std::make_unique<Session>(output, Config{});
    const auto now = MonoTime{} + 1s;
    const auto ticket = activate(*session, now);
    publish_shot(*session, ticket, now + 2ms);
    REQUIRE_FALSE(output.triggers.empty());
    REQUIRE_FALSE(output.bodies.empty());
    REQUIRE(output.bodies.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_KEEP);
    validate_frames(client, output);
    REQUIRE(session->close_and_drain());
}

TEST_CASE("fallout4.package.hd_and_vats_requests_validate_cold",
    "[integration][fallout4][package]") {
    const auto client = connect_runtime();
    FakeOutput output;
    output.configured_capabilities.feature_flags |= INPUT_NEXT_EFFECTS_CAP_HD_READY;
    output.configured_capabilities.audio_instance_id = 901;
    const auto session = std::make_unique<Session>(output, Config{});
    const auto now = MonoTime{} + 1s;
    const auto ticket = activate(*session, now);
    publish_shot(*session, ticket, now + 2ms);
    REQUIRE_FALSE(output.bodies.empty());
    REQUIRE_FALSE(output.bodies.back().samples.empty());
    REQUIRE(output.bodies.back().request.pcm_command == INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
    REQUIRE(session->publish(ticket, observation(now + 20ms, Mode::vats_selection), now + 20ms));
    session->tick(now + 20ms);
    validate_frames(client, output);
    REQUIRE(session->close_and_drain());
}

TEST_CASE("fallout4.package.focus_cleanup_validates_cold", "[integration][fallout4][package]") {
    const auto client = connect_runtime();
    FakeOutput output;
    const auto session = std::make_unique<Session>(output, Config{});
    const auto now = MonoTime{} + 1s;
    static_cast<void>(activate(*session, now));
    output.query_context_status = INPUT_NEXT_STATUS_REVOKED;
    session->tick(now + 2ms);
    session->control(Control::focus_lost, now + 3ms);
    session->tick(now + 3ms);
    REQUIRE(output.close_requests.size() == 1);
    REQUIRE(output.acknowledged_close_receipts.size() == 1);
    auto close = output.close_requests.front();
    input_next_effects_close_receipt_v1 receipt{};
    receipt.publication_revision = 73;
    REQUIRE(client.close_context(close, receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
    REQUIRE(receipt.publication_revision == 73);
    close.reserved = 1;
    REQUIRE(client.close_context(close, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(receipt.publication_revision == 73);
    auto acknowledgement = output.acknowledged_close_receipts.front();
    REQUIRE(client.ack_close(acknowledgement) == INPUT_NEXT_STATUS_UNAVAILABLE);
    acknowledgement.close_request_id = 0;
    REQUIRE(client.ack_close(acknowledgement) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(session->close_and_drain());
}
} // namespace
