#include "native/skse_sdk.hpp"
#include "support/test_runner.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <type_traits>

namespace {
using LoadFunction = bool(__cdecl*)(const SKSEInterface*);
static_assert(std::is_same_v<LoadFunction, _SKSEPlugin_Load>);

enum class CallbackFailure : std::uint8_t { none, query_interface, plugin_handle };
enum class InterfaceFault : std::uint8_t {
    missing_task,
    old_task_version,
    missing_add_task,
    missing_messaging,
    old_messaging_version,
    missing_listener,
    missing_dispatcher,
    invalid_handle
};

struct MockInterfaces {
    SKSETaskInterface task{};
    SKSEMessagingInterface messaging{};
    PluginHandle handle{static_cast<PluginHandle>(kPluginHandle_Invalid)};
    CallbackFailure failure{};
    bool has_task{};
    bool has_messaging{};
    unsigned queries{};
    unsigned handles{};
    unsigned native_calls{};
} mock;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void* __cdecl query_interface(UInt32 id) {
    ++mock.queries;
    if (mock.failure == CallbackFailure::query_interface) {
        throw std::runtime_error{"Injected QueryInterface exception"};
    }
    if (id == kInterface_Task) {
        return mock.has_task ? &mock.task : nullptr;
    }
    if (id == kInterface_Messaging) {
        return mock.has_messaging ? &mock.messaging : nullptr;
    }
    return nullptr;
}

PluginHandle __cdecl get_plugin_handle() {
    ++mock.handles;
    if (mock.failure == CallbackFailure::plugin_handle) {
        throw std::runtime_error{"Injected GetPluginHandle exception"};
    }
    return mock.handle;
}

void __cdecl unexpected_add_task([[maybe_unused]] TaskDelegate* task) {
    ++mock.native_calls;
}

bool __cdecl unexpected_listener([[maybe_unused]] PluginHandle handle,
    [[maybe_unused]] const char* sender,
    [[maybe_unused]] SKSEMessagingInterface::EventCallback callback) {
    ++mock.native_calls;
    return false;
}

void* __cdecl unexpected_dispatcher([[maybe_unused]] UInt32 id) {
    ++mock.native_calls;
    return nullptr;
}

const char* configure_invalid_interfaces(InterfaceFault fault) {
    mock = {};
    mock.has_task = mock.has_messaging = true;
    mock.handle = 1;
    mock.task.interfaceVersion = SKSETaskInterface::kInterfaceVersion;
    mock.task.AddTask = &unexpected_add_task;
    mock.messaging.interfaceVersion = SKSEMessagingInterface::kInterfaceVersion;
    mock.messaging.RegisterListener = &unexpected_listener;
    mock.messaging.GetEventDispatcher = &unexpected_dispatcher;
    switch (fault) {
    case InterfaceFault::missing_task:
        mock.has_task = false;
        return "Missing Task interface accepted";
    case InterfaceFault::old_task_version:
        --mock.task.interfaceVersion;
        return "Old Task interface accepted";
    case InterfaceFault::missing_add_task:
        mock.task.AddTask = nullptr;
        return "Missing AddTask accepted";
    case InterfaceFault::missing_messaging:
        mock.has_messaging = false;
        return "Missing Messaging interface accepted";
    case InterfaceFault::old_messaging_version:
        --mock.messaging.interfaceVersion;
        return "Old Messaging interface accepted";
    case InterfaceFault::missing_listener:
        mock.messaging.RegisterListener = nullptr;
        return "Missing RegisterListener accepted";
    case InterfaceFault::missing_dispatcher:
        mock.messaging.GetEventDispatcher = nullptr;
        return "Missing GetEventDispatcher accepted";
    case InterfaceFault::invalid_handle:
        mock.handle = static_cast<PluginHandle>(kPluginHandle_Invalid);
        return "Invalid plugin handle accepted";
    }
    return "Invalid SKSE interface accepted";
}

class Module final {
public:
    explicit Module(const std::filesystem::path& path)
        : handle_(LoadLibraryExW(path.c_str(),
              nullptr,
              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (!handle_) {
            throw std::system_error{static_cast<int>(GetLastError()),
                std::system_category(),
                "Plugin or one of its dependencies could not be loaded"};
        }
    }

    ~Module() { FreeLibrary(handle_); }

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&&) = delete;
    Module& operator=(Module&&) = delete;

