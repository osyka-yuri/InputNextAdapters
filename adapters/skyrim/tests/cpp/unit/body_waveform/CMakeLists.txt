if(NOT TARGET skyrim_body_waveform_tests)
    add_executable(skyrim_body_waveform_tests
        body_waveform_tests.cpp
        body_waveform_test_support.cpp
        cases/pcm_core_cases.cpp
        cases/channel_mixing_cases.cpp
        cases/gameplay_waveforms_cases.cpp
        cases/world_content_cases.cpp
    )
    target_include_directories(skyrim_body_waveform_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${InputNextSkyrimCore_SOURCE_DIR}/tests"
        "${InputNextAdapters_SOURCE_DIR}/tests"
    )
    target_link_libraries(skyrim_body_waveform_tests PRIVATE skyrim_policy)
    skyrim_enable_checks(skyrim_body_waveform_tests)
    add_test(NAME skyrim_body_waveform COMMAND skyrim_body_waveform_tests)
    set_tests_properties(skyrim_body_waveform PROPERTIES
        LABELS "unit;skyrim;waveform"
    )
endif()
