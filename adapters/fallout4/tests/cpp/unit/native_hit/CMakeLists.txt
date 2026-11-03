add_executable(fallout4_native_hit_unit_tests
    native_hit_tests.cpp
)

target_include_directories(fallout4_native_hit_unit_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${InputNextFallout4_SOURCE_DIR}/src"
)

target_link_libraries(fallout4_native_hit_unit_tests PRIVATE
    Catch2::Catch2WithMain
    fallout4_policy
    Fallout4::CommonLibF4
)

fallout4_enable_checks(fallout4_native_hit_unit_tests)
target_precompile_headers(fallout4_native_hit_unit_tests PRIVATE <F4SE/Impl/PCH.h>)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(fallout4_native_hit_unit_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 10
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
