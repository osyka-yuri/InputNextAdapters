add_executable(fallout4_runtime_package_tests
    runtime_package_tests.cpp
    "${InputNextFallout4_SOURCE_DIR}/tests/cpp/component/session/support.cpp"
)
target_include_directories(fallout4_runtime_package_tests PRIVATE
    "${InputNextAdapters_SOURCE_DIR}/tests"
    "${InputNextFallout4_SOURCE_DIR}/tests"
)
target_link_libraries(fallout4_runtime_package_tests PRIVATE
    Catch2::Catch2WithMain
    fallout4_session
    InputNextAdapters::runtime_client
)
fallout4_enable_checks(fallout4_runtime_package_tests)
if(INPUTNEXTADAPTERS_RUNTIME_DIRECTORY)
    catch_discover_tests(fallout4_runtime_package_tests
        ADD_TAGS_AS_LABELS
        PROPERTIES TIMEOUT 30 WORKING_DIRECTORY "${INPUTNEXTADAPTERS_RUNTIME_DIRECTORY}"
    )
endif()
