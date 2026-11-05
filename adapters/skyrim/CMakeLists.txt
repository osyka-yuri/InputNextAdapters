cmake_minimum_required(VERSION 4.4.4)
project(InputNextSkyrimCore LANGUAGES CXX)
option(INPUTNEXT_SKYRIM_BUILD_TESTS "Build isolated Skyrim tests" "${PROJECT_IS_TOP_LEVEL}")
option(INPUTNEXT_SKYRIM_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(INPUTNEXT_SKYRIM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(INPUTNEXT_SKYRIM_ENABLE_MSVC_ANALYZE "Enable MSVC /analyze" OFF)
option(INPUTNEXT_SKYRIM_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
set(INPUTNEXTADAPTERS_ENABLE_ASAN "${INPUTNEXT_SKYRIM_ENABLE_ASAN}")
set(INPUTNEXTADAPTERS_ENABLE_UBSAN "${INPUTNEXT_SKYRIM_ENABLE_UBSAN}")
set(INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE "${INPUTNEXT_SKYRIM_ENABLE_MSVC_ANALYZE}")
set(INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY "${INPUTNEXT_SKYRIM_ENABLE_CLANG_TIDY}")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProductionOptions.cmake")
inputnext_prepare_production_configuration()
if(NOT TARGET InputNextAdapters::effects)
 add_subdirectory(../.. inputnext_adapters EXCLUDE_FROM_ALL)
endif()
if(NOT COMMAND skyrim_enable_checks)
 function(skyrim_enable_checks target)
  inputnext_configure_target(${target} ${ARGN})
 endfunction()
endif()
add_library(skyrim_policy STATIC
        src/effect_tuning.cpp
        src/effect_recipes.cpp
        src/body_waveform.cpp
        src/world_effects.cpp
        src/content_effects.cpp
        src/policy.cpp
        src/audio_haptics.cpp
        src/body_cue_history.cpp
        src/critical_health.cpp
        src/effect_mixer.cpp
)
target_include_directories(skyrim_policy PUBLIC include)
target_link_libraries(skyrim_policy PUBLIC InputNextAdapters::effects)
skyrim_enable_checks(skyrim_policy PRODUCTION)
add_library(skyrim_session STATIC src/coordinator.cpp src/coordinator/tick.cpp src/coordinator/observer.cpp)
target_link_libraries(skyrim_session PUBLIC skyrim_policy InputNextAdapters::effects)
skyrim_enable_checks(skyrim_session PRODUCTION)
if(INPUTNEXT_SKYRIM_BUILD_TESTS)
 set(CMAKE_TEST_BUILD_DEPENDS ON)
 enable_testing()
 include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/InputNextCatch2.cmake")
 add_subdirectory(tests/cpp/component/effect_settings)
 add_subdirectory(tests/cpp/unit/body_waveform)
 add_subdirectory(tests/cpp/component/body_waveform_equivalence)
 add_subdirectory(tests/cpp/unit/content_effects)
 add_subdirectory(tests/cpp/component/world_effects)
 add_subdirectory(tests/cpp/component/policy)
 add_subdirectory(tests/cpp/component/native_schedule)
 add_subdirectory(tests/cpp/component/session)
 add_subdirectory(tests/cpp/integration/runtime_package)
endif()
