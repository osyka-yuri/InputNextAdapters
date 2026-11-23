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

set(INPUTNEXTADAPTERS_PACKAGE_PLUGIN "" CACHE FILEPATH "Extracted plugin DLL to probe")
set(_oblivion_package_plugin "$<TARGET_FILE:InputNextOblivion>")
if(INPUTNEXTADAPTERS_PACKAGE_PLUGIN)
    set(_oblivion_package_plugin "${INPUTNEXTADAPTERS_PACKAGE_PLUGIN}")
endif()
add_test(NAME oblivion_plugin_package COMMAND oblivion_query_tests "${_oblivion_package_plugin}")
set_tests_properties(oblivion_plugin_package PROPERTIES TIMEOUT 30 LABELS "integration;oblivion;package")
