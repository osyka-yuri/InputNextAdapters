add_executable(skyrim_le_diagnostics_tests diagnostics_tests.cpp)
target_include_directories(skyrim_le_diagnostics_tests PRIVATE
    "${InputNextSkyrimLE_SOURCE_DIR}/src"
)
skyrim_enable_checks(skyrim_le_diagnostics_tests)
add_test(NAME skyrim_le_diagnostics COMMAND skyrim_le_diagnostics_tests)
set_tests_properties(skyrim_le_diagnostics PROPERTIES
    LABELS "unit;skyrim_le;diagnostics"
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_test(NAME skyrim_le_clang_pe_machine
        COMMAND "${CMAKE_COMMAND}"
            "-Dimage=$<TARGET_FILE:skyrim_le_diagnostics_tests>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/VerifyX86Image.cmake"
    )
    set_tests_properties(skyrim_le_clang_pe_machine PROPERTIES LABELS "integration;skyrim_le;cmake")
endif()
