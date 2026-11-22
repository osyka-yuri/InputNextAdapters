add_executable(fallout_gamebryo_tests
    audio_profiles_tests.cpp
    combat_feedback_tests.cpp
    gait_policy_tests.cpp
    policy_tests.cpp
    session_tests.cpp
    settings_tests.cpp
    weapon_policy_tests.cpp
)
target_include_directories(fallout_gamebryo_tests PRIVATE "${InputNextAdapters_SOURCE_DIR}/tests")
target_link_libraries(fallout_gamebryo_tests PRIVATE fallout_gamebryo_session Catch2::Catch2WithMain)
inputnext_configure_target(fallout_gamebryo_tests)
catch_discover_tests(fallout_gamebryo_tests ADD_TAGS_AS_LABELS PROPERTIES TIMEOUT 30)
