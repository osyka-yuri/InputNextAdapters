add_executable(skyrim_policy_component_tests
    body_footsteps_tests.cpp
    body_identity_tests.cpp
    body_positions_tests.cpp
    body_voice_tests.cpp
    bow_tests.cpp
    combat_tests.cpp
    components_tests.cpp
    events_lifecycle_tests.cpp
    led_tests.cpp
    magic_tests.cpp
    world_tests.cpp
    "${InputNextSkyrimCore_SOURCE_DIR}/tests/policy/support/policy_test_support.cpp"
)
target_include_directories(skyrim_policy_component_tests PRIVATE
    "${InputNextSkyrimCore_SOURCE_DIR}/tests"
    "${InputNextSkyrimCore_SOURCE_DIR}/tests/policy"
    "${InputNextSkyrimCore_SOURCE_DIR}/tests/policy/support"
)
target_link_libraries(skyrim_policy_component_tests PRIVATE
    Catch2::Catch2WithMain
    skyrim_policy
)
skyrim_enable_checks(skyrim_policy_component_tests)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(skyrim_policy_component_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 10
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
