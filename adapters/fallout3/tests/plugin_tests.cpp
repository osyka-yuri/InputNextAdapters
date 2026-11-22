#include "fose_sdk.hpp"
#include "support/test_runner.hpp"

#include <Windows.h>

#include <fose/fose/PluginAPI.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace {

using QueryFunction = bool(__cdecl*)(const FOSEInterface*, PluginInfo*);
using LoadFunction = bool(__cdecl*)(const FOSEInterface*);

struct ScopedModule final {
    HMODULE handle{};

    explicit ScopedModule(const char* path) noexcept : handle(LoadLibraryA(path)) {}

    ~ScopedModule() noexcept {
        if (handle != nullptr) {
            FreeLibrary(handle);
        }
    }

    ScopedModule(const ScopedModule&) = delete;
    ScopedModule& operator=(const ScopedModule&) = delete;
    ScopedModule(ScopedModule&&) = delete;
    ScopedModule& operator=(ScopedModule&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return handle != nullptr; }
};

struct ScopedDirectory final {
    std::filesystem::path path;

    explicit ScopedDirectory(std::filesystem::path directory) : path{std::move(directory)} {}

    ~ScopedDirectory() noexcept {
        try {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        } catch (...) {
            OutputDebugStringA(
                "Fallout 3 plugin test could not clean up its temporary directory.\r\n");
        }
    }

    ScopedDirectory(const ScopedDirectory&) = delete;
    ScopedDirectory& operator=(const ScopedDirectory&) = delete;
    ScopedDirectory(ScopedDirectory&&) = delete;
    ScopedDirectory& operator=(ScopedDirectory&&) = delete;
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] FOSEInterface make_interface(UInt32 fose_version) noexcept {
    FOSEInterface fose{};
    fose.foseVersion = fose_version;
    fose.runtimeVersion = fallout3_adapter::native::supported_runtime_version;
    return fose;
}

} // namespace

int main(int argc, const char* const* argv) {
    return inputnext::test::run_main("Fallout 3 plugin tests", [&] -> int {
        require(argc > 1, "Expected plugin DLL path as first argument");
        const auto plugin_path = std::filesystem::absolute(argv[1]);
        const ScopedModule module(plugin_path.string().c_str());
        require(static_cast<bool>(module), "Failed to load InputNextFallout3 DLL");

        const auto query =
            reinterpret_cast<QueryFunction>(GetProcAddress(module.handle, "FOSEPlugin_Query"));
        const auto load =
            reinterpret_cast<LoadFunction>(GetProcAddress(module.handle, "FOSEPlugin_Load"));
        if (query == nullptr) {
            throw std::runtime_error{"FOSEPlugin_Query export not found"};
        }
        if (load == nullptr) {
            throw std::runtime_error{"FOSEPlugin_Load export not found"};
        }

        {
            PluginInfo info{};
            const auto fose = make_interface(fallout3_adapter::native::supported_fose_version);
            require(query(&fose, &info),
                "FOSE 1.3 beta 2 and Fallout 3 1.7.0.3 should be accepted");
            require(info.infoVersion == PluginInfo::kInfoVersion, "FOSE plugin info version match");
            require(info.name != nullptr && std::string{info.name} == "InputNextFallout3",
                "FOSE plugin name match");
            require(info.version == 1, "Plugin version match");
        }

        {
            PluginInfo info{};
            const auto newer_fose = make_interface(fallout3_adapter::native::fose_version(1, 4, 0));
            require(!query(&newer_fose, &info), "Unverified FOSE versions must be rejected");
        }

        {
            PluginInfo info{};
            const auto fose = make_interface(fallout3_adapter::native::fose_version(1, 2, 2));
            require(!query(&fose, &info), "FOSE versions other than 1.3 beta 2 must be rejected");
        }

        {
            PluginInfo info{};
            auto fose = make_interface(fallout3_adapter::native::supported_fose_version);
            fose.runtimeVersion = fallout3_adapter::native::fose_version(1, 7, 0);
            require(!query(&fose, &info), "Unsupported Fallout runtimes must be rejected");
        }

        {
            auto fose = make_interface(fallout3_adapter::native::supported_fose_version);
            fose.isEditor = 1;
            PluginInfo info{};
            require(!query(&fose, &info), "The editor must be rejected");
            require(!query(&fose, nullptr), "A null plugin info must be rejected");
            require(!query(nullptr, &info), "A null FOSE interface must be rejected");
        }

        const auto fose = make_interface(fallout3_adapter::native::supported_fose_version);
        const auto log_path = plugin_path.parent_path() / "InputNextFallout3.log";
        std::error_code log_error;
        std::filesystem::remove(log_path, log_error);
        require(!log_error, "The previous test log must be removable");
        require(!load(nullptr), "Load must reject a null FOSE interface");
        const auto unsupported_fose =
            make_interface(fallout3_adapter::native::fose_version(1, 2, 2));
        require(!load(&unsupported_fose), "Load must reject unverified FOSE versions");
        require(load(&fose), "Load should succeed with supported versions");
        require(load(&fose), "Repeated load must remain successful");

        std::ifstream log(log_path, std::ios::binary);
        require(static_cast<bool>(log), "The startup log must be written beside the plugin");
        const std::string content((std::istreambuf_iterator<char>(log)),
            std::istreambuf_iterator<char>());
        const auto marker = std::string{"InputNextFallout3 0.1.0 loaded through FOSE 1.3 beta 2"};
        require(content.contains(marker), "The startup log must identify supported versions");
        require(content.find(marker, content.find(marker) + marker.size()) == std::string::npos,
            "One process must write only one successful startup log entry");

        const ScopedDirectory receipt_blocked_directory{
            std::filesystem::temp_directory_path() /
            ("InputNextFallout3-plugin-test-" + std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(GetTickCount64()))};
        require(std::filesystem::create_directory(receipt_blocked_directory.path),
            "The temporary receipt failure directory must be created");
        const auto blocked_plugin_path = receipt_blocked_directory.path / "InputNextFallout3.dll";
        require(std::filesystem::copy_file(plugin_path, blocked_plugin_path),
            "The plugin DLL must be copied to the receipt failure directory");
        const auto blocked_log_path = receipt_blocked_directory.path / "InputNextFallout3.log";
        require(std::filesystem::create_directory(blocked_log_path),
            "A directory at the receipt path must prevent opening the log as a file");

        const ScopedModule blocked_module(blocked_plugin_path.string().c_str());
        require(static_cast<bool>(blocked_module),
            "The copied plugin must load for the receipt failure test");
        const auto blocked_load = reinterpret_cast<LoadFunction>(
            GetProcAddress(blocked_module.handle, "FOSEPlugin_Load"));
        if (blocked_load == nullptr) {
            throw std::runtime_error{"FOSEPlugin_Load must be exported by the copied plugin"};
        }
        require(blocked_load(&fose),
            "A compatible FOSE plugin must load when the optional startup receipt cannot be "
            "written");
        require(blocked_load(&fose), "Repeated load must remain successful after receipt failure");
        require(std::filesystem::is_directory(blocked_log_path),
            "The failed receipt must not replace the path that prevented its creation");

        std::cout << "All Fallout 3 plugin tests passed.\n";
        return 0;
    });
}
