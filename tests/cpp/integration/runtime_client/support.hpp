#pragma once

#include "fake_runtime.hpp"
#include "inputnext_adapters/runtime_client.hpp"

#include <catch2/catch_test_macros.hpp>

#include <Windows.h>

#include <TlHelp32.h>

#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <utility>

namespace inputnext::test::runtime_client {
using inputnext::adapters::ConnectError;
using inputnext::adapters::RuntimeClient;
using FixturePaths = std::array<std::filesystem::path, 16>;

const FixturePaths& get_fixtures();
void set_fixtures(FixturePaths fixtures);

class FixtureObserver {
public:
    explicit FixtureObserver(const std::filesystem::path& path);
    FixtureObserver(const FixtureObserver&) = delete;
    FixtureObserver& operator=(const FixtureObserver&) = delete;
    FixtureObserver(FixtureObserver&&) = delete;
    FixtureObserver& operator=(FixtureObserver&&) = delete;
    ~FixtureObserver();
    explicit operator bool() const noexcept;
    [[nodiscard]] fake_runtime_state& state() const noexcept;
    void reset() const noexcept;

private:
    HMODULE module_{};
    fake_runtime_state* state_{};
    fake_runtime_reset_fn reset_{};
};

bool module_is_loaded(const std::filesystem::path& path);
bool reset_fixture(const std::filesystem::path& path);

namespace detail {
class module_snapshot_api {
public:
    module_snapshot_api() = default;
    module_snapshot_api(const module_snapshot_api&) = delete;
    module_snapshot_api& operator=(const module_snapshot_api&) = delete;
    module_snapshot_api(module_snapshot_api&&) = delete;
    module_snapshot_api& operator=(module_snapshot_api&&) = delete;
    virtual ~module_snapshot_api() = default;
    virtual HANDLE create_snapshot(DWORD process_id) noexcept = 0;
    virtual BOOL module_first(HANDLE snapshot, MODULEENTRY32W& entry) noexcept = 0;
    virtual BOOL module_next(HANDLE snapshot, MODULEENTRY32W& entry) noexcept = 0;
    virtual BOOL close_handle(HANDLE snapshot) noexcept = 0;
};

bool module_is_loaded(const std::filesystem::path& path,
    DWORD process_id,
    module_snapshot_api& api);
} // namespace detail

bool same_target(const input_next_device_target_v1& left, const input_next_device_target_v1& right);
bool same_context(const input_next_effects_context_v1& left,
    const input_next_effects_context_v1& right);
bool same_capabilities(const input_next_effects_capabilities_v1& left,
    const input_next_effects_capabilities_v1& right);
bool same_trigger(const input_next_effects_trigger_v1& left,
    const input_next_effects_trigger_v1& right);
bool same_rumble(const input_next_effects_rumble_v1& left,
    const input_next_effects_rumble_v1& right);
bool same_leds(const input_next_effects_led_v1& left, const input_next_effects_led_v1& right);
bool same_pcm(const input_next_effects_pcm_v1& left, const input_next_effects_pcm_v1& right);
bool same_request(const input_next_effects_request_v1& left,
    const input_next_effects_request_v1& right);
bool same_receipt(const input_next_effects_receipt_v1& left,
    const input_next_effects_receipt_v1& right);
bool same_clear(const input_next_effects_clear_v1& left, const input_next_effects_clear_v1& right);
bool same_body_request(const input_next_effects_body_request_v1& left,
    const input_next_effects_body_request_v1& right);
bool same_body_receipt(const input_next_effects_body_receipt_v1& left,
    const input_next_effects_body_receipt_v1& right);
bool same_body_clear(const input_next_effects_body_clear_v1& left,
    const input_next_effects_body_clear_v1& right);
bool same_close_receipt(const input_next_effects_close_receipt_v1& left,
    const input_next_effects_close_receipt_v1& right);

void check_connect_failure(const std::filesystem::path& path,
    ConnectError expected_error,
    bool query_expected);

void test_reconnect_after_incompatible_api(const std::filesystem::path& incompatible_fixture,
    const std::filesystem::path& valid_fixture);
void test_scoped_disconnect(const std::filesystem::path& fixture);
void test_disconnect_idempotence(const std::filesystem::path& fixture);

void test_client_target_and_capabilities(RuntimeClient& client,
    const std::filesystem::path& fixture,
    input_next_device_target_v1& target,
    input_next_effects_capabilities_v1& capabilities,
    input_next_effects_context_v1& context);
void test_client_channel_effects_and_clear(RuntimeClient& client,
    const std::filesystem::path& fixture,
    const input_next_effects_context_v1& context,
    input_next_effects_request_v1& request,
    input_next_effects_receipt_v1& receipt);
void test_client_body_effects(RuntimeClient& client,
    const std::filesystem::path& fixture,
    const input_next_device_target_v1& target,
    const input_next_effects_context_v1& context,
    const input_next_effects_capabilities_v1& capabilities);

void test_concurrent_clients_and_moves(RuntimeClient& first,
    const std::filesystem::path& first_fixture,
    const std::filesystem::path& second_fixture,
    const input_next_device_target_v1& target,
    const input_next_effects_request_v1& request,
    const input_next_effects_receipt_v1& receipt);

template <typename Function>
void with_connected_client(const std::filesystem::path& fixture, Function&& operation) {
    REQUIRE(reset_fixture(fixture));
    std::exception_ptr failure;
    try {
        RuntimeClient client;
        REQUIRE(client.connect(fixture));
        std::forward<Function>(operation)(client);
    } catch (...) {
        failure = std::current_exception();
    }
    CHECK(!module_is_loaded(fixture));
    if (failure) {
        std::rethrow_exception(failure);
    }
}

} // namespace inputnext::test::runtime_client
