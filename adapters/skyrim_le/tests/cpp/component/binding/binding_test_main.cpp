#include "binding_test_support.hpp"

#include "case_runner.hpp"
#include "support/test_runner.hpp"

#include <array>
#include <iostream>
#include <string_view>
#include <utility>

namespace {
int failures{};

template <typename TestGroup>
bool run_group(TestGroup&& run) {
    const int failures_before = failures;
    std::forward<TestGroup>(run)();
    return failures == failures_before;
}

int run_tests(int argc, const char* const* argv) {
    const std::array<inputnext::test::Case, 3> test_cases{{
        {.id = "translators",
            .run = [] { return run_group(skyrim_le::binding_tests::run_translation_tests); }},
        {.id = "menus", .run = [] { return run_group(skyrim_le::binding_tests::run_menu_tests); }},
        {.id = "abi", .run = [] { return run_group(skyrim_le::binding_tests::run_abi_tests); }},
    }};
    return inputnext::test::run_cases(test_cases, argc, argv);
}
} // namespace

namespace skyrim_le::binding_tests {
void check(bool condition, std::string_view label) {
    if (!condition) {
        std::cerr << "FAIL: " << label << '\n';
        ++failures;
    }
}
} // namespace skyrim_le::binding_tests

int main(int argc, char** argv) {
    return inputnext::test::run_main(__FILE__, [&] { return run_tests(argc, argv); });
}
