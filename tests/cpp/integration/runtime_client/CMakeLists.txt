add_executable(inputnext_adapters_runtime_client_integration_tests
    main.cpp
    support.cpp
    module_snapshot_tests.cpp
    lifecycle_tests.cpp
    api_forwarding_tests.cpp
    client_move_tests.cpp
    context_output_tests.cpp
)

target_include_directories(inputnext_adapters_runtime_client_integration_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/tests"
    "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(inputnext_adapters_runtime_client_integration_tests PRIVATE
    Catch2::Catch2
    InputNextAdapters::runtime_client
)
inputnext_configure_target(inputnext_adapters_runtime_client_integration_tests)

add_dependencies(inputnext_adapters_runtime_client_integration_tests
    fake_runtime_kind_0_id_1
    fake_runtime_kind_0_id_2
    fake_runtime_kind_1_id_3
    fake_runtime_kind_2_id_4
    fake_runtime_kind_3_id_5
    fake_runtime_kind_4_id_6
    fake_runtime_kind_5_id_7
    fake_runtime_kind_6_id_8
    fake_runtime_kind_7_id_9
    fake_runtime_kind_8_id_10
    fake_runtime_kind_9_id_11
    fake_runtime_kind_10_id_12
    fake_runtime_kind_11_id_13
    fake_runtime_kind_12_id_14
    fake_runtime_kind_13_id_15
    fake_runtime_kind_14_id_16
)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(inputnext_adapters_runtime_client_integration_tests
    EXTRA_ARGS --inputnext-fixtures ${_inputnext_runtime_fixture_arguments}
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 20
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
