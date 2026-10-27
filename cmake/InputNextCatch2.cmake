if(TARGET Catch2::Catch2 OR TARGET Catch2::Catch2WithMain)
    if(NOT TARGET Catch2::Catch2 OR NOT TARGET Catch2::Catch2WithMain)
        message(FATAL_ERROR "Incomplete Catch2 dependency: both Catch2::Catch2 and Catch2::Catch2WithMain must be defined.")
    endif()

    if(NOT COMMAND catch_discover_tests)
        if(DEFINED catch2_SOURCE_DIR AND EXISTS "${catch2_SOURCE_DIR}/extras/Catch.cmake")
            include("${catch2_SOURCE_DIR}/extras/Catch.cmake")
        elseif(DEFINED Catch2_DIR AND EXISTS "${Catch2_DIR}/Catch.cmake")
            include("${Catch2_DIR}/Catch.cmake")
        else()
            include(Catch OPTIONAL RESULT_VARIABLE _inputnext_catch_included)
            if(NOT _inputnext_catch_included)
                find_file(_inputnext_catch_cmake Catch.cmake PATH_SUFFIXES Catch2 extras)
                if(_inputnext_catch_cmake)
                    include("${_inputnext_catch_cmake}")
                endif()
            endif()
        endif()
    endif()

    if(NOT COMMAND catch_discover_tests)
        message(FATAL_ERROR "Catch2 targets exist but catch_discover_tests command is not available. Ensure Catch.cmake is provided or discoverable.")
    endif()

    return()
endif()

include(FetchContent)

set(_inputnext_prev_cxx_standard "${CMAKE_CXX_STANDARD}")
set(_inputnext_prev_cxx_required "${CMAKE_CXX_STANDARD_REQUIRED}")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CATCH_BUILD_TESTING OFF CACHE BOOL "Disable Catch2 internal testing")
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "Disable Catch2 documentation")
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "Disable Catch2 extra targets")

FetchContent_Declare(Catch2
    URL https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.16.1
    URL_HASH SHA256=d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d
    SYSTEM
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Catch2)

if(DEFINED _inputnext_prev_cxx_standard AND NOT _inputnext_prev_cxx_standard STREQUAL "")
    set(CMAKE_CXX_STANDARD "${_inputnext_prev_cxx_standard}")
else()
    unset(CMAKE_CXX_STANDARD)
endif()
if(DEFINED _inputnext_prev_cxx_required AND NOT _inputnext_prev_cxx_required STREQUAL "")
    set(CMAKE_CXX_STANDARD_REQUIRED "${_inputnext_prev_cxx_required}")
else()
    unset(CMAKE_CXX_STANDARD_REQUIRED)
endif()

if(TARGET Catch2)
    target_compile_features(Catch2 PUBLIC cxx_std_23)
    inputnext_enable_asan(Catch2)
    inputnext_enable_ubsan(Catch2)
endif()
if(TARGET Catch2WithMain)
    target_compile_features(Catch2WithMain PUBLIC cxx_std_23)
    inputnext_enable_asan(Catch2WithMain)
    inputnext_enable_ubsan(Catch2WithMain)
endif()

if(NOT COMMAND catch_discover_tests)
    if(DEFINED catch2_SOURCE_DIR AND EXISTS "${catch2_SOURCE_DIR}/extras/Catch.cmake")
        include("${catch2_SOURCE_DIR}/extras/Catch.cmake")
    else()
        message(FATAL_ERROR "Catch2 was made available, but Catch.cmake could not be found.")
    endif()
endif()
