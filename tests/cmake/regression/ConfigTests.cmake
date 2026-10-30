include_guard(GLOBAL)

function(_expect_strict_selection name build_directory executable configuration expected_result expected_path)
    file(TO_CMAKE_PATH "${INPUTNEXTADAPTERS_SOURCE_DIR}" TEST_SOURCE_DIRECTORY)
    file(TO_CMAKE_PATH "${build_directory}" TEST_BUILD_DIRECTORY)
    set(TEST_EXECUTABLE "${executable}")
    set(TEST_CONFIGURATION "${configuration}")
    set(TEST_RESULT_FILE "${sandbox}/${name}.txt")
    file(TO_CMAKE_PATH "${TEST_RESULT_FILE}" TEST_RESULT_FILE)
    set(script_path "${sandbox}/${name}.cmake")
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/StrictSelection.cmake.in"
        "${script_path}"
        @ONLY
    )
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -P "${script_path}"
        RESULT_VARIABLE result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(expected_result STREQUAL "success")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Strict configuration regression '${name}' unexpectedly failed with ${result}.")
        endif()
        file(READ "${TEST_RESULT_FILE}" selected_path)
        if(NOT selected_path STREQUAL expected_path)
            message(FATAL_ERROR "Strict configuration regression '${name}' selected '${selected_path}', expected '${expected_path}'.")
        endif()
    elseif(result EQUAL 0)
        message(FATAL_ERROR "Strict configuration regression '${name}' unexpectedly succeeded.")
    endif()
endfunction()

file(TO_CMAKE_PATH "${sandbox}" sandbox)
message(STATUS "[RegressionTest] Checking strict executable configuration selection...")
set(multi_directory "${sandbox}/strict-multiconfig")
file(MAKE_DIRECTORY "${multi_directory}/Debug")
file(WRITE "${multi_directory}/Debug/probe.exe" "debug")
_expect_strict_selection("strict-missing-release" "${multi_directory}" "probe" "Release" failure "")

file(MAKE_DIRECTORY "${multi_directory}/Release")
file(WRITE "${multi_directory}/Release/probe.exe" "release")
_expect_strict_selection(
    "strict-release-selected" "${multi_directory}" "probe" "Release" success
    "${multi_directory}/Release/probe.exe"
)

set(single_directory "${sandbox}/strict-singleconfig")
file(MAKE_DIRECTORY "${single_directory}")
file(WRITE "${single_directory}/probe.exe" "single")
file(WRITE "${single_directory}/CMakeCache.txt" "CMAKE_BUILD_TYPE:STRING=Debug\n")
_expect_strict_selection("strict-single-mismatch" "${single_directory}" "probe" "Release" failure "")
