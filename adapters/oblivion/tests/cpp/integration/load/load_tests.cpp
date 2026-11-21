#include "load_scenarios.hpp"
#include "support/test_runner.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace oblivion_adapter::test {

namespace {

std::string get_executable_path() {
    std::string path;
    path.resize(MAX_PATH);
    for (;;) {
        const DWORD len = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (len == 0) {
            throw std::runtime_error(
                "GetModuleFileNameA failed: " + std::to_string(GetLastError()));
        }
        if (len < path.size()) {
            path.resize(len);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::string quote_argument(const std::string& arg) {
    if (arg.empty()) {
        return "\"\"";
    }
    if (arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg;
    }
    std::string out = "\"";
    for (auto it = arg.begin(); it != arg.end(); ++it) {
        unsigned backslashes = 0;
        while (it != arg.end() && *it == '\\') {
            ++backslashes;
            ++it;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (*it == '"') {
            out.append((backslashes * 2) + 1, '\\');
            out.push_back(*it);
        } else {
            out.append(backslashes, '\\');
            out.push_back(*it);
        }
    }
    out.push_back('"');
    return out;
}

struct ScopedTempDir {
    std::filesystem::path path;

    ScopedTempDir() {
        static std::atomic<std::uint64_t> counter{0};
        const auto pid = GetCurrentProcessId();
        const auto base_temp = std::filesystem::temp_directory_path();

        constexpr int kMaxAttempts = 100;
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
            const auto tick = GetTickCount64();
            const auto seq = counter.fetch_add(1, std::memory_order_relaxed);
            const std::string dirname = "inputnext_test_spaces_" + std::to_string(pid) + "_" +
                                        std::to_string(tick) + "_" + std::to_string(seq);
            const auto candidate = base_temp / dirname;
            std::error_code ec;
            if (std::filesystem::create_directory(candidate, ec) && !ec) {
                path = candidate;
                return;
            }
            if (ec) {
                throw std::runtime_error("Failed to create temporary directory '" +
                                         candidate.string() + "': " + ec.message());
            }
        }
        throw std::runtime_error(
            "Failed to create a unique temporary directory after multiple attempts");
    }

    ~ScopedTempDir() noexcept {
        try {
            if (!path.empty()) {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } catch (const std::exception& ex) {
            OutputDebugStringA(ex.what());
        } catch (...) {
            OutputDebugStringA("ScopedTempDir: unknown cleanup failure\n");
        }
    }

    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;
    ScopedTempDir(ScopedTempDir&&) = delete;
    ScopedTempDir& operator=(ScopedTempDir&&) = delete;
};

enum class ProcessResult : std::uint8_t {
    Success,
    LaunchFailed,
    TimedOut,
    TerminationFailed,
    WaitFailed,
    NonZeroExit,
};

const char* process_result_name(ProcessResult result) noexcept {
    switch (result) {
    case ProcessResult::Success:
        return "Success";
    case ProcessResult::LaunchFailed:
        return "LaunchFailed";
    case ProcessResult::TimedOut:
        return "TimedOut";
    case ProcessResult::TerminationFailed:
        return "TerminationFailed";
    case ProcessResult::WaitFailed:
        return "WaitFailed";
    case ProcessResult::NonZeroExit:
        return "NonZeroExit";
    }
    return "Unknown";
}

struct ScenarioProcessOutcome {
    ProcessResult result{ProcessResult::LaunchFailed};
    DWORD exit_code{0};
    DWORD error_code{0};

    [[nodiscard]] bool success() const noexcept { return result == ProcessResult::Success; }

    [[nodiscard]] std::string describe() const {
        std::string desc = process_result_name(result);
        if (result == ProcessResult::NonZeroExit) {
            desc += " (exit_code=" + std::to_string(exit_code) + ")";
        } else if (result == ProcessResult::LaunchFailed || result == ProcessResult::WaitFailed ||
                   result == ProcessResult::TerminationFailed) {
            desc += " (error_code=" + std::to_string(error_code) + ")";
        }
        return desc;
    }
};

bool ensure_process_terminated(HANDLE process,
    DWORD timeout_ms = 5000,
    DWORD* out_error = nullptr) {
    DWORD exit_code = STILL_ACTIVE;
    if (GetExitCodeProcess(process, &exit_code) && exit_code != STILL_ACTIVE) {
        return true;
    }

    if (!TerminateProcess(process, 1)) {
        const DWORD err = GetLastError();
        if (GetExitCodeProcess(process, &exit_code) && exit_code != STILL_ACTIVE) {
            return true;
        }
        if (out_error) {
            *out_error = err;
        }
        return false;
    }

    const DWORD term_wait = WaitForSingleObject(process, timeout_ms);
    if (term_wait == WAIT_OBJECT_0) {
        return true;
    }

    if (out_error) {
        *out_error = (term_wait == WAIT_FAILED) ? GetLastError() : ERROR_TIMEOUT;
    }
    return false;
}

ScenarioProcessOutcome await_scenario_process(HANDLE process, DWORD timeout_ms) noexcept {
    ScenarioProcessOutcome outcome{};
    const DWORD wait_res = WaitForSingleObject(process, timeout_ms);
    if (wait_res == WAIT_TIMEOUT) {
        DWORD exit_code = STILL_ACTIVE;
        if (GetExitCodeProcess(process, &exit_code) && exit_code != STILL_ACTIVE) {
            outcome.exit_code = exit_code;
            outcome.result = (exit_code == 0) ? ProcessResult::Success : ProcessResult::NonZeroExit;
            return outcome;
        }

        DWORD term_err = 0;
        if (!ensure_process_terminated(process, 5000, &term_err)) {
            outcome.result = ProcessResult::TerminationFailed;
            outcome.error_code = term_err;
            return outcome;
        }

        GetExitCodeProcess(process, &outcome.exit_code);
        outcome.result = ProcessResult::TimedOut;
        return outcome;
    }

    if (wait_res != WAIT_OBJECT_0) {
        outcome.result = ProcessResult::WaitFailed;
        outcome.error_code = GetLastError();

        DWORD term_err = 0;
        if (!ensure_process_terminated(process, 5000, &term_err)) {
            outcome.result = ProcessResult::TerminationFailed;
            outcome.error_code = term_err;
        }
        return outcome;
    }

    DWORD exit_code = 1;
    if (!GetExitCodeProcess(process, &exit_code)) {
        outcome.result = ProcessResult::WaitFailed;
        outcome.error_code = GetLastError();

        DWORD term_err = 0;
        if (!ensure_process_terminated(process, 5000, &term_err)) {
            outcome.result = ProcessResult::TerminationFailed;
            outcome.error_code = term_err;
        }
        return outcome;
    }

    outcome.exit_code = exit_code;
    outcome.result = (exit_code == 0) ? ProcessResult::Success : ProcessResult::NonZeroExit;
    return outcome;
}

ScenarioProcessOutcome run_scenario_process(const char* exe_path,
    const char* scenario_name,
    const char* plugin_path,
    DWORD timeout_ms = 15000) {
    ScenarioProcessOutcome outcome{};
    const std::string cmd_line = quote_argument(exe_path) + " --scenario " +
                                 quote_argument(scenario_name) + " " + quote_argument(plugin_path);

    STARTUPINFOA startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    std::vector<char> cmd_buf(cmd_line.begin(), cmd_line.end());
    cmd_buf.push_back('\0');

    const BOOL created = CreateProcessA(nullptr,
        cmd_buf.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startup_info,
        &process_info);

    if (!created) {
        outcome.result = ProcessResult::LaunchFailed;
        outcome.error_code = GetLastError();
        return outcome;
    }

    CloseHandle(process_info.hThread);
    outcome = await_scenario_process(process_info.hProcess, timeout_ms);
    CloseHandle(process_info.hProcess);
    return outcome;
}

void verify_process_runner(const char* exe_path, const char* plugin_path) {
    {
        const auto outcome = run_scenario_process("C:\\non_existent_runner_exe_12345.exe",
            "compatible_load",
            plugin_path,
            1000);
        require(outcome.result == ProcessResult::LaunchFailed,
            ("Expected LaunchFailed for non-existent exe, got: " + outcome.describe()).c_str());
    }

    {
        const auto outcome =
            run_scenario_process(exe_path, "__internal_runner_fail", plugin_path, 5000);
        require(outcome.result == ProcessResult::NonZeroExit && outcome.exit_code == 42,
            ("Expected NonZeroExit(42), got: " + outcome.describe()).c_str());
    }

    {
        const auto outcome =
            run_scenario_process(exe_path, "__internal_runner_hang", plugin_path, 200);
        require(outcome.result == ProcessResult::TimedOut,
            ("Expected TimedOut for hanging process, got: " + outcome.describe()).c_str());
        require(outcome.exit_code != STILL_ACTIVE,
            "Terminated child process must not remain active");
    }

    {
        const ScopedTempDir temp_dir;
        const auto spaced_dll = temp_dir.path / "InputNext Oblivion.dll";
        std::error_code ec;
        std::filesystem::copy_file(plugin_path,
            spaced_dll,
            std::filesystem::copy_options::overwrite_existing,
            ec);
        require(!ec, "Failed to copy DLL to temporary directory with spaces");

        const auto outcome =
            run_scenario_process(exe_path, "compatible_load", spaced_dll.string().c_str(), 15000);
        require(outcome.success(),
            ("Scenario with spaces in path failed: " + outcome.describe()).c_str());
    }
}

struct CommandLineOptions {
    std::string scenario_arg;
    std::string plugin_path;
};

CommandLineOptions parse_command_line(int argc, const char* const* argv) {
    CommandLineOptions options;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            options.scenario_arg = argv[++i];
        } else if (options.plugin_path.empty()) {
            options.plugin_path = argv[i];
        }
    }
    return options;
}

int handle_scenario_dispatch(const std::string& scenario_arg, const char* plugin_path) {
    if (scenario_arg == "__internal_runner_hang") {
        Sleep(3000);
        return 0;
    }
    if (scenario_arg == "__internal_runner_fail") {
        return 42;
    }

    if (!run_scenario(scenario_arg, plugin_path)) {
        std::cerr << "Unknown scenario: " << scenario_arg << "\n";
        return 1;
    }
    return 0;
}

} // namespace

} // namespace oblivion_adapter::test

int main(int argc, const char* const* argv) {
    return inputnext::test::run_main("Oblivion load tests", [&] -> int {
        using namespace oblivion_adapter::test;

        if (argc < 2) {
            std::cerr << "Usage: oblivion_load_tests [--scenario <name>] <plugin_path>\n";
            return 1;
        }

        const auto options = parse_command_line(argc, argv);
        if (options.plugin_path.empty()) {
            std::cerr << "Error: missing plugin DLL path\n";
            return 1;
        }

        if (!options.scenario_arg.empty()) {
            return handle_scenario_dispatch(options.scenario_arg, options.plugin_path.c_str());
        }

        const std::string exe_path = get_executable_path();
        verify_process_runner(exe_path.c_str(), options.plugin_path.c_str());

        const auto& scenarios = get_scenarios();
        for (const auto& scenario : scenarios) {
            const auto outcome =
                run_scenario_process(exe_path.c_str(), scenario.name, options.plugin_path.c_str());
            if (!outcome.success()) {
                throw std::runtime_error(
                    "Scenario [" + std::string(scenario.name) + "] failed: " + outcome.describe());
            }
        }
        std::cout << "All " << scenarios.size()
                  << " Oblivion load scenarios passed in isolated processes.\n";
        return 0;
    });
}
