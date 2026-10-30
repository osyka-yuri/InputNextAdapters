set(_package_probe_root "${sandbox}/package-probe")
file(MAKE_DIRECTORY "${_package_probe_root}/fixture")

file(WRITE "${_package_probe_root}/fixture/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 4.4.4)
project(InputNextAdaptersPackageProbeFixture NONE)
include("${INPUTNEXTADAPTERS_SOURCE_DIR}/cmake/PackageProbe.cmake")
add_library(FakePlugin SHARED IMPORTED GLOBAL)
set_target_properties(FakePlugin PROPERTIES IMPORTED_LOCATION "${INPUTNEXTADAPTERS_FAKE_DLL}")
inputnext_package_probe_argument(_argument FakePlugin)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/probe-argument.txt" CONTENT "${_argument}\n")
]=])
file(WRITE "${_package_probe_root}/InputNextSkyrim.dll" "probe fixture\n")
file(REAL_PATH "${_package_probe_root}/InputNextSkyrim.dll" _package_dll)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUTNEXTADAPTERS_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DINPUTNEXTADAPTERS_FAKE_DLL=${_package_dll}"
        -S "${_package_probe_root}/fixture"
        -B "${_package_probe_root}/default"
    RESULT_VARIABLE _default_result
    OUTPUT_VARIABLE _default_output
    ERROR_VARIABLE _default_error
)
if(NOT _default_result EQUAL 0)
    message(FATAL_ERROR "Default package probe configuration failed:\n${_default_output}${_default_error}")
endif()
file(READ "${_package_probe_root}/default/probe-argument.txt" _default_argument)
string(STRIP "${_default_argument}" _default_argument)
if(NOT _default_argument STREQUAL "${_package_dll}")
    message(FATAL_ERROR "Default package probe did not retain its target-file behavior: '${_default_argument}'.")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUTNEXTADAPTERS_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DINPUTNEXTADAPTERS_FAKE_DLL=${_package_dll}"
        "-DINPUTNEXTADAPTERS_PACKAGE_PLUGIN:FILEPATH=${_package_dll}"
        -S "${_package_probe_root}/fixture"
        -B "${_package_probe_root}/selected"
    RESULT_VARIABLE _selected_result
    OUTPUT_VARIABLE _selected_output
    ERROR_VARIABLE _selected_error
)
if(NOT _selected_result EQUAL 0)
    message(FATAL_ERROR "Selected package probe configuration failed:\n${_selected_output}${_selected_error}")
endif()
file(READ "${_package_probe_root}/selected/probe-argument.txt" _selected_argument)
string(STRIP "${_selected_argument}" _selected_argument)
if(NOT _selected_argument STREQUAL "${_package_dll}")
    message(FATAL_ERROR "Selected package probe did not use the canonical package DLL path.")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUTNEXTADAPTERS_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DINPUTNEXTADAPTERS_FAKE_DLL=${_package_dll}"
        "-DINPUTNEXTADAPTERS_PACKAGE_PLUGIN:FILEPATH=${_package_probe_root}/missing.dll"
        -S "${_package_probe_root}/fixture"
        -B "${_package_probe_root}/missing"
    RESULT_VARIABLE _missing_result
    OUTPUT_VARIABLE _missing_output
    ERROR_VARIABLE _missing_error
)
if(_missing_result EQUAL 0 OR
   NOT "${_missing_output}${_missing_error}" MATCHES "existing DLL file")
    message(FATAL_ERROR "Missing package DLL was not rejected during configuration.")
endif()

file(WRITE "${_package_probe_root}/not-a-plugin.txt" "not a DLL\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUTNEXTADAPTERS_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DINPUTNEXTADAPTERS_FAKE_DLL=${_package_dll}"
        "-DINPUTNEXTADAPTERS_PACKAGE_PLUGIN:FILEPATH=${_package_probe_root}/not-a-plugin.txt"
        -S "${_package_probe_root}/fixture"
        -B "${_package_probe_root}/wrong-extension"
    RESULT_VARIABLE _wrong_extension_result
    OUTPUT_VARIABLE _wrong_extension_output
    ERROR_VARIABLE _wrong_extension_error
)
if(_wrong_extension_result EQUAL 0 OR
   NOT "${_wrong_extension_output}${_wrong_extension_error}" MATCHES "must identify a DLL file")
    message(FATAL_ERROR "Non-DLL package probe path was not rejected during configuration.")
endif()
