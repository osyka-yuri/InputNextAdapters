add_executable(inputnext_runtime_package_tests runtime_package_tests.cpp)
target_link_libraries(inputnext_runtime_package_tests PRIVATE InputNextAdapters::runtime_client)

inputnext_configure_target(inputnext_runtime_package_tests)

set(INPUTNEXTADAPTERS_RUNTIME_DIRECTORY "" CACHE PATH
    "Optional architecture-matched InputNext distribution bin directory for read-only integration")
if(INPUTNEXTADAPTERS_RUNTIME_DIRECTORY)
    if(NOT IS_ABSOLUTE "${INPUTNEXTADAPTERS_RUNTIME_DIRECTORY}" OR
       NOT EXISTS "${INPUTNEXTADAPTERS_RUNTIME_DIRECTORY}/inputnext_runtime.dll")
        message(FATAL_ERROR "INPUTNEXTADAPTERS_RUNTIME_DIRECTORY must identify an extracted InputNext bin directory.")
    endif()
    add_test(NAME runtime_client.production_package
        COMMAND inputnext_runtime_package_tests "${INPUTNEXTADAPTERS_RUNTIME_DIRECTORY}")
    set_tests_properties(runtime_client.production_package PROPERTIES
        LABELS "integration;runtime_client;package" TIMEOUT 30)
endif()
