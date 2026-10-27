cmake_minimum_required(VERSION 4.4.4)

if(INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE AND NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for MSVC analysis verification")
endif()

project(InputNextAdapters VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

include(cmake/CompilerEligibility.cmake)
include(cmake/VerifySdkHeader.cmake)
include(cmake/TargetOptions.cmake)
include(cmake/Packaging.cmake)

inputnext_check_compiler()
inputnext_verify_sdk_header()

option(INPUTNEXTADAPTERS_BUILD_TESTS "Build the adapter foundation tests" "${PROJECT_IS_TOP_LEVEL}")
option(INPUTNEXTADAPTERS_ENABLE_ASAN "Enable AddressSanitizer for project targets" OFF)
option(INPUTNEXTADAPTERS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for project targets" OFF)
option(INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE "Enable the MSVC /analyze pass" OFF)

if(INPUTNEXTADAPTERS_ENABLE_ASAN AND INPUTNEXTADAPTERS_ENABLE_UBSAN)
    message(FATAL_ERROR "AddressSanitizer and UndefinedBehaviorSanitizer are separate profiles; enable only one.")
endif()

if(INPUTNEXTADAPTERS_ENABLE_UBSAN)
    inputnext_configure_ubsan_runtime()
endif()

inputnext_prepare_production_configuration()

if(INPUTNEXTADAPTERS_BUILD_TESTS)
    if(NOT CMAKE_C_COMPILER AND "$ENV{CC}" STREQUAL "")
        set(CMAKE_C_COMPILER "${CMAKE_CXX_COMPILER}")
    endif()
    enable_language(C)
    set(CMAKE_TEST_BUILD_DEPENDS ON)
    enable_testing()
    add_subdirectory(tests)
endif()
