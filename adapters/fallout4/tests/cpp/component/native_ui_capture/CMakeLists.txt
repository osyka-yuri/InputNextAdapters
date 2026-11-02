add_executable(fallout4_native_ui_capture_component_tests
    abi_tests.cpp
    transition_tests.cpp
    fence_tests.cpp
    remap_tests.cpp
    ui_capture_test.cpp
    support.hpp
    "${InputNextFallout4_SOURCE_DIR}/src/native/bindings.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/ui_capture.cpp"
)

target_include_directories(fallout4_native_ui_capture_component_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../tests"
)

target_link_libraries(fallout4_native_ui_capture_component_tests PRIVATE
    Catch2::Catch2WithMain
    fallout4_policy
    Fallout4::CommonLibF4
)

fallout4_enable_checks(fallout4_native_ui_capture_component_tests)
target_precompile_headers(fallout4_native_ui_capture_component_tests PRIVATE <F4SE/Impl/PCH.h>)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(fallout4_native_ui_capture_component_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 60
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
