if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR "Native Fallout 4 component tests require MSVC; clang-cl remains available for adapter tests.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
add_subdirectory(tests/cpp/component/native_ui_capture)
add_subdirectory(tests/cpp/component/native_dispatch)
add_library(fallout4_native_observation STATIC
    src/native/bindings.cpp
    src/native/ui_capture.cpp
    src/native/led_bindings.cpp
    src/native/radiation_source.cpp
    src/native/observation.cpp
)
target_include_directories(fallout4_native_observation PRIVATE src)
target_link_libraries(fallout4_native_observation PRIVATE
    fallout4_config
    fallout4_session
    Fallout4::CommonLibF4
)
fallout4_enable_checks(fallout4_native_observation PRODUCTION)
target_precompile_headers(fallout4_native_observation PRIVATE <F4SE/Impl/PCH.h>)
add_subdirectory(tests/cpp/component/native_owner)
add_subdirectory(tests/cpp/unit/native_hit)
add_subdirectory(tests/cpp/unit/native_led)
