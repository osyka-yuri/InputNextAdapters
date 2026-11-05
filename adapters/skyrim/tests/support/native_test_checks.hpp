#pragma once
#include <iostream>
#include <string_view>
#include <utility>

namespace skyrim_adapter::tests {
inline int& failure_count() {
    static int failures{};
    return failures;
}

inline void check(bool passed, std::string_view label) {
    if (!passed) {
        ++failure_count();
        std::cerr << "FAIL: " << label << '\n';
    }
}

template <typename Function>
bool run_case(Function&& function) {
    const int failures_before = failure_count();
    std::forward<Function>(function)();
    return failure_count() == failures_before;
}
} // namespace skyrim_adapter::tests
