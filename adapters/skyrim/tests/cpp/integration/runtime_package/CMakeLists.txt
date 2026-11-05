add_executable(skyrim_runtime_package_tests
    runtime_package_tests.cpp
    "${InputNextSkyrimCore_SOURCE_DIR}/tests/session/support/session_test_support.cpp"
)
target_include_directories(skyrim_runtime_package_tests PRIVATE
    "${InputNextAdapters_SOURCE_DIR}/tests"
    "${InputNextSkyrimCore_SOURCE_DIR}/tests"
)
target_link_libraries(skyrim_runtime_package_tests PRIVATE Catch2::Catch2WithMain skyrim_session)
skyrim_enable_checks(skyrim_runtime_package_tests)

if(INPUTNEXTADAPTERS_RUNTIME_DIRECTORY)
    catch_discover_tests(skyrim_runtime_package_tests
        ADD_TAGS_AS_LABELS
        PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${INPUTNEXTADAPTERS_RUNTIME_DIRECTORY}"
    )
endif()
