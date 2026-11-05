add_executable(skyrim_native_schedule_component_tests
    schedule_tests.cpp
    owner_iteration_tests.cpp
    "${InputNextSkyrimCore_SOURCE_DIR}/tests/session/support/session_test_support.cpp"
)
target_include_directories(skyrim_native_schedule_component_tests PRIVATE
    "${InputNextAdapters_SOURCE_DIR}/tests"
    "${InputNextSkyrimCore_SOURCE_DIR}/tests"
    "${InputNextSkyrimCore_SOURCE_DIR}/src"
)
target_link_libraries(skyrim_native_schedule_component_tests PRIVATE
    Catch2::Catch2WithMain
    skyrim_session
)
skyrim_enable_checks(skyrim_native_schedule_component_tests)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(skyrim_native_schedule_component_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 10
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
