add_executable(inputnext_adapters_output_session_tests
    leds_tests.cpp
    triggers_tests.cpp
    trigger_batching_tests.cpp
    pcm_readiness_tests.cpp
    rebind_tests.cpp
    body_delivery_tests.cpp
    session_isolation_tests.cpp
    recovery_tests.cpp
    restriction_tests.cpp
    status_recovery_tests.cpp
    context_contract_tests.cpp
    context_cleanup_tests.cpp
    context_cleanup_scheduling_tests.cpp
)

target_include_directories(inputnext_adapters_output_session_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(inputnext_adapters_output_session_tests PRIVATE Catch2::Catch2WithMain InputNextAdapters::effects)
inputnext_configure_target(inputnext_adapters_output_session_tests)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(inputnext_adapters_output_session_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 20
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
