include_guard(GLOBAL)

set(_tidy_fixture "${sandbox}/tidy-config")
file(MAKE_DIRECTORY "${_tidy_fixture}/valid" "${_tidy_fixture}/invalid")
file(WRITE "${_tidy_fixture}/valid/.clang-tidy" "valid")
file(WRITE "${_tidy_fixture}/invalid/.clang-tidy" "invalid")
file(WRITE "${_tidy_fixture}/validator.cmake" [=[
if(NOT CMAKE_ARGV3 STREQUAL "--verify-config")
    message(FATAL_ERROR "Expected --verify-config")
endif()
file(READ ".clang-tidy" config)
if(config STREQUAL "invalid")
    message(FATAL_ERROR "Unknown check option")
endif()
file(APPEND "verified.txt" "checked\n")
]=])
set(_tidy_source [=[
set(INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY OFF)
include("@INPUTNEXTADAPTERS_SOURCE_DIR@/cmake/ClangTidy.cmake")
set_property(GLOBAL PROPERTY INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE
    "@CMAKE_COMMAND@;-P;@_tidy_fixture@/validator.cmake")
_inputnext_verify_clang_tidy_config("@_tidy_fixture@/valid/.clang-tidy")
_inputnext_verify_clang_tidy_config("@_tidy_fixture@/valid/.clang-tidy")
if(REJECT_INVALID)
    _inputnext_verify_clang_tidy_config("@_tidy_fixture@/invalid/.clang-tidy")
endif()
]=])
string(CONFIGURE "${_tidy_source}" _tidy_source @ONLY)
file(WRITE "${_tidy_fixture}/run.cmake" "${_tidy_source}")
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${_tidy_fixture}/run.cmake"
    RESULT_VARIABLE _tidy_result OUTPUT_VARIABLE _tidy_output ERROR_VARIABLE _tidy_error)
if(NOT _tidy_result EQUAL 0)
    message(FATAL_ERROR "Tidy configuration preflight failed:\n${_tidy_output}${_tidy_error}")
endif()
file(READ "${_tidy_fixture}/valid/verified.txt" _tidy_calls)
if(NOT _tidy_calls STREQUAL "checked\n")
    message(FATAL_ERROR "Tidy configuration was verified more than once: ${_tidy_calls}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -DREJECT_INVALID=ON -P "${_tidy_fixture}/run.cmake"
    RESULT_VARIABLE _tidy_result OUTPUT_VARIABLE _tidy_output ERROR_VARIABLE _tidy_error)
if(_tidy_result EQUAL 0 OR NOT _tidy_error MATCHES "Invalid clang-tidy configuration.*invalid/.clang-tidy"
        OR NOT _tidy_error MATCHES "Unknown check option")
    message(FATAL_ERROR "Tidy preflight did not retain configuration failure details:\n${_tidy_output}${_tidy_error}")
endif()

