#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace inputnext::test::runtime_client {
namespace {
struct snapshot_scenario {
    std::vector<std::filesystem::path> modules{get_fixtures()[0]};
    std::vector<DWORD> snapshot_errors;
    std::optional<DWORD> first_error;
    DWORD end_error{ERROR_NO_MORE_FILES};
    std::optional<DWORD> close_error;
};

struct snapshot_calls {
    std::size_t create{};
    std::size_t first{};
    std::size_t next{};
    std::size_t close{};
    HANDLE closed_snapshot{};
    bool arguments_valid{true};
};

class snapshot_script final : public detail::module_snapshot_api {
public:
    snapshot_scenario scenario;
    snapshot_calls calls;
    std::size_t position{};

    HANDLE create_snapshot(DWORD process_id) noexcept override {
        ++calls.create;
        calls.arguments_valid = calls.arguments_valid && process_id == 123U;
        if (calls.create <= scenario.snapshot_errors.size()) {
            SetLastError(scenario.snapshot_errors[calls.create - 1U]);
            return INVALID_HANDLE_VALUE;
        }
        return this;
    }

    BOOL module_first(HANDLE snapshot, MODULEENTRY32W& entry) noexcept override {
        ++calls.first;
        position = 0;
        check_arguments(snapshot, entry);
        if (scenario.first_error) {
            SetLastError(*scenario.first_error);
            return FALSE;
        }
        return write_entry(entry);
    }

    BOOL module_next(HANDLE snapshot, MODULEENTRY32W& entry) noexcept override {
        ++calls.next;
        ++position;
        check_arguments(snapshot, entry);
        return write_entry(entry);
    }

    BOOL close_handle(HANDLE snapshot) noexcept override {
        ++calls.close;
        calls.closed_snapshot = snapshot;
        calls.arguments_valid = calls.arguments_valid && snapshot == this;
        SetLastError(scenario.close_error.value_or(ERROR_SUCCESS));
        return scenario.close_error ? FALSE : TRUE;
    }

private:
    void check_arguments(HANDLE snapshot, const MODULEENTRY32W& entry) noexcept {
        calls.arguments_valid =
            calls.arguments_valid && snapshot == this && entry.dwSize == sizeof(entry);
    }

    BOOL write_entry(MODULEENTRY32W& entry) const noexcept {
        if (position == scenario.modules.size()) {
            SetLastError(scenario.end_error);
            return FALSE;
        }
        const auto& path = scenario.modules[position].native();
        if (path.size() >= std::size(entry.szExePath)) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        std::ranges::copy(path, entry.szExePath);
        entry.szExePath[path.size()] = L'\0';
        return TRUE;
    }
};

void check_closed(const snapshot_script& script) {
    CHECK(script.calls.close == 1U);
    CHECK(script.calls.closed_snapshot == &script);
    CHECK(script.calls.arguments_valid);
}

void check_failure(snapshot_script& script,
    DWORD expected,
    std::initializer_list<std::string_view> diagnostics) {
    try {
        static_cast<void>(detail::module_is_loaded(get_fixtures()[0], 123U, script));
        FAIL("module lookup failure must be reported");
    } catch (const std::system_error& error) {
        CHECK(error.code().category() == std::system_category());
        CHECK(error.code().value() == static_cast<int>(expected));
        for (const auto text : diagnostics) {
            CHECK(std::string_view(error.what()).contains(text));
        }
    }
}
} // namespace

TEST_CASE("runtime_client.module_snapshot_retry", "[integration][runtime_client]") {
    snapshot_script script;
    script.scenario.snapshot_errors = {ERROR_BAD_LENGTH, ERROR_BAD_LENGTH};

    CHECK(detail::module_is_loaded(get_fixtures()[0], 123U, script));
    CHECK(script.calls.create == 3U);
    CHECK(script.calls.first == 1U);
    CHECK(script.calls.next == 0U);
    check_closed(script);
}

TEST_CASE("runtime_client.module_snapshot_create_errors", "[integration][runtime_client]") {
    snapshot_script script;
    SECTION("permanent snapshot error is not retried") {
        script.scenario.snapshot_errors = {ERROR_ACCESS_DENIED};
        check_failure(script, ERROR_ACCESS_DENIED, {"CreateToolhelp32Snapshot"});
        CHECK(script.calls.create == 1U);
    }
    SECTION("failure with no last-error value is still an error") {
        script.scenario.snapshot_errors = {ERROR_SUCCESS};
        check_failure(script, ERROR_GEN_FAILURE, {"CreateToolhelp32Snapshot"});
        CHECK(script.calls.create == 1U);
    }
    SECTION("retry bound is enforced") {
        script.scenario.snapshot_errors.assign(8U, ERROR_BAD_LENGTH);
        check_failure(script, ERROR_BAD_LENGTH, {"CreateToolhelp32Snapshot"});
        CHECK(script.calls.create == 8U);
    }
    CHECK(script.calls.first == 0U);
    CHECK(script.calls.close == 0U);
    CHECK(script.calls.arguments_valid);
}

