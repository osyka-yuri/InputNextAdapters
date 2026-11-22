cmake_minimum_required(VERSION 4.4.4)
project(InputNextFallout3 VERSION 0.1.0 LANGUAGES CXX)

get_filename_component(INPUTNEXT_FALLOUT3_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

option(INPUTNEXT_FALLOUT3_BUILD_TESTS "Build Fallout 3 x86 adapter tests" OFF)
option(INPUTNEXT_FALLOUT3_BUILD_PLUGIN "Build the native Fallout 3 FOSE plugin" OFF)
option(INPUTNEXT_FALLOUT3_ENABLE_MSVC_ANALYZE "Enable MSVC /analyze for owned targets" OFF)
option(INPUTNEXT_FALLOUT3_ENABLE_CLANG_TIDY "Run clang-tidy for owned targets" OFF)

set(INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE "${INPUTNEXT_FALLOUT3_ENABLE_MSVC_ANALYZE}")
set(INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY "${INPUTNEXT_FALLOUT3_ENABLE_CLANG_TIDY}")

if(INPUTNEXT_FALLOUT3_BUILD_PLUGIN AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
        AND NOT INPUTNEXT_FALLOUT3_ENABLE_MSVC_ANALYZE
        AND NOT DEFINED CACHE{INPUTNEXTADAPTERS_PRODUCTION})
    set(INPUTNEXTADAPTERS_PRODUCTION ON CACHE BOOL
        "Use the optimized Release policy for owned product targets")
endif()

include("${INPUTNEXT_FALLOUT3_REPOSITORY_ROOT}/cmake/CompilerEligibility.cmake")
include("${INPUTNEXT_FALLOUT3_REPOSITORY_ROOT}/cmake/TargetOptions.cmake")
inputnext_check_compiler()
inputnext_prepare_production_configuration()

if(INPUTNEXT_FALLOUT3_BUILD_TESTS)
    set(CMAKE_TEST_BUILD_DEPENDS ON)
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The Fallout 3 adapter requires an x86 toolchain.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "The Fallout 3 adapter requires an MSVC-compatible toolchain.")
endif()

function(fallout3_enable_checks target)
    inputnext_configure_target(${target} ${ARGN})
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/EHsc>")
endfunction()

if(INPUTNEXT_FALLOUT3_BUILD_TESTS)
    enable_testing()
endif()

if(INPUTNEXT_FALLOUT3_BUILD_PLUGIN)
    include(cmake/NativePlugin.cmake)
endif()

if(INPUTNEXT_FALLOUT3_BUILD_TESTS)
    if(NOT INPUTNEXT_FALLOUT3_BUILD_PLUGIN)
        message(FATAL_ERROR "INPUTNEXT_FALLOUT3_BUILD_TESTS requires INPUTNEXT_FALLOUT3_BUILD_PLUGIN=ON.")
    endif()
    add_subdirectory(tests)
endif()
