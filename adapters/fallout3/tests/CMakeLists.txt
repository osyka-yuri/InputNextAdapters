add_executable(fallout3_plugin_tests plugin_tests.cpp)
target_include_directories(fallout3_plugin_tests PRIVATE
    "${INPUTNEXT_FALLOUT3_REPOSITORY_ROOT}/tests"
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(fallout3_plugin_tests PRIVATE fallout3_fose_sdk)
fallout3_enable_checks(fallout3_plugin_tests)
add_dependencies(fallout3_plugin_tests InputNextFallout3)
add_test(NAME fallout3_plugin
    COMMAND fallout3_plugin_tests "$<TARGET_FILE:InputNextFallout3>")
set_tests_properties(fallout3_plugin PROPERTIES
    TIMEOUT 30
    LABELS "integration;fallout3;native;loader"
)

add_test(NAME fallout3_verify_x86_image
    COMMAND "${CMAKE_COMMAND}"
        "-Dimage=$<TARGET_FILE:InputNextFallout3>"
        "-Dproduct_name=Fallout 3 plugin"
        -P "${INPUTNEXT_FALLOUT3_REPOSITORY_ROOT}/cmake/VerifyPe32Image.cmake"
)
set_tests_properties(fallout3_verify_x86_image PROPERTIES
    LABELS "integration;fallout3;native;image"
)

add_test(NAME fallout3_verify_exports
    COMMAND "${CMAKE_COMMAND}"
        "-Dimage=$<TARGET_FILE:InputNextFallout3>"
        "-Ddumpbin=${_fallout3_dumpbin}"
        "-Dexpected_exports=FOSEPlugin_Load,FOSEPlugin_Query"
        "-Dproduct_name=Fallout 3 plugin"
        -P "${INPUTNEXT_FALLOUT3_REPOSITORY_ROOT}/cmake/VerifyWindowsDll.cmake"
)
set_tests_properties(fallout3_verify_exports PROPERTIES
    LABELS "integration;fallout3;native;exports"
)

add_test(NAME fallout3_verify_package
    COMMAND "${CMAKE_COMMAND}"
        "-Dinstall_build_dir=${CMAKE_BINARY_DIR}"
        "-Dpackage_archive=${CMAKE_CURRENT_BINARY_DIR}/package/InputNextFallout3-${PROJECT_VERSION}-windows-x86.zip"
        "-Dimage=$<TARGET_FILE:InputNextFallout3>"
        "-Ddumpbin=${_fallout3_dumpbin}"
        "-Dconfiguration=$<CONFIG>"
        -P "${PROJECT_SOURCE_DIR}/cmake/MakePackage.cmake"
)
set_tests_properties(fallout3_verify_package PROPERTIES
    TIMEOUT 30
    LABELS "integration;fallout3;native;package"
)