TEST_CASE("runtime_client.module_snapshot_enumeration", "[integration][runtime_client]") {
    snapshot_script script;
    const auto& expected = get_fixtures()[0];
    SECTION("match follows several nonmatching modules") {
        script.scenario.modules = {get_fixtures()[1], get_fixtures()[2], expected};
        CHECK(detail::module_is_loaded(expected, 123U, script));
        CHECK(script.calls.next == 2U);
    }
    SECTION("absence requires reaching the end of all modules") {
        script.scenario.modules = {get_fixtures()[1], get_fixtures()[2]};
        CHECK_FALSE(detail::module_is_loaded(expected, 123U, script));
        CHECK(script.calls.next == 2U);
    }
    SECTION("empty module list means absent") {
        script.scenario.modules.clear();
        CHECK_FALSE(detail::module_is_loaded(expected, 123U, script));
        CHECK(script.calls.next == 0U);
    }
    check_closed(script);
}

TEST_CASE("runtime_client.module_snapshot_path_identity", "[integration][runtime_client]") {
    snapshot_script script;
    const auto& expected = get_fixtures()[0];
    SECTION("same basename in another directory is a different module") {
        script.scenario.modules = {expected.parent_path() / L"another" / expected.filename()};
        CHECK_FALSE(detail::module_is_loaded(expected, 123U, script));
    }
    SECTION("module path comparison ignores case") {
        auto upper_path = expected.native();
        REQUIRE(CharUpperBuffW(upper_path.data(), static_cast<DWORD>(upper_path.size())) != 0);
        script.scenario.modules = {get_fixtures()[1], upper_path};
        CHECK(detail::module_is_loaded(expected, 123U, script));
        CHECK(script.calls.next == 1U);
    }
    SECTION("requested path is normalized") {
        const auto alias = expected.parent_path() / L"." / L"unused" / L".." / expected.filename();
        CHECK(detail::module_is_loaded(alias, 123U, script));
    }
    SECTION("enumerated path is normalized") {
        script.scenario.modules = {
            expected.parent_path() / L"." / L"unused" / L".." / expected.filename()};
        CHECK(detail::module_is_loaded(expected, 123U, script));
    }
    check_closed(script);
}

TEST_CASE("runtime_client.module_snapshot_enumeration_errors", "[integration][runtime_client]") {
    snapshot_script script;
    SECTION("first module failure") {
        script.scenario.first_error = ERROR_ACCESS_DENIED;
        check_failure(script, ERROR_ACCESS_DENIED, {"Module32FirstW"});
        CHECK(script.calls.next == 0U);
    }
    SECTION("next module failure") {
        script.scenario.modules = {get_fixtures()[1]};
        script.scenario.end_error = ERROR_READ_FAULT;
        check_failure(script, ERROR_READ_FAULT, {"Module32NextW"});
        CHECK(script.calls.next == 1U);
    }
    SECTION("first module failure without a last-error value") {
        script.scenario.first_error = ERROR_SUCCESS;
        check_failure(script, ERROR_GEN_FAILURE, {"Module32FirstW"});
    }
    SECTION("next module failure without a last-error value") {
        script.scenario.modules = {get_fixtures()[1]};
        script.scenario.end_error = ERROR_SUCCESS;
        check_failure(script, ERROR_GEN_FAILURE, {"Module32NextW"});
    }
    SECTION("enumeration error remains primary when close also fails") {
        script.scenario.first_error = ERROR_ACCESS_DENIED;
        script.scenario.close_error = ERROR_INVALID_HANDLE;
        check_failure(script, ERROR_ACCESS_DENIED, {"Module32FirstW", "CloseHandle", "(6)"});
    }
    check_closed(script);
}

TEST_CASE("runtime_client.module_snapshot_cleanup", "[integration][runtime_client]") {
    snapshot_script script;
    SECTION("close failure after a match") {
        script.scenario.close_error = ERROR_ACCESS_DENIED;
        check_failure(script, ERROR_ACCESS_DENIED, {"CloseHandle"});
    }
    SECTION("close failure after normal exhaustion") {
        script.scenario.modules.clear();
        script.scenario.close_error = ERROR_ACCESS_DENIED;
        check_failure(script, ERROR_ACCESS_DENIED, {"CloseHandle"});
    }
    SECTION("close failure without a last-error value") {
        script.scenario.close_error = ERROR_SUCCESS;
        check_failure(script, ERROR_GEN_FAILURE, {"CloseHandle"});
    }
    SECTION("snapshot closes while a malformed module path exception unwinds") {
        script.scenario.modules = {L"relative.dll"};
        check_failure(script, ERROR_BAD_PATHNAME, {"module snapshot returned a non-absolute path"});
    }
    check_closed(script);
}
} // namespace inputnext::test::runtime_client
