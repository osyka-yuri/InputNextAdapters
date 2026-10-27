add_executable(inputnext_sdk_layout_c_tests sdk_layout_c.c)
inputnext_configure_c_target(inputnext_sdk_layout_c_tests)
target_compile_definitions(inputnext_sdk_layout_c_tests PRIVATE
    INPUTNEXTADAPTERS_EXPECTED_BITS=${INPUTNEXTADAPTERS_EXPECTED_BITS}
)
target_include_directories(inputnext_sdk_layout_c_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/inputnext/include"
)
add_test(NAME inputnext_sdk_layout_c COMMAND inputnext_sdk_layout_c_tests)
set_tests_properties(inputnext_sdk_layout_c PROPERTIES
    LABELS "unit;contract"
)

add_executable(inputnext_sdk_layout_cpp_tests sdk_layout_cpp.cpp)
inputnext_configure_target(inputnext_sdk_layout_cpp_tests)
target_compile_definitions(inputnext_sdk_layout_cpp_tests PRIVATE
    INPUTNEXTADAPTERS_EXPECTED_BITS=${INPUTNEXTADAPTERS_EXPECTED_BITS}
)
target_include_directories(inputnext_sdk_layout_cpp_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/inputnext/include"
)
add_test(NAME inputnext_sdk_layout_cpp COMMAND inputnext_sdk_layout_cpp_tests)
set_tests_properties(inputnext_sdk_layout_cpp PROPERTIES
    LABELS "unit;contract"
)

if(TARGET InputNextAdapters::ClangClAsanRuntime)
    set_tests_properties(inputnext_sdk_layout_c inputnext_sdk_layout_cpp PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:InputNextAdapters::ClangClAsanRuntime>"
    )
endif()
