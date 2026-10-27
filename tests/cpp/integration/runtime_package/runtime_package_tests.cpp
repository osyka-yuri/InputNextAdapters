#include "inputnext_adapters/runtime_client.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>

#include <Windows.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void check_core(HMODULE module) {
    const auto query = reinterpret_cast<pfn_input_next_runtime_query_core_api>(
        GetProcAddress(module, "input_next_runtime_query_core_api"));
    if (query == nullptr) {
        throw std::runtime_error{"Core query export missing"};
    }
    require(query(0) == nullptr, "Core query accepted an unsupported version");
    const auto* api = query(INPUT_NEXT_CORE_ABI_VERSION);
    if (api == nullptr || api->struct_size != sizeof(*api) ||
        api->abi_version != INPUT_NEXT_CORE_ABI_VERSION) {
        throw std::runtime_error{"Core ABI table mismatch"};
    }
    if (api->get_runtime_info == nullptr || api->enumerate_devices == nullptr ||
        api->resolve_target == nullptr || api->read_latest == nullptr ||
        api->open_reader == nullptr || api->read_events == nullptr ||
        api->close_reader == nullptr) {
        throw std::runtime_error{"Core ABI callback missing"};
    }
    require(api->get_runtime_info(nullptr) == INPUT_NEXT_STATUS_INVALID_ARGUMENT,
        "Core callback calling convention or validation mismatch");
}

void check_effects(const inputnext::adapters::RuntimeClient& client) {
    const input_next_device_selector_v1 selector{};
    input_next_device_target_v1 target{};
    target.device_key = 71;
    input_next_effects_capabilities_v1 capabilities{};
    capabilities.audio_instance_id = 72;
    require(client.resolve_target(selector, target, capabilities) ==
                INPUT_NEXT_STATUS_INVALID_ARGUMENT,
        "Effects resolve validation mismatch");
    require(target.device_key == 71 && capabilities.audio_instance_id == 72,
        "Failed resolve changed output");

    const input_next_effects_open_v1 open{};
    input_next_effects_context_v1 context{};
    context.id = 73;
    require(client.open_context(open, context) == INPUT_NEXT_STATUS_INVALID_ARGUMENT,
        "Effects open validation mismatch");
    require(context.id == 73, "Failed open changed output");
    input_next_effects_context_info_v1 info{};
    info.state = 74;
    require(client.query_context(context, info) == INPUT_NEXT_STATUS_INVALID_ARGUMENT,
        "Effects context validation mismatch");
    require(info.state == 74, "Failed context query changed output");

    const input_next_effects_request_v1 request{};
    input_next_effects_receipt_v1 receipt{};
    receipt.publication_revision = 75;
    require(client.submit(request, receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT,
        "Effects submit validation mismatch");
    require(receipt.publication_revision == 75, "Failed submit changed output");
    const input_next_effects_clear_v1 clear{};
    require(client.clear(clear) == INPUT_NEXT_STATUS_INVALID_ARGUMENT,
        "Effects clear validation mismatch");

    const input_next_effects_body_request_v1 body{};
    input_next_effects_body_receipt_v1 body_receipt{};
    body_receipt.lease_id = 76;
    require(client.submit_body(body, body_receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT,
        "Effects body validation mismatch");
    require(body_receipt.lease_id == 76, "Failed body submit changed output");
    const input_next_effects_body_clear_v1 body_clear{};
    require(client.clear_body(body_clear) == INPUT_NEXT_STATUS_INVALID_ARGUMENT,
        "Effects body clear validation mismatch");

    const input_next_effects_close_v1 close{};
    input_next_effects_close_receipt_v1 close_receipt{};
    close_receipt.publication_revision = 77;
    require(client.close_context(close, close_receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT,
        "Effects close validation mismatch");
    require(close_receipt.publication_revision == 77, "Failed close changed output");
    require(client.ack_close(close_receipt) == INPUT_NEXT_STATUS_INVALID_ARGUMENT,
        "Effects ACK validation mismatch");

    const input_next_effects_progress_query_v1 progress_query{};
    input_next_effects_progress_v1 progress{};
    progress.attempt_serial = 78;
    require(client.query_progress(progress_query, progress) == INPUT_NEXT_STATUS_INVALID_ARGUMENT,
        "Effects progress validation mismatch");
    require(progress.attempt_serial == 78, "Failed progress query changed output");
}

} // namespace

extern "C" int wmain(int argc, wchar_t* const* argv) try {
    require(argc == 2, "Expected an extracted InputNext bin directory");
    const auto library = std::filesystem::absolute(argv[1]) / L"inputnext_runtime.dll";
    inputnext::adapters::RuntimeClient client;
    require(static_cast<bool>(client.connect(library)), "Could not bind the production runtime");
    auto* const module = GetModuleHandleW(library.c_str());
    require(module != nullptr, "Bound runtime module missing");
    check_core(module);
    check_effects(client);
    auto moved = std::move(client);
    require(moved.connected(), "Production client move lost ownership");
    check_effects(moved);
    moved.disconnect();
    require(!moved.connected(), "Production client disconnect failed");
    std::cout << "Production Core/Effects ABI validated without native input or effects.\n";
    return 0;
} catch (const std::exception& error) {
    static_cast<void>(std::fprintf(stderr, "%s\n", error.what()));
    return 1;
} catch (...) {
    static_cast<void>(std::fputs("Production runtime integration failed.\n", stderr));
    return 1;
}
