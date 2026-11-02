if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR "Native Fallout 4 component tests require MSVC; clang-cl remains available for adapter tests.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
add_subdirectory(tests/cpp/component/native_ui_capture)
