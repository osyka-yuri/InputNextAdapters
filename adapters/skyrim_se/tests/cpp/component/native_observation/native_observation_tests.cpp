#include "case_runner.hpp"
#include "native_observation/native_observation_test_cases.hpp" // IWYU pragma: keep
#include "support/test_runner.hpp"

#include <array>

namespace {

using namespace skyrim_adapter::native_observation_tests;

bool run_observation_case(void (*function)()) {
    function();
    return true;
}

int run_tests(int argc, const char* const* argv) {
    const std::array cases{
#define INPUTNEXT_TEST_CASE(case_id, function_name)                                                \
    inputnext::test::Case{.id = #case_id,                                                          \
        .run = [] { return run_observation_case(&(function_name)); }},
#include "native_observation_cases.def"
#undef INPUTNEXT_TEST_CASE
    };
    return inputnext::test::run_cases(cases, argc, argv);
}

} // namespace

int main(int argc, char** argv) {
    return inputnext::test::run_main(__FILE__, [&] { return run_tests(argc, argv); });
}
