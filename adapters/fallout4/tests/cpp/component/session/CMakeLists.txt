add_executable(fallout4_session_component_tests
    cleanup_tests.cpp
    delivery_recovery_tests.cpp
    events_timing_tests.cpp
    fake_output_tests.cpp
    lifecycle_tests.cpp
    menu_feedback_tests.cpp
    support.cpp
)

target_include_directories(fallout4_session_component_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${InputNextFallout4_SOURCE_DIR}/include"
    "${InputNextAdapters_SOURCE_DIR}/tests"
)

target_link_libraries(fallout4_session_component_tests PRIVATE
    Catch2::Catch2WithMain
    fallout4_session
)

fallout4_enable_checks(fallout4_session_component_tests)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(fallout4_session_component_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 10
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
