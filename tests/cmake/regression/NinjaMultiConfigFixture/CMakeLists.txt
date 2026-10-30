cmake_minimum_required(VERSION 4.4.4)
project(InputNextAnalyzeMultiConfigFixture LANGUAGES CXX)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/shared_source.cpp" "int analyze_multi_config_fixture() { return 0; }\n")
add_library(ninja_multi_config_target OBJECT "${CMAKE_CURRENT_BINARY_DIR}/shared_source.cpp")
target_compile_options(ninja_multi_config_target PRIVATE
    "$<$<CONFIG:Debug>:/analyze->"
    "$<$<CONFIG:Release>:/analyze>"
)
