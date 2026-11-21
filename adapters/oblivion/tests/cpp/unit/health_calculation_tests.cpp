#include "model.hpp"
#include "support/test_runner.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

void test_modified_max_health() {
    using namespace oblivion_adapter;

    // Fortify Health buff
    {
        const float base_av = 100.0F;
        const float max_mod = 50.0F;
        const float max_av = base_av + max_mod;
        const auto health = calculate_health(150.0F, max_av);
        require(health.valid, "Fortified max health must be valid");
        require(std::abs(health.maximum - 150.0F) < 0.001F, "Maximum health should be 150");
        require(std::abs(health.current - 150.0F) < 0.001F, "Current health should be 150");
        require(std::abs(health.ratio - 1.0F) < 0.001F, "Ratio should be 1.0");
    }

    // Drain Health debuff
    {
        const float base_av = 100.0F;
        const float max_mod = -30.0F;
        const float max_av = base_av + max_mod;
        const auto health = calculate_health(35.0F, max_av);
        require(health.valid, "Drained max health must be valid");
        require(std::abs(health.maximum - 70.0F) < 0.001F, "Maximum health should be 70");
        require(std::abs(health.current - 35.0F) < 0.001F, "Current health should be 35");
        require(std::abs(health.ratio - 0.5F) < 0.001F, "Ratio should be 0.5");
    }

    // Extreme drain reducing max health to 0 or below
    {
        const float base_av = 100.0F;
        const float max_mod = -100.0F;
        const float max_av = base_av + max_mod;
        const auto health = calculate_health(0.0F, max_av);
        require(!health.valid, "Zero max health must be invalid");
        require(health.ratio == 0.0F, "Ratio 0");
    }

    {
        const float base_av = 100.0F;
        const float max_mod = -120.0F;
        const float max_av = base_av + max_mod;
        const auto health = calculate_health(0.0F, max_av);
        require(!health.valid, "Negative max health must be invalid");
        require(health.ratio == 0.0F, "Ratio 0");
    }
}

void test_invalid_and_infinite_values() {
    using namespace oblivion_adapter;

    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    {
        const auto health = calculate_health(inf, 100.0F);
        require(!health.valid, "Positive infinite current health must be invalid");
        require(health.ratio == 0.0F, "Ratio must be 0 for invalid health");
    }

    {
        const auto health = calculate_health(-inf, 100.0F);
        require(!health.valid, "Negative infinite current health must be invalid");
        require(health.ratio == 0.0F, "Ratio must be 0 for invalid health");
    }

    {
        const auto health = calculate_health(100.0F, inf);
        require(!health.valid, "Infinite maximum health must be invalid");
        require(health.ratio == 0.0F, "Ratio must be 0 for invalid health");
    }

    {
        const auto health = calculate_health(100.0F, -inf);
        require(!health.valid, "Negative infinite maximum health must be invalid");
        require(health.ratio == 0.0F, "Ratio must be 0 for invalid health");
    }

    {
        const auto health = calculate_health(nan, 100.0F);
        require(!health.valid, "NaN current health must be invalid");
        require(health.ratio == 0.0F, "Ratio must be 0 for invalid health");
    }

    {
        const auto health = calculate_health(100.0F, nan);
        require(!health.valid, "NaN maximum health must be invalid");
        require(health.ratio == 0.0F, "Ratio must be 0 for invalid health");
    }

    {
        const auto health_nan = calculate_health(nan, nan);
        require(!health_nan.valid, "NaN/NaN must be invalid");

        const auto health_inf = calculate_health(inf, inf);
        require(!health_inf.valid, "Inf/Inf must be invalid");
    }
}

void test_boundary_calculations() {
    using namespace oblivion_adapter;

    {
        const auto health = calculate_health(0.001F, 100.0F);
        require(health.valid, "Valid small positive");
        require(health.ratio > 0.0F, "Ratio > 0");
    }

    {
        const auto health = calculate_health(50000.0F, 50000.0F);
        require(health.valid, "Valid large values");
        require(health.ratio == 1.0F, "Full ratio");
    }

    {
        const auto health = calculate_health(-25.0F, 100.0F);
        require(health.valid, "Negative current with positive finite max is valid (zero ratio)");
        require(health.current == 0.0F, "Negative current health clamped to 0");
        require(health.ratio == 0.0F, "Ratio 0");
    }

    {
        const auto health = calculate_health(125.0F, 100.0F);
        require(health.valid, "Current exceeding max is valid");
        require(health.current == 125.0F, "Current health preserved");
        require(health.ratio == 1.0F, "Ratio clamped to 1.0");
    }

    {
        const auto health = calculate_health(33.333F, 100.0F);
        require(std::abs(health.ratio - 0.33333F) < 0.001F, "Ratio precision match");
    }
}

} // namespace

int main() {
    return inputnext::test::run_main("Oblivion health calculation tests", [] -> int {
        test_modified_max_health();
        test_invalid_and_infinite_values();
        test_boundary_calculations();
        std::cout << "All Oblivion health calculation tests passed.\n";
        return 0;
    });
}
