#include "native_observation_test_support.hpp"

#include <iostream>
#include <stdexcept>

namespace skyrim_adapter::native_observation_tests {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        throw std::runtime_error(message);
    }
}

} // namespace skyrim_adapter::native_observation_tests
