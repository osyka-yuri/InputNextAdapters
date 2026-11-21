add_executable(oblivion_query_tests query_tests.cpp)
target_include_directories(oblivion_query_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${InputNextAdapters_SOURCE_DIR}/tests"
)

target_link_libraries(oblivion_query_tests PRIVATE oblivion_sdk)
oblivion_enable_checks(oblivion_query_tests)

add_dependencies(oblivion_query_tests InputNextOblivion)
add_test(NAME oblivion_query COMMAND oblivion_query_tests "$<TARGET_FILE:InputNextOblivion>")
set_tests_properties(oblivion_query PROPERTIES
    TIMEOUT 30
    LABELS "integration;oblivion;native;query"
)
