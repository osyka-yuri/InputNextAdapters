#pragma once

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <type_traits>
#include <utility>

namespace inputnext::test {

inline void report_exception(const char* context, const char* message) noexcept {
    (void)std::fprintf(stderr, "%s: unexpected exception: %s\n", context, message);
}

inline void report_unknown_exception(const char* context) noexcept {
    (void)std::fprintf(stderr, "%s: unexpected non-standard exception\n", context);
}

template <typename Function>
[[nodiscard]] auto run_noexcept(const char* context, Function&& function) noexcept
    -> std::invoke_result_t<Function> {
    try {
        return std::forward<Function>(function)();
    } catch (const std::exception& exception) {
        report_exception(context, exception.what());
    } catch (...) {
        report_unknown_exception(context);
    }
    std::exit(EXIT_FAILURE);
}

template <typename Function>
[[nodiscard]] int run_main(const char* context, Function&& function) noexcept {
    try {
        return std::forward<Function>(function)();
    } catch (const std::exception& exception) {
        report_exception(context, exception.what());
    } catch (...) {
        report_unknown_exception(context);
    }
    return EXIT_FAILURE;
}

} // namespace inputnext::test
