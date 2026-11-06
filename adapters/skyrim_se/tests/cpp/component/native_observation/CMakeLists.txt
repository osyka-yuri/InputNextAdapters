add_executable(skyrim_native_observation_tests
    native_observation_tests.cpp
    native_observation_test_support.cpp
    cases/native_state_cases.cpp
    cases/content_and_survival_cases.cpp
    cases/spell_and_casting_cases.cpp
    cases/event_attribution_cases.cpp
    cases/led_observation_cases.cpp
)
target_include_directories(skyrim_native_observation_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${InputNextSkyrim_SOURCE_DIR}/tests"
    "${InputNextAdapters_SOURCE_DIR}/tests"
)
target_link_libraries(skyrim_native_observation_tests PRIVATE skyrim_native_observation)
target_precompile_headers(skyrim_native_observation_tests PRIVATE <SKSE/Impl/PCH.h>)
skyrim_enable_checks(skyrim_native_observation_tests)

inputnext_register_case_tests(
    TARGET skyrim_native_observation_tests
    PREFIX skyrim_native_observation
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/native_observation_cases.def"
    LABELS component skyrim_se native observation
)
