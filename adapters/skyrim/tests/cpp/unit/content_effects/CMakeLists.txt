if(NOT TARGET skyrim_content_effects_tests)
    add_executable(skyrim_content_effects_tests
        content_effects_tests.cpp
    )
    target_include_directories(skyrim_content_effects_tests PRIVATE
        "${InputNextSkyrimCore_SOURCE_DIR}/src"
        "${InputNextAdapters_SOURCE_DIR}/tests"
    )
    target_link_libraries(skyrim_content_effects_tests PRIVATE skyrim_policy)
    skyrim_enable_checks(skyrim_content_effects_tests)
    add_test(NAME skyrim_content_effects COMMAND skyrim_content_effects_tests)
    set_tests_properties(skyrim_content_effects PROPERTIES
        LABELS "unit;skyrim;effects"
    )
endif()
