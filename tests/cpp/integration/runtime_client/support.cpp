#include "support.hpp"
#include "fake_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <TlHelp32.h>
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

namespace inputnext::test::runtime_client {

FixtureObserver::FixtureObserver(const std::filesystem::path& path) {
    auto loader_path = path;
    loader_path.make_preferred();
    module_ = LoadLibraryExW(loader_path.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module_ == nullptr) {
        return;
    }
    static_assert(sizeof(fake_runtime_get_state_fn) == sizeof(FARPROC));
    static_assert(sizeof(fake_runtime_reset_fn) == sizeof(FARPROC));
    const auto get_state = reinterpret_cast<fake_runtime_get_state_fn>(
        GetProcAddress(module_, "inputnext_test_get_state"));
    reset_ =
        reinterpret_cast<fake_runtime_reset_fn>(GetProcAddress(module_, "inputnext_test_reset"));
    if (get_state != nullptr && reset_ != nullptr) {
        state_ = get_state();
    }
}

FixtureObserver::~FixtureObserver() {
    if (module_ != nullptr) {
        FreeLibrary(module_);
    }
}

FixtureObserver::operator bool() const noexcept {
    return state_ != nullptr;
}

fake_runtime_state& FixtureObserver::state() const noexcept {
    return *state_;
}

void FixtureObserver::reset() const noexcept {
    reset_();
}

namespace detail {
namespace {
DWORD failure_code() noexcept {
    const auto error = GetLastError();
    return error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
}

HANDLE create_module_snapshot(DWORD process_id, module_snapshot_api& api) {
    constexpr auto max_snapshot_attempts = 8U;
    for (auto attempt = 0U; attempt < max_snapshot_attempts; ++attempt) {
        SetLastError(ERROR_SUCCESS);
        auto* const snapshot = api.create_snapshot(process_id);
        if (snapshot != INVALID_HANDLE_VALUE && snapshot != nullptr) {
            return snapshot;
        }
        const auto snapshot_error = failure_code();
        if (snapshot_error != ERROR_BAD_LENGTH) {
            throw std::system_error(static_cast<int>(snapshot_error),
                std::system_category(),
                "CreateToolhelp32Snapshot");
        }
    }
    throw std::system_error(ERROR_BAD_LENGTH, std::system_category(), "CreateToolhelp32Snapshot");
}

class module_snapshot {
public:
    module_snapshot(DWORD process_id, module_snapshot_api& api)
        : api_(api),
          handle_(create_module_snapshot(process_id, api)) {}

    module_snapshot(const module_snapshot&) = delete;
    module_snapshot& operator=(const module_snapshot&) = delete;
    module_snapshot(module_snapshot&&) = delete;
    module_snapshot& operator=(module_snapshot&&) = delete;

    ~module_snapshot() { static_cast<void>(close()); }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

    [[nodiscard]] DWORD close() noexcept {
        if (handle_ == INVALID_HANDLE_VALUE) {
            return ERROR_SUCCESS;
        }
        auto* const handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        SetLastError(ERROR_SUCCESS);
        return api_.close_handle(handle) != FALSE ? ERROR_SUCCESS : failure_code();
    }

private:
    module_snapshot_api& api_;
    HANDLE handle_;
};

struct module_lookup {
    bool found{};
    DWORD error{};
    const char* operation{};
};

std::filesystem::path normalized_path(const std::filesystem::path& path) {
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path));
}

module_lookup
find_module(const std::filesystem::path& expected_path, HANDLE snapshot, module_snapshot_api& api) {
    MODULEENTRY32W entry{};
    entry.dwSize = static_cast<DWORD>(sizeof(entry));
    SetLastError(ERROR_SUCCESS);
    auto has_entry = api.module_first(snapshot, entry);
    const char* operation = "Module32FirstW";
    while (has_entry != FALSE) {
        const std::filesystem::path module_path(entry.szExePath);
        if (!module_path.is_absolute()) {
            throw std::system_error(ERROR_BAD_PATHNAME,
                std::system_category(),
                "module snapshot returned a non-absolute path");
        }
        const auto actual_path = normalized_path(module_path);
        if (CompareStringOrdinal(actual_path.c_str(), -1, expected_path.c_str(), -1, TRUE) ==
            CSTR_EQUAL) {
            return {.found = true};
        }
        operation = "Module32NextW";
        SetLastError(ERROR_SUCCESS);
        has_entry = api.module_next(snapshot, entry);
    }
    const auto enumeration_error = GetLastError();
    return {.error = enumeration_error == ERROR_NO_MORE_FILES ? ERROR_SUCCESS : failure_code(),
        .operation = operation};
}
} // namespace

