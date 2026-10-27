#include "case_runner.hpp"
#include "support/test_runner.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
class CleanupProbe {
public:
    explicit CleanupProbe(int& cleanup_count) noexcept : cleanup_count_{cleanup_count} {}

    ~CleanupProbe() { ++cleanup_count_; }

    CleanupProbe(const CleanupProbe&) = delete;
    CleanupProbe& operator=(const CleanupProbe&) = delete;
    CleanupProbe(CleanupProbe&&) = delete;
    CleanupProbe& operator=(CleanupProbe&&) = delete;

private:
    int& cleanup_count_;
};

int run_tests() {
    int failures{};
    const auto check = [&failures](bool passed, const char* message) {
        if (!passed) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    int false_calls{};
    int exception_calls{};
    int passing_calls{};
    int cleanup_count{};
    const std::array cases{
        inputnext::test::Case{
            .id = "boolean_failure",
            .run =
                [&] {
                    ++false_calls;
                    return false;
                },
        },
        inputnext::test::Case{
            .id = "exception_failure",
            .run = [&] -> bool {
                const CleanupProbe cleanup{cleanup_count};
                ++exception_calls;
                throw std::runtime_error{"expected fixture failure"};
            },
        },
        inputnext::test::Case{
            .id = "passing_case",
            .run =
                [&] {
                    ++passing_calls;
                    return true;
                },
        },
    };
    check(inputnext::test::run_cases(cases, {}) == 1,
        "aggregate runner must report boolean and exception failures");
    check(false_calls == 1 && exception_calls == 1 && passing_calls == 1,
        "failures must not skip unrelated cases");
    check(cleanup_count == 1, "exception failure must unwind the case fixture");

    constexpr std::array<std::string_view, 2> selected{"--case", "passing_case"};
    check(inputnext::test::run_cases(cases, selected) == 0,
        "selected passing case must succeed independently");
    check(false_calls == 1 && exception_calls == 1 && passing_calls == 2,
        "selection must execute only the chosen case");
    constexpr std::array<std::string_view, 1> list{"--list"};
    check(inputnext::test::run_cases(cases, list) == 0, "list operation must succeed");
    check(false_calls == 1 && exception_calls == 1 && passing_calls == 2,
        "listing must not execute any case");
    constexpr std::array<std::string_view, 2> unknown{"--case", "unknown_case"};
    check(inputnext::test::run_cases(cases, unknown) != 0,
        "unknown case must fail rather than pass silently");
    constexpr std::array<std::string_view, 1> incomplete{"--case"};
    check(inputnext::test::run_cases(cases, incomplete) != 0, "incomplete selection must fail");
    check(false_calls == 1 && exception_calls == 1 && passing_calls == 2,
        "invalid selection must execute no cases");
    return failures == 0 ? 0 : 1;
}
} // namespace

int main() {
    return inputnext::test::run_main(__FILE__, [] { return run_tests(); });
}
