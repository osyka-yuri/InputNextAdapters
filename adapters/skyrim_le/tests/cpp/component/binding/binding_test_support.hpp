#pragma once

#include <string_view>

namespace skyrim_le::binding_tests {
void check(bool condition, std::string_view label);

void run_translation_tests();
void run_menu_tests();
void run_abi_tests();
} // namespace skyrim_le::binding_tests
