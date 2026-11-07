cmake_minimum_required(VERSION 4.4.4)
project(InputNextSkyrimLE VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

option(INPUTNEXT_SKYRIM_LE_BUILD_TESTS "Build isolated Skyrim LE adapter tests" "${PROJECT_IS_TOP_LEVEL}")
option(INPUTNEXT_SKYRIM_LE_BUILD_PLUGIN "Build the native Skyrim LE SKSE plugin" OFF)
option(INPUTNEXT_SKYRIM_LE_ENABLE_ASAN "Enable AddressSanitizer for owned targets" OFF)
option(INPUTNEXT_SKYRIM_LE_ENABLE_MSVC_ANALYZE "Enable MSVC /analyze for owned targets" OFF)
option(INPUTNEXT_SKYRIM_LE_ENABLE_CLANG_TIDY "Run clang-tidy for owned targets" OFF)

set(INPUTNEXTADAPTERS_ENABLE_ASAN "${INPUTNEXT_SKYRIM_LE_ENABLE_ASAN}")
set(INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE "${INPUTNEXT_SKYRIM_LE_ENABLE_MSVC_ANALYZE}")
set(INPUTNEXT_SKYRIM_ENABLE_CLANG_TIDY "${INPUTNEXT_SKYRIM_LE_ENABLE_CLANG_TIDY}")
set(INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY "${INPUTNEXT_SKYRIM_LE_ENABLE_CLANG_TIDY}")
set(INPUTNEXT_SKYRIM_ENABLE_ASAN "${INPUTNEXT_SKYRIM_LE_ENABLE_ASAN}")
set(INPUTNEXT_SKYRIM_ENABLE_MSVC_ANALYZE "${INPUTNEXT_SKYRIM_LE_ENABLE_MSVC_ANALYZE}")
set(INPUTNEXT_SKYRIM_BUILD_TESTS "${INPUTNEXT_SKYRIM_LE_BUILD_TESTS}")
if(INPUTNEXT_SKYRIM_LE_BUILD_PLUGIN AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
        AND NOT INPUTNEXT_SKYRIM_LE_ENABLE_ASAN
        AND NOT INPUTNEXT_SKYRIM_LE_ENABLE_MSVC_ANALYZE
        AND NOT DEFINED CACHE{INPUTNEXTADAPTERS_PRODUCTION})
    set(INPUTNEXTADAPTERS_PRODUCTION ON CACHE BOOL
        "Use the optimized Release policy for owned product targets")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProductionOptions.cmake")
inputnext_prepare_production_configuration()
if(INPUTNEXT_SKYRIM_LE_BUILD_TESTS)
    set(CMAKE_TEST_BUILD_DEPENDS ON)
endif()
if(NOT TARGET InputNextAdapters::effects)
    add_subdirectory(../.. inputnext_adapters EXCLUDE_FROM_ALL)
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The Skyrim LE adapter requires an x86 toolchain.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "The Skyrim LE adapter requires an MSVC-compatible toolchain.")
endif()

if(INPUTNEXT_SKYRIM_LE_BUILD_TESTS)
    enable_testing()
endif()
add_subdirectory(../skyrim skyrim_shared)
target_compile_definitions(skyrim_config PRIVATE INPUTNEXT_SKYRIM_LE_CONFIG=1)

if(INPUTNEXT_SKYRIM_LE_BUILD_TESTS)
    target_compile_definitions(skyrim_config_unit_tests PRIVATE INPUTNEXT_SKYRIM_LE_CONFIG=1)
    add_subdirectory(tests/cpp/unit/diagnostics)
    inputnext_register_case_tests(
        TARGET skyrim_native_config_file_tests
        PREFIX skyrim_config_files
        MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/../skyrim/tests/config_file/config_file_cases.def"
        ARGS "${CMAKE_CURRENT_SOURCE_DIR}/InputNextSkyrimLE.ini"
             "${CMAKE_CURRENT_SOURCE_DIR}/InputNextSkyrimLE.reference.ini"
        LABELS integration skyrim_le config
    )
    target_compile_definitions(skyrim_native_config_file_tests PRIVATE INPUTNEXT_SKYRIM_LE_CONFIG=1)
endif()

if(INPUTNEXT_SKYRIM_LE_BUILD_PLUGIN)
    include(cmake/NativePlugin.cmake)
endif()
