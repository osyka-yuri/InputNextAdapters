#include "native/skse_sdk.hpp"
#include "support/test_runner.hpp"

#include <Windows.h>

#include <array>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace {
using QueryFunction = bool(__cdecl*)(const SKSEInterface*, PluginInfo*);

static_assert(sizeof(void*) == 4, "Skyrim LE query tests require 32-bit x86 architecture");
static_assert(sizeof(PluginInfo) == 0x0C, "PluginInfo must be 12 bytes on x86");
static_assert(sizeof(SKSEInterface) == 0x1C, "SKSEInterface must be 28 bytes on x86");
static_assert(std::is_same_v<QueryFunction, _SKSEPlugin_Query>,
    "QueryFunction signature must match SKSE SDK _SKSEPlugin_Query typedef");

struct ScopedModule final {
    HMODULE handle{};

    explicit ScopedModule(HMODULE module_handle) noexcept : handle(module_handle) {}

    ~ScopedModule() noexcept {
        if (handle) {
            FreeLibrary(handle);
        }
    }

    ScopedModule(const ScopedModule&) = delete;
    ScopedModule& operator=(const ScopedModule&) = delete;

    ScopedModule(ScopedModule&& other) noexcept : handle(other.handle) { other.handle = nullptr; }

    ScopedModule& operator=(ScopedModule&& other) noexcept {
        if (this != &other) {
            if (handle) {
                FreeLibrary(handle);
            }
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return handle != nullptr; }
};

struct QueryCallCounters {
    int interface_queries{};
    int handle_queries{};
    int release_index_queries{};

    void reset() noexcept {
        interface_queries = 0;
        handle_queries = 0;
        release_index_queries = 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return interface_queries == 0 && handle_queries == 0 && release_index_queries == 0;
    }
};

QueryCallCounters g_counters{};
int g_failures{};

void* __cdecl unexpected_query_interface([[maybe_unused]] UInt32 id) {
    ++g_counters.interface_queries;
    return nullptr;
}

PluginHandle __cdecl unexpected_plugin_handle() {
    ++g_counters.handle_queries;
    return static_cast<PluginHandle>(kPluginHandle_Invalid);
}

UInt32 __cdecl unexpected_release_index() {
    ++g_counters.release_index_queries;
    return 0;
}

void check(bool condition, std::string_view label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << '\n';
        ++g_failures;
    }
}

SKSEInterface create_compatible_interface() {
    SKSEInterface api{};
    api.skseVersion = PACKED_SKSE_VERSION;
    api.runtimeVersion = RUNTIME_VERSION_1_9_32_0;
    api.isEditor = 0;
    api.QueryInterface = &unexpected_query_interface;
    api.GetPluginHandle = &unexpected_plugin_handle;
    api.GetReleaseIndex = &unexpected_release_index;
    return api;
}

bool has_expected_info(const PluginInfo& info) {
    return info.infoVersion == PluginInfo::kInfoVersion && info.name != nullptr &&
           std::strcmp(info.name, "InputNextSkyrimLE") == 0 && info.version == 1;
}

struct QueryTestCase {
    const char* description{};
    std::optional<SKSEInterface> api;
    bool pass_info{true};
    bool expected_result{false};
};

void run_query_case(QueryFunction query, const QueryTestCase& test_case) {
    g_counters.reset();
    PluginInfo info{};
    PluginInfo* info_ptr = test_case.pass_info ? &info : nullptr;
    const SKSEInterface* api_ptr = test_case.api.has_value() ? &test_case.api.value() : nullptr;

    const bool actual_result = query(api_ptr, info_ptr);

    check(actual_result == test_case.expected_result, test_case.description);

    if (test_case.pass_info) {
        check(has_expected_info(info),
            std::string(test_case.description) + ": PluginInfo is properly populated");
    }

    check(g_counters.empty(),
        std::string(test_case.description) + ": no SKSE API callbacks invoked");
}