    [[nodiscard]] HMODULE get() const noexcept { return handle_; }

private:
    HMODULE handle_{};
};

void check_rejection(LoadFunction load,
    const SKSEInterface* api,
    CallbackFailure failure,
    unsigned expected_queries,
    unsigned expected_handles,
    const char* message = "Plugin accepted an invalid SKSE interface") {
    mock.failure = failure;
    mock.queries = mock.handles = mock.native_calls = 0;
    require(!load(api), message);
    require(mock.queries == expected_queries && mock.handles == expected_handles,
        "Rejected startup invoked unexpected SKSE callbacks");
    require(mock.native_calls == 0, "Rejected startup called a native registration or task API");
    require(GetModuleHandleW(L"inputnext_runtime.dll") == nullptr,
        "Rejected startup loaded the InputNext runtime");
}

int run_tests(int argc, const wchar_t* const* argv) {
    require(argc == 2, "Expected the absolute path to InputNextSkyrimLE.dll");
    const std::filesystem::path path{argv[1]};
    require(path.is_absolute(), "Plugin path must be absolute");
    require(GetModuleHandleW(L"inputnext_runtime.dll") == nullptr,
        "The package test must start without an InputNext runtime");
    {
        const Module module{path};
        const auto load =
            reinterpret_cast<LoadFunction>(GetProcAddress(module.get(), "SKSEPlugin_Load"));
        require(load != nullptr, "SKSEPlugin_Load is missing");
        check_rejection(load, nullptr, CallbackFailure::none, 0, 0);
        SKSEInterface api{};
        api.skseVersion = PACKED_SKSE_VERSION;
        api.runtimeVersion = RUNTIME_VERSION_1_9_32_0;
        api.QueryInterface = &query_interface;
        api.GetPluginHandle = &get_plugin_handle;
        api.isEditor = 1;
        check_rejection(load, &api, CallbackFailure::none, 0, 0);
        api.isEditor = 0;
        api.runtimeVersion = 0;
        check_rejection(load, &api, CallbackFailure::none, 0, 0);
        api.runtimeVersion = RUNTIME_VERSION_1_9_32_0;
        api.QueryInterface = nullptr;
        check_rejection(load, &api, CallbackFailure::none, 0, 0);
        api.QueryInterface = &query_interface;
        api.GetPluginHandle = nullptr;
        check_rejection(load, &api, CallbackFailure::none, 0, 0);
        api.GetPluginHandle = &get_plugin_handle;
        check_rejection(load, &api, CallbackFailure::none, 2, 1);
        check_rejection(load, &api, CallbackFailure::query_interface, 1, 0);
        check_rejection(load, &api, CallbackFailure::plugin_handle, 2, 1);
        constexpr std::array interface_faults{InterfaceFault::missing_task,
            InterfaceFault::old_task_version,
            InterfaceFault::missing_add_task,
            InterfaceFault::missing_messaging,
            InterfaceFault::old_messaging_version,
            InterfaceFault::missing_listener,
            InterfaceFault::missing_dispatcher,
            InterfaceFault::invalid_handle};
        for (const auto fault : interface_faults) {
            const auto* message = configure_invalid_interfaces(fault);
            check_rejection(load, &api, CallbackFailure::none, 2, 1, message);
        }
    }
    require(GetModuleHandleW(path.c_str()) == nullptr,
        "Rejected startup retained the plugin module");
    return 0;
}
} // namespace

extern "C" int wmain(int argc, wchar_t** argv) {
    return inputnext::test::run_main(__FILE__, [&] { return run_tests(argc, argv); });
}
