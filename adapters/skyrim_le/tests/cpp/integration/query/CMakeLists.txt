add_executable(skyrim_le_query_tests query_tests.cpp)
target_include_directories(skyrim_le_query_tests PRIVATE
    "${InputNextSkyrimLE_SOURCE_DIR}/src"
    "${InputNextAdapters_SOURCE_DIR}/tests"
)

target_link_libraries(skyrim_le_query_tests PRIVATE skyrim_le_sdk)
skyrim_enable_checks(skyrim_le_query_tests)
add_dependencies(skyrim_le_query_tests InputNextSkyrimLE)
add_test(NAME skyrim_le_query COMMAND skyrim_le_query_tests "${_skyrim_le_package_probe_plugin}")
set_tests_properties(skyrim_le_query PROPERTIES
    TIMEOUT 30
    LABELS "integration;skyrim_le;native;query"
)
