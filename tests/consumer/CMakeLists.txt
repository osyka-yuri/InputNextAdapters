cmake_minimum_required(VERSION 4.4.4)

project(InputNextAdaptersCommonEffectsConsumer LANGUAGES CXX)

set(_inputnext_consumer_checks_module)
if(DEFINED INPUTNEXTADAPTERS_HELPERS_SOURCE_DIR AND
   EXISTS "${INPUTNEXTADAPTERS_HELPERS_SOURCE_DIR}/cmake/ConsumerChecks.cmake")
    set(_inputnext_consumer_checks_module "${INPUTNEXTADAPTERS_HELPERS_SOURCE_DIR}/cmake/ConsumerChecks.cmake")
elseif(DEFINED INPUTNEXTADAPTERS_SOURCE_DIR AND
       EXISTS "${INPUTNEXTADAPTERS_SOURCE_DIR}/cmake/ConsumerChecks.cmake")
    set(_inputnext_consumer_checks_module "${INPUTNEXTADAPTERS_SOURCE_DIR}/cmake/ConsumerChecks.cmake")
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../cmake/ConsumerChecks.cmake")
    set(_inputnext_consumer_checks_module "${CMAKE_CURRENT_LIST_DIR}/../../cmake/ConsumerChecks.cmake")
endif()
if(_inputnext_consumer_checks_module)
    include("${_inputnext_consumer_checks_module}")
endif()
if(NOT COMMAND inputnext_apply_consumer_checks)
    foreach(flag INPUTNEXTADAPTERS_ENABLE_ASAN INPUTNEXTADAPTERS_ENABLE_UBSAN
            INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY)
        if(NOT DEFINED ${flag})
            set(${flag} OFF)
        endif()
    endforeach()
    if(INPUTNEXTADAPTERS_ENABLE_ASAN OR INPUTNEXTADAPTERS_ENABLE_UBSAN OR
       INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE OR INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY)
        message(FATAL_ERROR "Enabled compiler checks require the source-relative InputNextAdapters CMake helpers.")
    endif()
endif()

if(DEFINED INPUTNEXTADAPTERS_SOURCE_DIR)
    if(NOT IS_ABSOLUTE "${INPUTNEXTADAPTERS_SOURCE_DIR}" OR
       NOT EXISTS "${INPUTNEXTADAPTERS_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "INPUTNEXTADAPTERS_SOURCE_DIR must name the adapter source tree.")
    endif()
    add_subdirectory("${INPUTNEXTADAPTERS_SOURCE_DIR}" inputnext_adapters)
else()
    find_package(InputNextAdapters CONFIG REQUIRED)
endif()

add_executable(common_effects_consumer main.cpp)
target_compile_features(common_effects_consumer PRIVATE cxx_std_23)
set_target_properties(common_effects_consumer PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(common_effects_consumer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(common_effects_consumer PRIVATE InputNextAdapters::effects)

if(COMMAND inputnext_apply_consumer_checks)
    inputnext_apply_consumer_checks(common_effects_consumer)
elseif(MSVC)
    target_compile_options(common_effects_consumer PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
endif()
