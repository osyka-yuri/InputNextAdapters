add_executable(skyrim_effect_settings_component_tests
    support.cpp
    catalogue_tests.cpp
    calibration_tests.cpp
    audio_haptics_tests.cpp
    policy_tuning_tests.cpp
    waveform_and_validation_tests.cpp
)
target_include_directories(skyrim_effect_settings_component_tests PRIVATE
    "${InputNextSkyrimCore_SOURCE_DIR}/tests"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src"
)
target_link_libraries(skyrim_effect_settings_component_tests PRIVATE Catch2::Catch2WithMain skyrim_policy)
skyrim_enable_checks(skyrim_effect_settings_component_tests)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(skyrim_effect_settings_component_tests ADD_TAGS_AS_LABELS PROPERTIES TIMEOUT 10
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results" OUTPUT_SUFFIX .txt)
