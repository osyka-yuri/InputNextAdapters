cmake_minimum_required(VERSION 4.4.4)
project(InputNextOblivion VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

option(INPUTNEXT_OBLIVION_BUILD_TESTS "Build isolated Oblivion adapter tests" "${PROJECT_IS_TOP_LEVEL}")
option(INPUTNEXT_OBLIVION_ENABLE_TEST_EXPORTS "Expose test-only entry points on the local test DLL" ON)

option(INPUTNEXT_OBLIVION_BUILD_PLUGIN "Build the native Oblivion xOBSE plugin" OFF)
option(INPUTNEXT_OBLIVION_ENABLE_ASAN "Enable AddressSanitizer for owned targets" OFF)
option(INPUTNEXT_OBLIVION_ENABLE_MSVC_ANALYZE "Enable MSVC /analyze for owned targets" OFF)
option(INPUTNEXT_OBLIVION_ENABLE_CLANG_TIDY "Run clang-tidy for owned targets" OFF)

set(INPUTNEXTADAPTERS_ENABLE_ASAN "${INPUTNEXT_OBLIVION_ENABLE_ASAN}")
set(INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE "${INPUTNEXT_OBLIVION_ENABLE_MSVC_ANALYZE}")
set(INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY "${INPUTNEXT_OBLIVION_ENABLE_CLANG_TIDY}")

if(INPUTNEXT_OBLIVION_BUILD_PLUGIN AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
        AND NOT INPUTNEXT_OBLIVION_ENABLE_ASAN
        AND NOT INPUTNEXT_OBLIVION_ENABLE_MSVC_ANALYZE
        AND NOT DEFINED CACHE{INPUTNEXTADAPTERS_PRODUCTION})
    set(INPUTNEXTADAPTERS_PRODUCTION ON CACHE BOOL
        "Use the optimized Release policy for owned product targets")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProductionOptions.cmake")
inputnext_prepare_production_configuration()

if(INPUTNEXT_OBLIVION_BUILD_TESTS)
    set(CMAKE_TEST_BUILD_DEPENDS ON)
endif()

if(INPUTNEXTADAPTERS_RUNTIME_DIRECTORY)
    set(INPUTNEXTADAPTERS_BUILD_TESTS OFF)
    if(NOT TARGET InputNextAdapters::runtime_client)
        add_subdirectory(../.. inputnext_adapters EXCLUDE_FROM_ALL)
    endif()
    enable_testing()
    add_subdirectory("../../tests/cpp/integration/runtime_package" runtime_package)
    return()
endif()

if(NOT TARGET InputNextAdapters::effects)
    add_subdirectory(../.. inputnext_adapters EXCLUDE_FROM_ALL)
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The Oblivion adapter requires an x86 toolchain.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "The Oblivion adapter requires an MSVC-compatible toolchain.")
endif()

function(oblivion_enable_checks target)
    inputnext_configure_target(${target} ${ARGN})
    if(MSVC)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/EHsc>")
    endif()
endfunction()

if(INPUTNEXT_OBLIVION_BUILD_TESTS)
    enable_testing()
endif()

add_library(oblivion_observation STATIC
    src/observation.cpp
)
target_include_directories(oblivion_observation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
oblivion_enable_checks(oblivion_observation PRODUCTION)

add_library(oblivion_effects STATIC
    src/audio_haptics.cpp
    src/body_effects.cpp
    src/config.cpp
    src/effect_policy.cpp
    src/effect_session.cpp
    src/gait_feedback.cpp
)
target_include_directories(oblivion_effects PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(oblivion_effects PUBLIC InputNextAdapters::effects)
oblivion_enable_checks(oblivion_effects PRODUCTION)

if(INPUTNEXT_OBLIVION_BUILD_PLUGIN OR INPUTNEXT_OBLIVION_BUILD_TESTS)
    include(cmake/Dependencies.cmake)
endif()

if(INPUTNEXT_OBLIVION_BUILD_PLUGIN)
    include(cmake/NativePlugin.cmake)
endif()

if(INPUTNEXT_OBLIVION_BUILD_TESTS)
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/InputNextCatch2.cmake")
    add_subdirectory(tests/cpp)
endif()
