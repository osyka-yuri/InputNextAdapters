#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <input_next_core.h>
#include <input_next_effects.h>

#include <cstddef>
#include <filesystem>
#include <string>

namespace inputnext::test::runtime_client {

TEST_CASE("runtime_client.fixture_reset", "[integration][runtime_client]") {
    const auto& fixtures = get_fixtures();
    REQUIRE(!fixtures[0].empty());
    for (const auto& path : fixtures) {
        FixtureObserver observer(path);
        REQUIRE(observer);
        observer.reset();
    }
}

TEST_CASE("runtime_client.query_missing", "[integration][runtime_client]") {
    const auto& fixtures = get_fixtures();
    REQUIRE(!fixtures[2].empty());
    check_connect_failure(fixtures[2], ConnectError::query_missing, false);
}

TEST_CASE("runtime_client.query_unavailable", "[integration][runtime_client]") {
    const auto& fixtures = get_fixtures();
    REQUIRE(!fixtures[3].empty());
    check_connect_failure(fixtures[3], ConnectError::query_unavailable, true);
}

TEST_CASE("runtime_client.incompatible_api_variants", "[integration][runtime_client]") {
    const auto& fixtures = get_fixtures();
    REQUIRE(fixtures.size() == 16);
    for (std::size_t fixture_index = 4; fixture_index != fixtures.size(); ++fixture_index) {
        REQUIRE(!fixtures[fixture_index].empty());
        check_connect_failure(fixtures[fixture_index], ConnectError::incompatible_api, true);
    }
}

TEST_CASE("runtime_client.reconnect_after_incompatible", "[integration][runtime_client]") {
    const auto& fixtures = get_fixtures();
    REQUIRE(!fixtures[0].empty());
    REQUIRE(!fixtures[4].empty());
    REQUIRE(reset_fixture(fixtures[4]));
    REQUIRE(reset_fixture(fixtures[0]));
    test_reconnect_after_incompatible_api(fixtures[4], fixtures[0]);
}

TEST_CASE("runtime_client.scoped_disconnect", "[integration][runtime_client]") {
    const auto& fixtures = get_fixtures();
    REQUIRE(!fixtures[0].empty());
    REQUIRE(reset_fixture(fixtures[0]));
    test_scoped_disconnect(fixtures[0]);
}

TEST_CASE("runtime_client.disconnect_idempotence", "[integration][runtime_client]") {
    const auto& fixtures = get_fixtures();
    REQUIRE(!fixtures[0].empty());
    REQUIRE(reset_fixture(fixtures[0]));
    test_disconnect_idempotence(fixtures[0]);
}

TEST_CASE("runtime_client.retains_unpinned_provider_without_observer",
    "[integration][runtime_client]") {
    const auto& fixture = get_fixtures()[0];
    REQUIRE(!module_is_loaded(fixture));
    RuntimeClient client;
    REQUIRE(client.connect(fixture));
    REQUIRE(module_is_loaded(fixture));
    input_next_effects_progress_query_v1 query{};
    query.struct_size = sizeof(query);
    input_next_effects_progress_v1 progress{};
    REQUIRE(client.query_progress(query, progress) == INPUT_NEXT_STATUS_OK);
    client.disconnect();
    REQUIRE(!module_is_loaded(fixture));
}

TEST_CASE("runtime_client.connection_path_validation", "[integration][runtime_client]") {
    RuntimeClient client;
    REQUIRE(client.connect(std::filesystem::path(L"relative-runtime.dll")).error ==
            ConnectError::invalid_path);

    std::wstring embedded_null_path = L"C:\\runtime.dll";
    embedded_null_path.push_back(L'\0');
    embedded_null_path.append(L"tail");
    REQUIRE(client.connect(std::filesystem::path(embedded_null_path)).error ==
            ConnectError::invalid_path);

    std::wstring missing_name = L"inputnext_missing_runtime_";
    missing_name += std::to_wstring(GetCurrentProcessId());
    missing_name += L'_';
    missing_name += std::to_wstring(GetTickCount64());
    missing_name += L".dll";
    const auto missing_path = std::filesystem::temp_directory_path() / missing_name;

    REQUIRE(!std::filesystem::exists(missing_path));
    const auto load_result = client.connect(missing_path);
    REQUIRE(load_result.error == ConnectError::load_failed);
    REQUIRE(load_result.native_error != 0);
    REQUIRE(!client.connected());
}

} // namespace inputnext::test::runtime_client
