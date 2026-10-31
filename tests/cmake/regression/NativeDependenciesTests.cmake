set(_native_dependencies_test_root "${sandbox}/native-dependency-catalog")
file(MAKE_DIRECTORY "${_native_dependencies_test_root}")
set(_native_dependencies_fixture "${CMAKE_CURRENT_LIST_DIR}/NativeDependenciesFixture.cmake")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DHELPER_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DWORK_ROOT=${_native_dependencies_test_root}/positive"
        -P "${_native_dependencies_fixture}"
    WORKING_DIRECTORY "${_native_dependencies_test_root}"
    RESULT_VARIABLE _catalog_result
    OUTPUT_VARIABLE _catalog_output
    ERROR_VARIABLE _catalog_error
)
if(NOT _catalog_result EQUAL 0 OR
   NOT "${_catalog_output}${_catalog_error}" MATCHES "Native dependency catalog positive checks passed")
    message(FATAL_ERROR
        "The production native dependency catalog failed its positive regression checks:\n${_catalog_output}${_catalog_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DHELPER_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        -S "${CMAKE_CURRENT_LIST_DIR}/NativeDependenciesDeclarationFixture"
        -B "${_native_dependencies_test_root}/declarations"
    WORKING_DIRECTORY "${_native_dependencies_test_root}"
    RESULT_VARIABLE _declaration_result
    OUTPUT_VARIABLE _declaration_output
    ERROR_VARIABLE _declaration_error
)
if(NOT _declaration_result EQUAL 0)
    message(FATAL_ERROR
        "Catalog-backed FetchContent declarations failed to configure:\n${_declaration_output}${_declaration_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DHELPER_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DWORK_ROOT=${_native_dependencies_test_root}/negative"
        -DTEST_WRONG_PIN=ON
        -P "${_native_dependencies_fixture}"
    WORKING_DIRECTORY "${_native_dependencies_test_root}"
    RESULT_VARIABLE _wrong_pin_result
    OUTPUT_VARIABLE _wrong_pin_output
    ERROR_VARIABLE _wrong_pin_error
)
if(_wrong_pin_result EQUAL 0 OR
   NOT "${_wrong_pin_output}${_wrong_pin_error}" MATCHES "Source bundle dependency identity mismatch")
    message(FATAL_ERROR
        "The production native dependency catalog did not reject a wrong source bundle pin:\n${_wrong_pin_output}${_wrong_pin_error}")
endif()

message(STATUS "Native dependency catalog CMake regressions passed.")
