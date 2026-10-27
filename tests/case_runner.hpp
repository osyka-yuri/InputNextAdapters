#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace inputnext::test {
struct Case {
    std::string_view id;
    std::function<bool()> run;
};

// Each callable owns its fixture lifetime. A failed case does not prevent the remaining cases from running.
inline int run_cases(std::span<const Case> cases, std::span<const std::string_view> arguments) {
    const bool list = arguments.size() == 1 && arguments[0] == "--list";
    const bool selected = arguments.size() == 2 && arguments[0] == "--case";
    if (!arguments.empty() && !list && !selected) {
        std::cerr << "Usage: [--list | --case <id>]\n";
        return 2;
    }
    if (selected) {
        bool found{};
        for (const auto& test_case : cases) {
            found = found || test_case.id == arguments[1];
        }
        if (!found) {
            std::cerr << "Unknown test case: " << arguments[1] << '\n';
            return 2;
        }
    }
    int failures{};
    for (const auto& test_case : cases) {
        if (list) {
            std::cout << test_case.id << '\n';
            continue;
        }
        if (selected && test_case.id != arguments[1]) {
            continue;
        }
        bool passed{};
        try {
            passed = test_case.run();
        } catch (const std::exception& exception) {
            std::cerr << "Case " << test_case.id << ": " << exception.what() << '\n';
        } catch (...) {
            std::cerr << "Case " << test_case.id << ": unexpected non-standard exception\n";
        }
        std::cout << (passed ? "PASS " : "FAIL ") << test_case.id << '\n';
        if (!passed) {
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

inline int run_cases(std::span<const Case> cases, int argc, const char* const* argv) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    return run_cases(cases, arguments);
}
} // namespace inputnext::test
