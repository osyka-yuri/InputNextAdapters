add_executable(skyrim_world_effects_component_tests
    world_effects_tests.cpp
)
target_include_directories(skyrim_world_effects_component_tests PRIVATE
    "${InputNextSkyrimCore_SOURCE_DIR}/tests"
    "${InputNextSkyrimCore_SOURCE_DIR}/src"
)
target_link_libraries(skyrim_world_effects_component_tests PRIVATE
    Catch2::Catch2WithMain
    skyrim_policy
)
skyrim_enable_checks(skyrim_world_effects_component_tests)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(skyrim_world_effects_component_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 10
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
