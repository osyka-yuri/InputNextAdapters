add_executable(inputnext_adapters_output_session_package_tests
    output_session_package_tests.cpp
)

target_include_directories(inputnext_adapters_output_session_package_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/tests"
)
target_link_libraries(inputnext_adapters_output_session_package_tests PRIVATE
    Catch2::Catch2WithMain
    InputNextAdapters::effects
)
inputnext_configure_target(inputnext_adapters_output_session_package_tests)

if(INPUTNEXTADAPTERS_RUNTIME_DIRECTORY)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
    catch_discover_tests(inputnext_adapters_output_session_package_tests
        ADD_TAGS_AS_LABELS
        PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${INPUTNEXTADAPTERS_RUNTIME_DIRECTORY}"
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
        OUTPUT_SUFFIX .txt
    )
endif()
