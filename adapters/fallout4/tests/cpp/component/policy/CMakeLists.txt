add_executable(fallout4_policy_component_tests
    combat_and_vats_tests.cpp
    audio_haptics_tests.cpp
    hit_feedback_tests.cpp
    led_integration_tests.cpp
    landing_tests.cpp
    mechanics_tests.cpp
    support.cpp
    trigger_safety_tests.cpp
    voice_mixer_tests.cpp
    weapon_profiles_tests.cpp
)

target_include_directories(fallout4_policy_component_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${InputNextFallout4_SOURCE_DIR}"
    "${InputNextFallout4_SOURCE_DIR}/src"
)

target_link_libraries(fallout4_policy_component_tests PRIVATE
    Catch2::Catch2WithMain
    fallout4_policy
)

fallout4_enable_checks(fallout4_policy_component_tests)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(fallout4_policy_component_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 10
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
