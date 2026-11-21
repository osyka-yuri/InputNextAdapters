add_test(NAME oblivion_verify_x86_image
    COMMAND "${CMAKE_COMMAND}"
        "-Dimage=$<TARGET_FILE:InputNextOblivion>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/VerifyX86Image.cmake"
)
set_tests_properties(oblivion_verify_x86_image PROPERTIES
    LABELS "integration;oblivion;native;image"
)
