#include "effect_model.hpp"
#include "model.hpp"
#include "native/effect_worker.hpp"
#include "support/test_runner.hpp"

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
using namespace std::chrono_literals;
using namespace oblivion_adapter;
using namespace oblivion_adapter::native;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

bool has_focus() noexcept {
    return true;
}

void verify_active_shutdown(const std::filesystem::path& runtime, const char* mode) {
    static_assert(!std::is_destructible_v<EffectWorker>);
    {
        auto const prepared = EffectWorker::prepare(runtime.parent_path(), has_focus);
        require(prepared != nullptr && (GetModuleHandleW(runtime.c_str()) != nullptr),
            "A prepared worker retains the runtime before callback registration");
    }
    require(GetModuleHandleW(runtime.c_str()) == nullptr,
        "Aborting a prepared worker joins it and releases the unopened runtime");

    const auto free_module = [](HMODULE module) noexcept {
        FreeLibrary(module);
    };
    std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(free_module)> dll{
        LoadLibraryExW(runtime.c_str(),
            nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    require(dll != nullptr, "Load the runtime fixture for call counts");
    using ReadCalls = std::uint32_t(__cdecl*)(bool) noexcept;
    const auto calls =
        reinterpret_cast<ReadCalls>(GetProcAddress(dll.get(), "InputNext_TestWorkerCalls"));
    if (calls == nullptr) {
        throw std::runtime_error{"Find the fixture's call counter"};
    }

    require(SetEnvironmentVariableA("INPUTNEXT_OBLIVION_TEST_RUNTIME_MODE", mode) != FALSE,
        "Configure the fixture's close response");
    auto prepared = EffectWorker::prepare(runtime.parent_path(), has_focus);
    require(prepared != nullptr, "Prepare a native worker with the valid fixture ABI");
    auto& worker = EffectWorker::activate_for_process(std::move(prepared));
    dll.reset();

    const auto deadline = MonoTime::clock::now() + 1s;
    for (std::uint64_t frame = 1; calls(false) == 0 && MonoTime::clock::now() < deadline; ++frame) {
        worker.publish({.sampled_at = MonoTime::clock::now(),
            .epoch = 1,
            .frame = frame,
            .mode = EffectMode::gameplay,
            .alive = true,
            .health = calculate_health(100.0F, 100.0F),
            .player_id = 0x14});
        std::this_thread::sleep_for(5ms);
    }
    require(calls(false) == 1, "Shutdown starts after the worker acquired an output context");
    require(calls(true) == 0, "The context is still open when shutdown starts");

    const auto stopped_at = MonoTime::clock::now();
    worker.stop();
    require(MonoTime::clock::now() - stopped_at < 200ms,
        "Stopping a committed worker does not wait for the runtime to close");
    std::this_thread::sleep_until(stopped_at + 750ms);
    const auto close_count = calls(true);
    require(close_count >= 2, "Shutdown retries the retained exact context within its window");
    std::this_thread::sleep_for(150ms);
    require(calls(true) == close_count, "Persistent errors do not trigger endless exit retries");
    require(GetModuleHandleW(runtime.c_str()) != nullptr,
        "The active owner keeps the runtime and unresolved cleanup record until process exit");
    require(calls(false) == 1, "No replacement context can open during shutdown");
}
} // namespace

int main(int argc, char** argv) {
    return inputnext::test::run_main("Oblivion worker shutdown", [&] {
        require(argc == 3, "Provide the isolated runtime fixture path and close response");
        const auto runtime = std::filesystem::absolute(argv[1]).make_preferred();
        verify_active_shutdown(runtime, argv[2]);
        return 0;
    });
}
