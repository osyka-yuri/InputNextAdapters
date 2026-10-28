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

add_library(inputnext_adapters_runtime_client STATIC
    src/runtime_client/connection.cpp
    src/runtime_client/effects.cpp
    src/runtime_client/request.cpp
)
set_target_properties(inputnext_adapters_runtime_client PROPERTIES
    EXPORT_NAME runtime_client
)
add_library(InputNextAdapters::runtime_client ALIAS inputnext_adapters_runtime_client)

target_include_directories(inputnext_adapters_runtime_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/inputnext/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/inputnext_adapters/sdk>
)

inputnext_configure_target(inputnext_adapters_runtime_client PRODUCTION)
set_target_properties(inputnext_adapters_runtime_client PROPERTIES
    COMPILE_PDB_NAME "InputNextAdapters-runtime-client"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/$<CONFIG>"
)

add_library(inputnext_adapters_effects STATIC
    src/body_program.cpp
    src/body_pcm.cpp
    src/audio_haptic_profile.cpp
    src/audio_haptics.cpp
    src/led_policy.cpp
)
set_target_properties(inputnext_adapters_effects PROPERTIES EXPORT_NAME effects)
add_library(InputNextAdapters::effects ALIAS inputnext_adapters_effects)
target_include_directories(inputnext_adapters_effects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(inputnext_adapters_effects PUBLIC InputNextAdapters::runtime_client)
inputnext_configure_target(inputnext_adapters_effects PRODUCTION)
set_target_properties(inputnext_adapters_effects PROPERTIES
    COMPILE_PDB_NAME "InputNextAdapters-effects"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/$<CONFIG>"
)

if(INPUTNEXTADAPTERS_BUILD_TESTS)
    if(NOT CMAKE_C_COMPILER AND "$ENV{CC}" STREQUAL "")
        set(CMAKE_C_COMPILER "${CMAKE_CXX_COMPILER}")
    endif()
    enable_language(C)
    set(CMAKE_TEST_BUILD_DEPENDS ON)
    enable_testing()
    add_subdirectory(tests)
endif()