const std::array<QueryTestCase, 15>& query_test_cases() {
    static const auto compatible_api = create_compatible_interface();

    static auto editor_api = create_compatible_interface();
    editor_api.isEditor = 1;

    static auto older_runtime_api = create_compatible_interface();
    older_runtime_api.runtimeVersion = RUNTIME_VERSION_1_9_29_0;

    static auto newer_runtime_api = create_compatible_interface();
    newer_runtime_api.runtimeVersion = RUNTIME_VERSION_1_9_32_0 + 1;

    static auto zero_runtime_api = create_compatible_interface();
    zero_runtime_api.runtimeVersion = 0;

    static auto older_skse_api = create_compatible_interface();
    older_skse_api.skseVersion = PACKED_SKSE_VERSION - 1;

    static auto newer_skse_api = create_compatible_interface();
    newer_skse_api.skseVersion = PACKED_SKSE_VERSION + 1;

    static auto zero_skse_api = create_compatible_interface();
    zero_skse_api.skseVersion = 0;

    static const std::array<QueryTestCase, 15> cases{{
        {
            .description = "compatible Skyrim 1.9.32.0 and SKSE 1.7.3 with PluginInfo",
            .api = compatible_api,
            .pass_info = true,
            .expected_result = true,
        },
        {
            .description = "compatible Skyrim 1.9.32.0 and SKSE 1.7.3 without PluginInfo",
            .api = compatible_api,
            .pass_info = false,
            .expected_result = true,
        },
        {
            .description = "future compatible SKSE version accepted with PluginInfo",
            .api = newer_skse_api,
            .pass_info = true,
            .expected_result = true,
        },
        {
            .description = "editor mode rejected with PluginInfo",
            .api = editor_api,
            .pass_info = true,
            .expected_result = false,
        },
        {
            .description = "editor mode rejected without PluginInfo",
            .api = editor_api,
            .pass_info = false,
            .expected_result = false,
        },
        {
            .description = "older Skyrim runtime 1.9.29.0 rejected with PluginInfo",
            .api = older_runtime_api,
            .pass_info = true,
            .expected_result = false,
        },
        {
            .description = "older Skyrim runtime 1.9.29.0 rejected without PluginInfo",
            .api = older_runtime_api,
            .pass_info = false,
            .expected_result = false,
        },
        {
            .description = "unknown newer Skyrim runtime rejected with PluginInfo",
            .api = newer_runtime_api,
            .pass_info = true,
            .expected_result = false,
        },
        {
            .description = "zero Skyrim runtime rejected with PluginInfo",
            .api = zero_runtime_api,
            .pass_info = true,
            .expected_result = false,
        },
        {
            .description = "older SKSE version rejected with PluginInfo",
            .api = older_skse_api,
            .pass_info = true,
            .expected_result = false,
        },
        {
            .description = "older SKSE version rejected without PluginInfo",
            .api = older_skse_api,
            .pass_info = false,
            .expected_result = false,
        },
        {
            .description = "zero SKSE version rejected with PluginInfo",
            .api = zero_skse_api,
            .pass_info = true,
            .expected_result = false,
        },
        {
            .description = "null SKSE interface rejected with PluginInfo",
            .api = std::nullopt,
            .pass_info = true,
            .expected_result = false,
        },
        {
            .description = "null SKSE interface rejected without PluginInfo",
            .api = std::nullopt,
            .pass_info = false,
            .expected_result = false,
        },
        {
            .description = "repeat null SKSE interface check with freshly cleared PluginInfo",
            .api = std::nullopt,
            .pass_info = true,
            .expected_result = false,
        },
    }};

    return cases;
}

int run_tests(int argc, const wchar_t* const* argv) {
    if (argc != 2) {
        std::cerr << "usage: skyrim_le_query_tests <plugin.dll>\n";
        return 2;
    }

    const ScopedModule module{LoadLibraryW(argv[1])};
    if (!module) {
        std::cerr << "unable to load test plugin, Win32 error " << GetLastError() << '\n';
        return 2;
    }

    FARPROC address = GetProcAddress(module.handle, "SKSEPlugin_Query");
    check(address != nullptr, "undecorated SKSEPlugin_Query export exists");
    if (!address) {
        return 1;
    }

    // Verified properties and scope:
    // - GetProcAddress confirms undecorated "SKSEPlugin_Query" export symbol.
    // - QueryFunction is statically checked against the SKSE SDK __cdecl typedef.
    // - Parameter validation across test cases, including optional null PluginInfo.
    // - Verifies instrumented SKSEInterface callbacks are not invoked.
    // Limitation: Out-of-process harness execution does not verify TESV.exe runtime ABI,
    // engine heap allocation, or game hooks.
    const auto query = reinterpret_cast<QueryFunction>(address);

    for (const auto& test_case : query_test_cases()) {
        run_query_case(query, test_case);
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " plugin query checks failed\n";
        return 1;
    }

    std::cout << "Skyrim LE query checks passed\n";
    return 0;
}
} // namespace

extern "C" int wmain(int argc, wchar_t** argv) {
    return inputnext::test::run_main(__FILE__, [&] { return run_tests(argc, argv); });
}
