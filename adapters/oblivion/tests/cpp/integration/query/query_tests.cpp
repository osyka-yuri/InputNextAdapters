#include "native/xobse_sdk.hpp"
#include "support/test_runner.hpp"

#include <Windows.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {

using QueryFunction = bool(__cdecl*)(const OBSEInterface*, PluginInfo*);

struct ScopedModule final {
    HMODULE handle{};

    explicit ScopedModule(const char* path) noexcept : handle(LoadLibraryA(path)) {}

    ~ScopedModule() noexcept {
        if (handle) {
            FreeLibrary(handle);
        }
    }

    ScopedModule(const ScopedModule&) = delete;
    ScopedModule& operator=(const ScopedModule&) = delete;
    ScopedModule(ScopedModule&&) = delete;
    ScopedModule& operator=(ScopedModule&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return handle != nullptr; }
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void* __cdecl dummy_query_interface([[maybe_unused]] UInt32 id) {
    return nullptr;
}

PluginHandle __cdecl dummy_plugin_handle() {
    return 1;
}

OBSEInterface make_compatible_interface() {
    OBSEInterface obse{};
    obse.obseVersion = 22;
    obse.oblivionVersion = OBLIVION_VERSION_1_2_416;
    obse.isEditor = 0;
    obse.QueryInterface = &dummy_query_interface;
    obse.GetPluginHandle = &dummy_plugin_handle;
    return obse;
}

} // namespace

int main(int argc, const char* const* argv) {
    return inputnext::test::run_main("Oblivion query tests", [&] -> int {
        require(argc > 1, "Expected plugin DLL path as first argument");
        const char* plugin_path = argv[1];

        const ScopedModule module(plugin_path);
        require(static_cast<bool>(module), "Failed to load InputNextOblivion DLL");

        auto query_fn =
            reinterpret_cast<QueryFunction>(GetProcAddress(module.handle, "OBSEPlugin_Query"));
        require(GetProcAddress(module.handle, "OBSEPlugin_Load") != nullptr,
            "The OBSEPlugin_Load export must be available");
        if (query_fn == nullptr) {
            throw std::runtime_error{"OBSEPlugin_Query export not found in DLL"};
        }

        {
            const auto obse = make_compatible_interface();
            PluginInfo info{};
            const bool result = query_fn(&obse, &info);
            require(result, "Expected compatible query to succeed");
            require(info.infoVersion == PluginInfo::kInfoVersion, "Info version match");
            require(info.name != nullptr && std::strcmp(info.name, "InputNextOblivion") == 0,
                "Plugin name match");
            require(info.version == 1, "Plugin version 1");
        }

        {
            const auto obse = make_compatible_interface();
            const bool result = query_fn(&obse, nullptr);
            require(result, "Compatible query with null info should still succeed");
        }

        {
            PluginInfo info{};
            const bool result = query_fn(nullptr, &info);
            require(!result, "Query with null OBSEInterface must fail");
        }

        {
            auto obse = make_compatible_interface();
            obse.isEditor = 1;
            PluginInfo info{};
            const bool result = query_fn(&obse, &info);
            require(!result, "Query in editor mode must fail");
        }

        {
            auto obse = make_compatible_interface();
            obse.oblivionVersion = 0x01020000;
            PluginInfo info{};
            const bool result = query_fn(&obse, &info);
            require(!result, "Query with wrong Oblivion version must fail");
        }

        {
            auto obse = make_compatible_interface();
            obse.obseVersion = 21;
            PluginInfo info{};
            const bool result = query_fn(&obse, &info);
            require(!result, "Query with obseVersion < 22 must fail");
        }

        {
            auto obse = make_compatible_interface();
            obse.QueryInterface = nullptr;
            PluginInfo info{};
            const bool result = query_fn(&obse, &info);
            require(!result, "Query without QueryInterface must fail");
        }

        {
            auto obse = make_compatible_interface();
            obse.GetPluginHandle = nullptr;
            PluginInfo info{};
            const bool result = query_fn(&obse, &info);
            require(!result, "Query without GetPluginHandle must fail");
        }

        std::cout << "All Oblivion query tests passed.\n";
        return 0;
    });
}