message(STATUS "[RegressionTest] Checking that owned PCH headers remain linted without linting generated PCH TUs...")
set(_tidy_pch_source "${_tidy_fixture}/pch-source")
set(_tidy_pch_binary "${_tidy_fixture}/pch-build")
file(MAKE_DIRECTORY "${_tidy_pch_source}")
set(_tidy_pch_project [=[
cmake_minimum_required(VERSION 4.4.4)
project(ClangTidyPchFixture LANGUAGES CXX)
include("@INPUTNEXTADAPTERS_SOURCE_DIR@/cmake/ClangTidy.cmake")
if(NOT INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY)
    set(INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY ON)
    set_property(GLOBAL PROPERTY INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE "${CMAKE_COMMAND};-E;echo;lint")
endif()
set_property(GLOBAL PROPERTY INPUTNEXTADAPTERS_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(pch_target STATIC main.cpp)
target_include_directories(pch_target PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_precompile_headers(pch_target PRIVATE pch.hpp)
inputnext_enable_clang_tidy(pch_target)
]=])
string(CONFIGURE "${_tidy_pch_project}" _tidy_pch_project @ONLY)
file(WRITE "${_tidy_pch_source}/CMakeLists.txt" "${_tidy_pch_project}")
file(WRITE "${_tidy_pch_source}/.clang-tidy" [=[
Checks: '-*,modernize-use-nullptr'
WarningsAsErrors: 'modernize-use-nullptr'
HeaderFilterRegex: '.*pch[.]hpp$'
]=])
file(WRITE "${_tidy_pch_source}/main.cpp" "int value() { return header_value() == nullptr ? 1 : 0; }\n")
file(WRITE "${_tidy_pch_source}/pch.hpp" "#pragma once\ninline int* header_value() { return 0; }\n")

foreach(_variable IN ITEMS
    CMAKE_CXX_COMPILER CMAKE_MAKE_PROGRAM CMAKE_GENERATOR CMAKE_CXX_COMPILER_TARGET
    INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE
)
    file(STRINGS "${INPUTNEXTADAPTERS_BINARY_DIR}/CMakeCache.txt" _cache_entry
        REGEX "^${_variable}:[^=]*=")
    if(_cache_entry)
        string(REGEX REPLACE "^[^=]*=" "" _cache_value "${_cache_entry}")
        set("_tidy_${_variable}" "${_cache_value}")
    endif()
endforeach()
if(NOT _tidy_CMAKE_CXX_COMPILER OR NOT _tidy_CMAKE_MAKE_PROGRAM OR
   NOT _tidy_CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR "PCH lint regression requires the active Ninja compiler and make program.")
endif()

set(_tidy_pch_configure_args
    "-DCMAKE_CXX_COMPILER:FILEPATH=${_tidy_CMAKE_CXX_COMPILER}"
    "-DCMAKE_MAKE_PROGRAM:FILEPATH=${_tidy_CMAKE_MAKE_PROGRAM}"
)
if(_tidy_CMAKE_CXX_COMPILER_TARGET)
    list(APPEND _tidy_pch_configure_args
        "-DCMAKE_CXX_COMPILER_TARGET:STRING=${_tidy_CMAKE_CXX_COMPILER_TARGET}")
endif()
if(_tidy_INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY)
    if(NOT _tidy_INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE)
        message(FATAL_ERROR "PCH lint regression requires the configured clang-tidy executable.")
    endif()
    list(APPEND _tidy_pch_configure_args
        "-DINPUTNEXTADAPTERS_ENABLE_CLANG_TIDY=ON"
        "-DINPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE:FILEPATH=${_tidy_INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_tidy_pch_source}" -B "${_tidy_pch_binary}"
        -G Ninja ${_tidy_pch_configure_args}
    RESULT_VARIABLE _tidy_pch_result
    OUTPUT_VARIABLE _tidy_pch_output
    ERROR_VARIABLE _tidy_pch_error
)
if(NOT _tidy_pch_result EQUAL 0)
    message(FATAL_ERROR "PCH lint fixture configure failed:\n${_tidy_pch_output}${_tidy_pch_error}")
endif()

execute_process(
    COMMAND "${_tidy_CMAKE_MAKE_PROGRAM}" -C "${_tidy_pch_binary}" -t commands pch_target
    RESULT_VARIABLE _tidy_pch_commands_result
    OUTPUT_VARIABLE _tidy_pch_commands
    ERROR_VARIABLE _tidy_pch_commands_error
)
string(REGEX MATCHALL "-E __run_co_compile" _tidy_wrappers "${_tidy_pch_commands}")
list(LENGTH _tidy_wrappers _tidy_wrapper_count)
string(REGEX MATCH "-E __run_co_compile[^\r\n]*" _tidy_wrapper_command
    "${_tidy_pch_commands}")
string(FIND "${_tidy_wrapper_command}" "--source=" _tidy_source_start)
string(FIND "${_tidy_wrapper_command}" " -- " _tidy_source_end)
set(_tidy_source_argument "")
if(_tidy_source_start GREATER_EQUAL 0 AND _tidy_source_end GREATER_EQUAL _tidy_source_start)
    math(EXPR _tidy_source_length "${_tidy_source_end} - ${_tidy_source_start}")
    string(SUBSTRING "${_tidy_wrapper_command}" ${_tidy_source_start} ${_tidy_source_length}
        _tidy_source_argument)
endif()
string(FIND "${_tidy_source_argument}" "main.cpp" _tidy_owned_source)
string(FIND "${_tidy_source_argument}" "cmake_pch.cxx" _tidy_generated_pch_source)
string(FIND "${_tidy_wrapper_command}" "--extra-arg-before=/FI" _tidy_forced_include_argument)
string(FIND "${_tidy_wrapper_command}" "pch.hpp" _tidy_forced_include_header)
if(NOT _tidy_pch_commands_result EQUAL 0 OR
   NOT _tidy_wrapper_count EQUAL 1 OR
   _tidy_owned_source LESS 0 OR
   _tidy_generated_pch_source GREATER_EQUAL 0 OR
   _tidy_forced_include_argument LESS 0 OR
   _tidy_forced_include_header LESS 0)
    message(FATAL_ERROR
        "PCH lint scope is incorrect; expected normal source and forced PCH-header lint only:\n"
        "${_tidy_pch_commands}${_tidy_pch_commands_error}")
endif()

if(_tidy_INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${_tidy_pch_binary}" --target pch_target
        RESULT_VARIABLE _tidy_pch_build_result
        OUTPUT_VARIABLE _tidy_pch_build_output
        ERROR_VARIABLE _tidy_pch_build_error
    )
    if(_tidy_pch_build_result EQUAL 0 OR NOT
       "${_tidy_pch_build_output}${_tidy_pch_build_error}" MATCHES
       "pch[.]hpp:[0-9]+:[0-9]+: error: [^\r\n]*\\[modernize-use-nullptr")
        message(FATAL_ERROR
            "Expected clang-tidy to reject the null pointer constant in pch.hpp:\n"
            "${_tidy_pch_build_output}${_tidy_pch_build_error}")
    endif()
endif()
