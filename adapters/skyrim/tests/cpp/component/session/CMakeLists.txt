add_executable(skyrim_session_component_tests
    body_content_fallback_tests.cpp
    body_delivery_recovery_tests.cpp
    body_fallback_pulses_tests.cpp
    body_ownership_tests.cpp
    body_pcm_timeline_tests.cpp
    body_transient_failures_tests.cpp
    coordinator_interleaving_tests.cpp
    coordinator_admission_tests.cpp
    coordinator_cleanup_budget_tests.cpp
    coordinator_cleanup_tests.cpp
    context_lifecycle_tests.cpp
    gates_tests.cpp
    ingress_tests.cpp
    lifecycle_tests.cpp
    output_recovery_tests.cpp
    timing_tests.cpp
    "${InputNextSkyrimCore_SOURCE_DIR}/tests/session/support/session_test_support.cpp"
)
target_include_directories(skyrim_session_component_tests PRIVATE
    "${InputNextAdapters_SOURCE_DIR}/tests"
    "${InputNextSkyrimCore_SOURCE_DIR}/tests"
    "${InputNextSkyrimCore_SOURCE_DIR}/tests/session/support"
)
target_link_libraries(skyrim_session_component_tests PRIVATE
    Catch2::Catch2WithMain
    skyrim_session
)
skyrim_enable_checks(skyrim_session_component_tests)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(skyrim_session_component_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 20
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
