add_executable(inputnext_adapters_runtime_client_unit_tests
    lifecycle_tests.cpp
    request_tests.cpp
)

target_include_directories(inputnext_adapters_runtime_client_unit_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/tests"
    "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(inputnext_adapters_runtime_client_unit_tests PRIVATE
    Catch2::Catch2WithMain
    InputNextAdapters::runtime_client
    InputNextAdapters::effects
)
inputnext_configure_target(inputnext_adapters_runtime_client_unit_tests)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(inputnext_adapters_runtime_client_unit_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 20
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