bool module_is_loaded(const std::filesystem::path& path,
    DWORD process_id,
    module_snapshot_api& api) {
    const auto expected_path = normalized_path(path);
    module_snapshot snapshot(process_id, api);
    const auto lookup = find_module(expected_path, snapshot.get(), api);
    const auto close_error = snapshot.close();
    if (lookup.error != ERROR_SUCCESS) {
        std::string diagnostic = lookup.operation;
        if (close_error != ERROR_SUCCESS) {
            diagnostic += "; CloseHandle for module snapshot also failed (";
            diagnostic += std::to_string(close_error);
            diagnostic += ')';
        }
        throw std::system_error(static_cast<int>(lookup.error), std::system_category(), diagnostic);
    }
    if (close_error != ERROR_SUCCESS) {
        throw std::system_error(static_cast<int>(close_error),
            std::system_category(),
            "CloseHandle for module snapshot");
    }
    return lookup.found;
}
} // namespace detail

bool module_is_loaded(const std::filesystem::path& path) {
    class toolhelp_api final : public detail::module_snapshot_api {
    public:
        HANDLE create_snapshot(DWORD process_id) noexcept override {
            return CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
        }

        BOOL module_first(HANDLE snapshot, MODULEENTRY32W& entry) noexcept override {
            return Module32FirstW(snapshot, &entry);
        }

        BOOL module_next(HANDLE snapshot, MODULEENTRY32W& entry) noexcept override {
            return Module32NextW(snapshot, &entry);
        }

        BOOL close_handle(HANDLE snapshot) noexcept override { return CloseHandle(snapshot); }
    };

    toolhelp_api api;
    return detail::module_is_loaded(path, GetCurrentProcessId(), api);
}

bool same_target(const input_next_device_target_v1& left,
    const input_next_device_target_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.reserved,
            value.runtime_id[0],
            value.runtime_id[1],
            value.device_key,
            value.connection_id);
    };
    return fields(left) == fields(right);
}

bool same_context(const input_next_effects_context_v1& left,
    const input_next_effects_context_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.reserved,
            value.runtime_id[0],
            value.runtime_id[1],
            value.id);
    };
    return fields(left) == fields(right);
}

bool same_capabilities(const input_next_effects_capabilities_v1& left,
    const input_next_effects_capabilities_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.supported_channels,
            value.feature_flags,
            value.completion_capabilities,
            value.audio_instance_id,
            value.capabilities_revision);
    };
    return fields(left) == fields(right);
}

bool same_trigger(const input_next_effects_trigger_v1& left,
    const input_next_effects_trigger_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.mode, value.start, value.end, value.strength, value.frequency);
    };
    return fields(left) == fields(right);
}

bool same_rumble(const input_next_effects_rumble_v1& left,
    const input_next_effects_rumble_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.low_frequency, value.high_frequency);
    };
    return fields(left) == fields(right);
}

bool same_leds(const input_next_effects_led_v1& left, const input_next_effects_led_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.lightbar_rgb,
            value.player_index,
            value.microphone_led,
            value.reserved);
    };
    return fields(left) == fields(right);
}

bool same_pcm(const input_next_effects_pcm_v1& left, const input_next_effects_pcm_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.frame_count,
            value.channels,
            value.sample_rate,
            value.gain,
            value.reserved,
            value.samples);
    };
    return fields(left) == fields(right);
}

bool same_request(const input_next_effects_request_v1& left,
    const input_next_effects_request_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.channel_mask,
            value.ttl_ms,
            value.reserved,
            value.mutation_sequence);
    };
    return fields(left) == fields(right) && same_context(left.context, right.context) &&
           same_trigger(left.left_trigger, right.left_trigger) &&
           same_trigger(left.right_trigger, right.right_trigger) &&
           same_rumble(left.rumble, right.rumble) && same_leds(left.leds, right.leds);
}

bool same_receipt(const input_next_effects_receipt_v1& left,
    const input_next_effects_receipt_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.accepted_mask,
            value.mutation_sequence,
            value.publication_revision);
    };
    return fields(left) == fields(right) && same_context(left.context, right.context) &&
           std::equal(std::begin(left.channel_revisions),
               std::end(left.channel_revisions),
               std::begin(right.channel_revisions));
}

bool same_clear(const input_next_effects_clear_v1& left, const input_next_effects_clear_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size, value.channel_mask, value.mutation_sequence);
    };
    return fields(left) == fields(right) && same_context(left.context, right.context) &&
           std::equal(std::begin(left.channel_revisions),
               std::end(left.channel_revisions),
               std::begin(right.channel_revisions));
}

