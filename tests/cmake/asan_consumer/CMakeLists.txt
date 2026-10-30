cmake_minimum_required(VERSION 4.4.4)

project(InputNextAdaptersAsanConsumer LANGUAGES CXX)

foreach(required INPUTNEXTADAPTERS_CONSUMER_MODE INPUTNEXTADAPTERS_SOURCE_DIR INPUTNEXTADAPTERS_CONSUMER_MAIN)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required -D${required}=... argument.")
    endif()
endforeach()

if(INPUTNEXTADAPTERS_CONSUMER_MODE STREQUAL "source")
    set(INPUTNEXTADAPTERS_ENABLE_ASAN ON)
    set(INPUTNEXTADAPTERS_BUILD_TESTS OFF)
    add_subdirectory("${INPUTNEXTADAPTERS_SOURCE_DIR}" inputnext_adapters)
elseif(INPUTNEXTADAPTERS_CONSUMER_MODE STREQUAL "installed")
    find_package(InputNextAdapters CONFIG REQUIRED)
else()
    message(FATAL_ERROR "INPUTNEXTADAPTERS_CONSUMER_MODE must be source or installed.")
endif()

add_executable(asan_external_consumer "${INPUTNEXTADAPTERS_CONSUMER_MAIN}")
target_compile_features(asan_external_consumer PRIVATE cxx_std_23)
set_target_properties(asan_external_consumer PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(asan_external_consumer PRIVATE "${INPUTNEXTADAPTERS_SOURCE_DIR}/tests")
target_link_libraries(asan_external_consumer PRIVATE InputNextAdapters::effects)

set(CMAKE_TEST_BUILD_DEPENDS ON)
enable_testing()
add_test(NAME asan_external_consumer COMMAND asan_external_consumer)
