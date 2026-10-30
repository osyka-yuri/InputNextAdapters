cmake_minimum_required(VERSION 4.4.4)

project(InputNextAdaptersUbsanConsumer LANGUAGES CXX)

option(INPUTNEXTADAPTERS_ENABLE_UBSAN "Verify UndefinedBehaviorSanitizer consumer propagation" OFF)
if(NOT INPUTNEXTADAPTERS_ENABLE_UBSAN)
    message(FATAL_ERROR "The UBSan consumer fixture requires INPUTNEXTADAPTERS_ENABLE_UBSAN=ON.")
endif()

foreach(required INPUTNEXTADAPTERS_CONSUMER_MODE INPUTNEXTADAPTERS_SOURCE_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required -D${required}=... argument.")
    endif()
endforeach()

if(INPUTNEXTADAPTERS_CONSUMER_MODE STREQUAL "source")
    set(INPUTNEXTADAPTERS_BUILD_TESTS OFF)
    add_subdirectory("${INPUTNEXTADAPTERS_SOURCE_DIR}" inputnext_adapters)
elseif(INPUTNEXTADAPTERS_CONSUMER_MODE STREQUAL "installed")
    find_package(InputNextAdapters CONFIG REQUIRED)
else()
    message(FATAL_ERROR "INPUTNEXTADAPTERS_CONSUMER_MODE must be source or installed.")
endif()

function(add_ubsan_consumer target_name trigger_overflow)
    add_executable(${target_name} main.cpp)
    target_compile_features(${target_name} PRIVATE cxx_std_23)
    set_target_properties(${target_name} PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_definitions(${target_name} PRIVATE INPUTNEXTADAPTERS_UBSAN_TRIGGER_OVERFLOW=${trigger_overflow})
    target_link_libraries(${target_name} PRIVATE InputNextAdapters::effects)
endfunction()

add_ubsan_consumer(ubsan_external_consumer_healthy 0)
add_ubsan_consumer(ubsan_external_consumer_overflow 1)
