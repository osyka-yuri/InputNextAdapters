add_executable(skyrim_config_unit_tests
    config_tests.cpp
    audio_haptics_tests.cpp
    "${InputNextSkyrimCore_SOURCE_DIR}/tests/config/config_test_support.cpp"
)
target_include_directories(skyrim_config_unit_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../config"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src"
)
target_link_libraries(skyrim_config_unit_tests PRIVATE
    Catch2::Catch2WithMain
    skyrim_config
)
skyrim_enable_checks(skyrim_config_unit_tests)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(skyrim_config_unit_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 10
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