bool same_body_request(const input_next_effects_body_request_v1& left,
    const input_next_effects_body_request_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.ttl_ms,
            value.route_preference,
            value.pcm_command,
            value.lease_id,
            value.audio_instance_id,
            value.sequence);
    };
    return fields(left) == fields(right) && same_context(left.context, right.context) &&
           same_rumble(left.fallback_rumble, right.fallback_rumble) &&
           same_pcm(left.pcm, right.pcm);
}

bool same_body_receipt(const input_next_effects_body_receipt_v1& left,
    const input_next_effects_body_receipt_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.effective_route,
            value.lease_id,
            value.audio_instance_id,
            value.sequence,
            value.publication_revision,
            value.fallback_reason,
            value.reserved);
    };
    return fields(left) == fields(right) && same_context(left.context, right.context) &&
           same_target(left.target, right.target);
}

bool same_body_clear(const input_next_effects_body_clear_v1& left,
    const input_next_effects_body_clear_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.reserved,
            value.lease_id,
            value.audio_instance_id,
            value.sequence);
    };
    return fields(left) == fields(right) && same_context(left.context, right.context);
}

bool same_close_receipt(const input_next_effects_close_receipt_v1& left,
    const input_next_effects_close_receipt_v1& right) {
    const auto fields = [](const auto& value) {
        return std::tie(value.struct_size,
            value.disposition,
            value.close_request_id,
            value.publication_revision);
    };
    return fields(left) == fields(right) && same_context(left.context, right.context) &&
           same_target(left.target, right.target);
}

bool reset_fixture(const std::filesystem::path& path) {
    FixtureObserver const observer(path);
    if (!observer) {
        return false;
    }
    observer.reset();
    return true;
}

void check_connect_failure(const std::filesystem::path& path,
    ConnectError expected_error,
    bool query_expected) {
    {
        const FixtureObserver observer(path);
        REQUIRE(observer);
        observer.reset();

        RuntimeClient client;
        const auto result = client.connect(path);
        REQUIRE(result.error == expected_error);
        REQUIRE(!result);
        REQUIRE(!client.connected());

        const auto& state = observer.state();
        REQUIRE(state.struct_size == sizeof(fake_runtime_state));
        REQUIRE(state.query_calls == (query_expected ? 1U : 0U));
        if (query_expected) {
            REQUIRE(state.last_requested_abi == INPUT_NEXT_EFFECTS_ABI_VERSION);
        }
    }
    REQUIRE(!module_is_loaded(path));
}

void test_reconnect_after_incompatible_api(const std::filesystem::path& incompatible_fixture,
    const std::filesystem::path& valid_fixture) {
    RuntimeClient client;
    const auto fail_result = client.connect(incompatible_fixture);
    REQUIRE(fail_result.error == ConnectError::incompatible_api);
    REQUIRE(!client.connected());

    const auto ok_result = client.connect(valid_fixture);
    REQUIRE(ok_result.error == ConnectError::none);
    REQUIRE(client.connected());
    REQUIRE(module_is_loaded(valid_fixture));

    input_next_device_target_v1 target{};
    input_next_device_selector_v1 selector{};
    selector.struct_size = sizeof(selector);
    selector.target.struct_size = sizeof(selector.target);
    selector.kind = INPUT_NEXT_CORE_SELECTOR_XINPUT_SLOT;
    input_next_effects_capabilities_v1 capabilities{};
    REQUIRE(client.resolve_target(selector, target, capabilities) == INPUT_NEXT_STATUS_OK);
    REQUIRE(target.struct_size == sizeof(target));

    client.disconnect();
    REQUIRE(!client.connected());
    REQUIRE(!module_is_loaded(valid_fixture));
}

void test_scoped_disconnect(const std::filesystem::path& fixture) {
    {
        RuntimeClient scoped;
        REQUIRE(scoped.connect(fixture));
        REQUIRE(module_is_loaded(fixture));
    }
    REQUIRE(!module_is_loaded(fixture));
}

void test_disconnect_idempotence(const std::filesystem::path& fixture) {
    RuntimeClient client;
    REQUIRE(!client.connected());
    client.disconnect();
    REQUIRE(!client.connected());
    client.disconnect();
    REQUIRE(!client.connected());

    REQUIRE(client.connect(fixture));
    REQUIRE(client.connected());
    REQUIRE(module_is_loaded(fixture));
    client.disconnect();
    REQUIRE(!client.connected());
    REQUIRE(!module_is_loaded(fixture));
    client.disconnect();
    REQUIRE(!client.connected());

    REQUIRE(client.connect(fixture));
    RuntimeClient moved_client(std::move(client));
    // Verify that disconnect() is safe on a moved-from client.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    client.disconnect();
    REQUIRE(!client.connected());
    moved_client.disconnect();
    REQUIRE(!moved_client.connected());
    REQUIRE(!module_is_loaded(fixture));
}

} // namespace inputnext::test::runtime_client
