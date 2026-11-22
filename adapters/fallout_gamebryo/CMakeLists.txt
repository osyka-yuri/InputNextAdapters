cmake_minimum_required(VERSION 4.4.4)
project(InputNextFalloutGamebryo LANGUAGES CXX)

option(INPUTNEXT_FALLOUT_GAMEBRYO_BUILD_TESTS "Build the shared effect tests for Fallout 3 and New Vegas" "${PROJECT_IS_TOP_LEVEL}")

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProductionOptions.cmake")
inputnext_prepare_production_configuration()

if(NOT TARGET InputNextAdapters::effects)
    add_subdirectory(../.. inputnext_adapters EXCLUDE_FROM_ALL)
endif()

add_library(fallout_gamebryo_policy STATIC
    src/body_effects.cpp
    src/body_recipes.cpp
    src/policy.cpp
    src/settings.cpp
    src/trigger_effects.cpp
)
target_include_directories(fallout_gamebryo_policy PUBLIC include)
target_link_libraries(fallout_gamebryo_policy PUBLIC InputNextAdapters::effects)
inputnext_configure_target(fallout_gamebryo_policy PRODUCTION)

if(INPUTNEXT_FALLOUT_GAMEBRYO_BUILD_TESTS)
    enable_testing()
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/InputNextCatch2.cmake")
    add_subdirectory(tests)
endif()
