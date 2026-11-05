if(NOT TARGET skyrim_native_config_file_tests)
    add_executable(skyrim_native_config_file_tests
        native_config_file_tests.cpp
        catalogue_cases.cpp
        diagnostic_cases.cpp
        config_file_support.cpp
        "${InputNextSkyrimCore_SOURCE_DIR}/tests/config/config_test_support.cpp"
    )
    target_include_directories(skyrim_native_config_file_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${InputNextSkyrimCore_SOURCE_DIR}/tests"
        "${InputNextAdapters_SOURCE_DIR}/tests"
    )
    target_link_libraries(skyrim_native_config_file_tests PRIVATE skyrim_config)
    skyrim_enable_checks(skyrim_native_config_file_tests)
endif()
