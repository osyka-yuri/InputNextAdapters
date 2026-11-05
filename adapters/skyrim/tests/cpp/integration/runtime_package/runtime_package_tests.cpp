#include "session/support/fake_output.hpp"
#include "session/support/session_test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>
#include <inputnext_adapters/runtime_client.hpp>
#include <skyrim_adapter/model.hpp>

#include <chrono>
#include <filesystem>
#include <memory>

namespace {
using namespace std::chrono_literals;
using namespace skyrim_adapter;

inputnext::adapters::RuntimeClient connect_runtime() {
    inputnext::adapters::RuntimeClient client;
    REQUIRE(client.connect(std::filesystem::current_path() / L"inputnext_runtime.dll"));
    return client;
}

void validate_frames(const inputnext::adapters::RuntimeClient& client,
    const session_test::FakeOutput& output) {
    for (const auto& call : output.submissions) {
        const auto& request = static_cast<const input_next_effects_request_v1&>(call);
        input_next_effects_receipt_v1 receipt{};
        receipt.publication_revision = 71;
        REQUIRE(client.submit(request, receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
        REQUIRE(receipt.publication_revision == 71);
        auto invalid = request;
        invalid.context.struct_size = 0;
        REQUIRE(client.submit(invalid, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
        REQUIRE(receipt.publication_revision == 71);
    }
    for (const auto& call : output.body_submissions) {
        auto request = static_cast<const input_next_effects_body_request_v1&>(call.request);
        request.pcm.samples = call.samples.empty() ? nullptr : call.samples.data();
        input_next_effects_body_receipt_v1 receipt{};
        receipt.lease_id = 72;
        REQUIRE(client.submit_body(request, receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
        REQUIRE(receipt.lease_id == 72);
        request.pcm.struct_size = 0;
        REQUIRE(client.submit_body(request, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
        REQUIRE(receipt.lease_id == 72);
    }
}

TEST_CASE("skyrim.package.classic_casting_requests_validate_cold",
    "[integration][skyrim][package]") {
    const auto client = connect_runtime();
    const auto fixture = std::make_unique<session_test::Fixture>();
    fixture->settle(session_test::casting());
    REQUIRE_FALSE(fixture->output.submissions.empty());
    REQUIRE_FALSE(fixture->output.body_submissions.empty());
    REQUIRE(fixture->output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_KEEP);
    validate_frames(client, fixture->output);
    REQUIRE(fixture->coordinator.close_and_drain());
}

TEST_CASE("skyrim.package.hd_and_menu_requests_validate_cold", "[integration][skyrim][package]") {
    const auto client = connect_runtime();
    const auto fixture = std::make_unique<session_test::Fixture>();
    session_test::advertise_hd(fixture->output, 901);
    fixture->output.body_route = INPUT_NEXT_EFFECTS_BODY_EFFECTIVE_HD;
    fixture->settle(session_test::casting());
    REQUIRE_FALSE(fixture->output.body_submissions.empty());
    REQUIRE(fixture->output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_REPLACE);
    REQUIRE_FALSE(fixture->output.body_submissions.back().samples.empty());

    fixture->coordinator.control(Control::menu_suppressed, fixture->time);
    auto menu = session_test::idle();
    menu.menus_clear = false;
    fixture->advance(menu, 20ms);
    REQUIRE(fixture->output.body_submissions.back().request.pcm_command ==
            INPUT_NEXT_EFFECTS_BODY_PCM_STOP);
    validate_frames(client, fixture->output);
    REQUIRE(fixture->coordinator.close_and_drain());
}

TEST_CASE("skyrim.package.focus_cleanup_validates_cold", "[integration][skyrim][package]") {
    const auto client = connect_runtime();
    const auto fixture = std::make_unique<session_test::Fixture>();
    fixture->settle(session_test::casting());
    fixture->output.body_clear_status = INPUT_NEXT_STATUS_REVOKED;
    fixture->coordinator.control(Control::focus_lost, fixture->time);
    fixture->coordinator.tick(fixture->time + 1ms);
    REQUIRE(fixture->output.close_requests.size() == 1);
    REQUIRE(fixture->output.acknowledged_close_receipts.size() == 1);

    auto close = fixture->output.close_requests.front();
    input_next_effects_close_receipt_v1 receipt{};
    receipt.publication_revision = 73;
    REQUIRE(client.close_context(close, receipt) == INPUT_NEXT_STATUS_UNAVAILABLE);
    REQUIRE(receipt.publication_revision == 73);
    close.reserved = 1;
    REQUIRE(client.close_context(close, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(receipt.publication_revision == 73);
    auto acknowledgement = fixture->output.acknowledged_close_receipts.front();
    REQUIRE(client.ack_close(acknowledgement) == INPUT_NEXT_STATUS_UNAVAILABLE);
    acknowledgement.close_request_id = 0;
    REQUIRE(client.ack_close(acknowledgement) == INPUT_NEXT_STATUS_INVALID_ARGUMENT);
    REQUIRE(fixture->coordinator.close_and_drain());
}
} // namespace
