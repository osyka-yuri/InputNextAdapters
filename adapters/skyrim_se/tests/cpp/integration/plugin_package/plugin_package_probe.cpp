#include <Windows.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace SKSE {
class LoadInterface;
}

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

class Module final {
public:
    explicit Module(const std::filesystem::path& path)
        : handle_(LoadLibraryExW(path.c_str(),
              nullptr,
              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        if (handle_ == nullptr) {
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
} // namespace

extern "C" int wmain(int argc, wchar_t* const* argv) try {
    require(argc == 2, "Expected the absolute path to InputNextSkyrim.dll");
    const std::filesystem::path path{argv[1]};
    require(path.is_absolute(), "Plugin path must be absolute");
    require(GetModuleHandleW(L"inputnext_runtime.dll") == nullptr,
        "The package probe must start without an InputNext runtime");
    {
        const Module module{path};
        using LoadFunction = bool (*)(const SKSE::LoadInterface*);
        const auto load =
            reinterpret_cast<LoadFunction>(GetProcAddress(module.get(), "SKSEPlugin_Load"));
        require(load != nullptr, "SKSEPlugin_Load is missing");
        require(GetProcAddress(module.get(), "SKSEPlugin_Query") != nullptr,
            "SKSEPlugin_Query is missing");
        require(GetProcAddress(module.get(), "SKSEPlugin_Version") != nullptr,
            "SKSEPlugin_Version is missing");
        require(!load(nullptr), "Plugin accepted a missing SKSE interface");
        require(GetModuleHandleW(L"inputnext_runtime.dll") == nullptr,
            "Rejected plugin startup loaded the runtime");
    }
    require(GetModuleHandleW(path.c_str()) == nullptr,
        "Rejected plugin startup retained the plugin module");
    return 0;
} catch (const std::exception& error) {
    static_cast<void>(std::fputs(error.what(), stderr));
    static_cast<void>(std::fputc('\n', stderr));
    return 1;
} catch (...) {
    static_cast<void>(std::fputs("Unexpected plugin package probe failure\n", stderr));
    return 1;
}
