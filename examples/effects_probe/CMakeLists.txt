cmake_minimum_required(VERSION 4.4.4)

project(InputNextAdaptersEffectsProbe LANGUAGES CXX)

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

add_executable(effects_probe effects_probe.cpp)
target_compile_features(effects_probe PRIVATE cxx_std_23)
set_target_properties(effects_probe PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(effects_probe PRIVATE InputNextAdapters::runtime_client)

if(COMMAND inputnext_apply_consumer_checks)
    inputnext_apply_consumer_checks(effects_probe)
elseif(MSVC)
    target_compile_options(effects_probe PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
endif()
