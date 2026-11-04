if(NOT TARGET skyrim_body_waveform_equivalence_tests)
    add_executable(skyrim_body_waveform_equivalence_tests
        body_waveform_equivalence_tests.cpp
    )
    target_include_directories(skyrim_body_waveform_equivalence_tests PRIVATE
        "${InputNextSkyrimCore_SOURCE_DIR}/tests"
        "${InputNextAdapters_SOURCE_DIR}/tests"
    )
    target_link_libraries(skyrim_body_waveform_equivalence_tests PRIVATE skyrim_policy)
    skyrim_enable_checks(skyrim_body_waveform_equivalence_tests)
    add_test(NAME skyrim_body_waveform_equivalence COMMAND skyrim_body_waveform_equivalence_tests)
    set_tests_properties(skyrim_body_waveform_equivalence PROPERTIES
        LABELS "component;skyrim;waveform"
    )
endif()
