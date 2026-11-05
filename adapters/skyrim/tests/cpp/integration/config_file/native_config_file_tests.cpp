#include "case_runner.hpp"
// These headers are used by the expressions in config_file_cases.def.
#include "config_file_cases.hpp"          // IWYU pragma: keep
#include "support/native_test_checks.hpp" // IWYU pragma: keep
#include "support/test_runner.hpp"
#include <array>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    return inputnext::test::run_main(__FILE__, [&] {
        if (argc < 3) {
            std::cerr << "Usage: <working.ini> <reference.ini> [--list | --case <id>]\n";
            return 2;
        }
        const std::filesystem::path working_path{argv[1]};
        const std::filesystem::path reference_path{argv[2]};
        const std::array cases{
#define INPUTNEXT_TEST_CASE(case_id, expression)                                                   \
    inputnext::test::Case{#case_id, [&] { return (expression); }},
#include "config_file/config_file_cases.def"
#undef INPUTNEXT_TEST_CASE
        };
        return inputnext::test::run_cases(cases, argc - 2, argv + 2);
    });
}
