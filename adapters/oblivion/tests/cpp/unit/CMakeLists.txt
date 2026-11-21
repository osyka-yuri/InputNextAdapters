add_executable(oblivion_model_tests model_tests.cpp)
target_include_directories(oblivion_model_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${InputNextAdapters_SOURCE_DIR}/tests"
)
target_link_libraries(oblivion_model_tests PRIVATE oblivion_observation)
oblivion_enable_checks(oblivion_model_tests)

add_test(NAME oblivion_model COMMAND oblivion_model_tests)
set_tests_properties(oblivion_model PROPERTIES
    TIMEOUT 30
    LABELS "unit;oblivion"
)

add_executable(oblivion_health_calculation_tests health_calculation_tests.cpp)
target_include_directories(oblivion_health_calculation_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${InputNextAdapters_SOURCE_DIR}/tests"
)
target_link_libraries(oblivion_health_calculation_tests PRIVATE oblivion_observation)
oblivion_enable_checks(oblivion_health_calculation_tests)

add_test(NAME oblivion_health_calculation COMMAND oblivion_health_calculation_tests)
set_tests_properties(oblivion_health_calculation PROPERTIES
    TIMEOUT 30
    LABELS "unit;oblivion"
)

add_executable(oblivion_native_abi_tests native_abi_tests.cpp
    "${PROJECT_SOURCE_DIR}/src/native/magic_forms.cpp"
    "${PROJECT_SOURCE_DIR}/src/native/options_input.cpp"
)
target_include_directories(oblivion_native_abi_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${InputNextAdapters_SOURCE_DIR}/tests"
)
target_link_libraries(oblivion_native_abi_tests PRIVATE oblivion_sdk InputNextAdapters::effects)
oblivion_enable_checks(oblivion_native_abi_tests)

add_test(NAME oblivion_native_abi COMMAND oblivion_native_abi_tests)
set_tests_properties(oblivion_native_abi PROPERTIES
    TIMEOUT 30
    LABELS "unit;oblivion;native;abi"
)

add_executable(oblivion_effect_policy_tests
    effect_policy/action_results_tests.cpp
    effect_policy/actions_tests.cpp
    effect_policy/body_tests.cpp
    effect_policy/bow_tests.cpp
    effect_policy/combat_tests.cpp
    effect_policy/gait_tests.cpp
    effect_policy/magic_tests.cpp
    effect_policy/player_tests.cpp
)
target_link_libraries(oblivion_effect_policy_tests PRIVATE oblivion_effects Catch2::Catch2WithMain)
oblivion_enable_checks(oblivion_effect_policy_tests)
catch_discover_tests(oblivion_effect_policy_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 30
)

foreach(case IN ITEMS effect_session config audio_profiles)
    add_executable(oblivion_${case}_tests ${case}_tests.cpp)
    target_include_directories(oblivion_${case}_tests PRIVATE
        "${InputNextAdapters_SOURCE_DIR}/tests"
    )
    target_link_libraries(oblivion_${case}_tests PRIVATE oblivion_effects)
    if(case STREQUAL "config")
        target_link_libraries(oblivion_${case}_tests PRIVATE Catch2::Catch2WithMain)
    endif()
    oblivion_enable_checks(oblivion_${case}_tests)
    add_test(NAME oblivion_${case} COMMAND oblivion_${case}_tests)
    set_tests_properties(oblivion_${case} PROPERTIES
        TIMEOUT 30
        LABELS "unit;oblivion;effects"
    )
endforeach()

target_link_libraries(oblivion_effect_session_tests PRIVATE oblivion_observation)

set(oblivion_haptics_manifest
    "${InputNextAdapters_SOURCE_DIR}/tools/haptics/manifests/oblivion.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${oblivion_haptics_manifest}")
file(READ "${oblivion_haptics_manifest}" manifest_json)
string(JSON profile_count LENGTH "${manifest_json}" profiles)
math(EXPR last_profile "${profile_count} - 1")
set(manifest_windows "")
foreach(index RANGE 0 "${last_profile}")
    string(JSON section GET "${manifest_json}" profiles "${index}" section)
    string(JSON duration GET "${manifest_json}" profiles "${index}" duration_ms)
    string(APPEND manifest_windows
        "    {.section = \"${section}\", .duration_ms = ${duration}},\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/audio_manifest_windows.inc"
    CONTENT "${manifest_windows}")
target_include_directories(oblivion_audio_profiles_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
