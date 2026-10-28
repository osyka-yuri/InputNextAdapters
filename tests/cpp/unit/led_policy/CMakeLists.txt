add_executable(inputnext_adapters_led_policy_tests led_policy_tests.cpp)

target_link_libraries(inputnext_adapters_led_policy_tests PRIVATE Catch2::Catch2WithMain InputNextAdapters::effects)
inputnext_configure_target(inputnext_adapters_led_policy_tests)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(inputnext_adapters_led_policy_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 10
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
