add_executable(inputnext_case_runner_tests case_runner_tests.cpp)

target_include_directories(inputnext_case_runner_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/tests"
)
inputnext_configure_target(inputnext_case_runner_tests)

add_test(NAME inputnext_case_runner COMMAND inputnext_case_runner_tests)
set_tests_properties(inputnext_case_runner PROPERTIES
    LABELS "unit"
)

if(TARGET InputNextAdapters::ClangClAsanRuntime)
    set_tests_properties(inputnext_case_runner PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:InputNextAdapters::ClangClAsanRuntime>"
    )
endif()
