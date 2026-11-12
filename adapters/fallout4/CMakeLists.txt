cmake_minimum_required(VERSION 4.4.4)
project(InputNextFallout4 VERSION 0.1.2 LANGUAGES CXX)

include(GNUInstallDirs)

option(INPUTNEXT_FALLOUT4_BUILD_PLUGIN "Build the native F4SE plugin" OFF)
option(INPUTNEXT_FALLOUT4_BUILD_TESTS "Build isolated Fallout 4 policy tests" "${PROJECT_IS_TOP_LEVEL}")
option(INPUTNEXT_FALLOUT4_ENABLE_ASAN "Enable AddressSanitizer for owned targets" OFF)
option(INPUTNEXT_FALLOUT4_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for owned targets" OFF)
option(INPUTNEXT_FALLOUT4_ENABLE_MSVC_ANALYZE "Enable MSVC /analyze for owned targets" OFF)
option(INPUTNEXT_FALLOUT4_ENABLE_CLANG_TIDY "Run clang-tidy for owned targets" OFF)

set(INPUTNEXTADAPTERS_ENABLE_ASAN "${INPUTNEXT_FALLOUT4_ENABLE_ASAN}")
set(INPUTNEXTADAPTERS_ENABLE_UBSAN "${INPUTNEXT_FALLOUT4_ENABLE_UBSAN}")
set(INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE "${INPUTNEXT_FALLOUT4_ENABLE_MSVC_ANALYZE}")
set(INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY "${INPUTNEXT_FALLOUT4_ENABLE_CLANG_TIDY}")

if(INPUTNEXT_FALLOUT4_BUILD_PLUGIN AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
        AND NOT INPUTNEXT_FALLOUT4_ENABLE_ASAN
        AND NOT INPUTNEXT_FALLOUT4_ENABLE_UBSAN
        AND NOT INPUTNEXT_FALLOUT4_ENABLE_MSVC_ANALYZE
        AND NOT DEFINED CACHE{INPUTNEXTADAPTERS_PRODUCTION})
    set(INPUTNEXTADAPTERS_PRODUCTION ON CACHE BOOL
        "Use the optimized Release policy for owned product targets")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProductionOptions.cmake")
inputnext_prepare_production_configuration()

if(INPUTNEXT_FALLOUT4_ENABLE_UBSAN AND INPUTNEXT_FALLOUT4_BUILD_PLUGIN)
    message(FATAL_ERROR "UndefinedBehaviorSanitizer is supported for isolated Fallout 4 adapter tests only.")
endif()

if(NOT TARGET InputNextAdapters::effects)
    add_subdirectory(../.. inputnext_adapters EXCLUDE_FROM_ALL)
endif()

if(NOT WIN32)
    message(FATAL_ERROR "The Fallout 4 adapter requires Windows.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Fallout 4 adapter requires an x64 toolchain.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "Use MSVC or clang-cl with the Windows SDK and MSVC-compatible linker.")
endif()

function(fallout4_enable_checks target)
    inputnext_configure_target(${target} ${ARGN})
endfunction()

add_library(fallout4_policy STATIC
    src/policy.cpp
    src/body_voice_mixer.cpp
    src/landing_detector.cpp
    src/resource_cue_tracker.cpp
    src/audio_haptics.cpp
    src/effect_recipes.cpp
)
target_include_directories(fallout4_policy PUBLIC include)
target_link_libraries(fallout4_policy PUBLIC InputNextAdapters::effects)
fallout4_enable_checks(fallout4_policy PRODUCTION)

add_library(fallout4_config STATIC src/config.cpp)
target_include_directories(fallout4_config PUBLIC include)
target_link_libraries(fallout4_config PUBLIC fallout4_policy)
fallout4_enable_checks(fallout4_config PRODUCTION)

add_library(fallout4_session STATIC src/session.cpp)
target_include_directories(fallout4_session PUBLIC include)
target_link_libraries(fallout4_session PUBLIC fallout4_policy)
fallout4_enable_checks(fallout4_session PRODUCTION)

if(INPUTNEXT_FALLOUT4_BUILD_TESTS)
    set(CMAKE_TEST_BUILD_DEPENDS ON)
    enable_testing()
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/InputNextCatch2.cmake")
    add_subdirectory(tests/cpp/component/policy)
    add_subdirectory(tests/cpp/component/session)
    add_subdirectory(tests/cpp/unit/waveform)
    add_subdirectory(tests/cpp/unit/config)
    add_subdirectory(tests/cpp/integration/config_file)
    add_subdirectory(tests/cpp/integration/runtime_package)
    add_subdirectory(tests/cpp/component/native_observation)
    add_subdirectory(tests/cpp/unit/native_animation)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/RegisterCases.cmake")
endif()

if(INPUTNEXT_FALLOUT4_BUILD_PLUGIN)
    include(cmake/NativePlugin.cmake)
endif()
