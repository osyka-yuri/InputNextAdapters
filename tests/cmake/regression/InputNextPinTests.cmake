if(NOT DEFINED INPUTNEXTADAPTERS_SOURCE_DIR OR INPUTNEXTADAPTERS_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "InputNextPinTests.cmake requires INPUTNEXTADAPTERS_SOURCE_DIR.")
endif()
if(NOT DEFINED INPUTNEXTADAPTERS_BINARY_DIR OR INPUTNEXTADAPTERS_BINARY_DIR STREQUAL "")
    message(FATAL_ERROR "InputNextPinTests.cmake requires INPUTNEXTADAPTERS_BINARY_DIR.")
endif()
if(NOT DEFINED sandbox OR NOT IS_DIRECTORY "${sandbox}")
    message(FATAL_ERROR "InputNextPinTests.cmake requires an existing validated sandbox directory.")
endif()

file(TO_CMAKE_PATH "${INPUTNEXTADAPTERS_SOURCE_DIR}" INPUTNEXTADAPTERS_SOURCE_DIR)
file(TO_CMAKE_PATH "${INPUTNEXTADAPTERS_BINARY_DIR}" INPUTNEXTADAPTERS_BINARY_DIR)
file(TO_CMAKE_PATH "${sandbox}" sandbox)
message(STATUS "[RegressionTest] Checking InputNext SDK header integrity...")

set(inputnext_pin_fixture "${sandbox}/inputnext_sdk_pin_fixture")
file(MAKE_DIRECTORY "${inputnext_pin_fixture}/third_party")
file(COPY "${INPUTNEXTADAPTERS_SOURCE_DIR}/third_party/inputnext" DESTINATION "${inputnext_pin_fixture}/third_party")
file(WRITE "${inputnext_pin_fixture}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 4.4.4)\n"
    "project(InputNextSdkPinFixture NONE)\n"
    "include(\"${INPUTNEXTADAPTERS_SOURCE_DIR}/cmake/VerifySdkHeader.cmake\")\n"
    "inputnext_verify_sdk_header()\n"
)

set(inputnext_pin_manifest_path "${inputnext_pin_fixture}/third_party/inputnext/manifest.json")
file(READ "${inputnext_pin_manifest_path}" inputnext_pin_manifest)

function(_inputnext_run_pin_configure case_name expect_success)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${inputnext_pin_fixture}" -B "${sandbox}/inputnext_pin_${case_name}"
        RESULT_VARIABLE configure_result
        OUTPUT_VARIABLE configure_stdout
        ERROR_VARIABLE configure_stderr
    )
    if(expect_success)
        if(NOT configure_result EQUAL 0)
            message(FATAL_ERROR
                "Regression: valid InputNext SDK headers were rejected for '${case_name}'.\n"
                "${configure_stdout}\n${configure_stderr}")
        endif()
    elseif(configure_result EQUAL 0)
        message(FATAL_ERROR "Regression: invalid InputNext SDK headers or manifest were accepted for '${case_name}'.")
    endif()
endfunction()

_inputnext_run_pin_configure(valid_headers TRUE)

string(JSON header_entries GET "${inputnext_pin_manifest}" headers)
file(WRITE "${inputnext_pin_manifest_path}" "{\"headers\":${header_entries}}\n")
_inputnext_run_pin_configure(headers_only TRUE)

file(WRITE "${inputnext_pin_manifest_path}" "{")
_inputnext_run_pin_configure(malformed_json FALSE)

string(JSON informational_metadata SET "${inputnext_pin_manifest}" release "\"example-sdk-release\"")
file(WRITE "${inputnext_pin_manifest_path}" "${informational_metadata}\n")
_inputnext_run_pin_configure(informational_metadata TRUE)

string(JSON alternative_revision SET "${inputnext_pin_manifest}" revision "\"informational-reference\"")
file(WRITE "${inputnext_pin_manifest_path}" "${alternative_revision}\n")
_inputnext_run_pin_configure(alternative_revision TRUE)

string(JSON missing_headers REMOVE "${inputnext_pin_manifest}" headers)
file(WRITE "${inputnext_pin_manifest_path}" "${missing_headers}\n")
_inputnext_run_pin_configure(missing_headers FALSE)

string(JSON core_pin_entry GET "${inputnext_pin_manifest}" headers 0)
string(JSON effects_pin_entry GET "${inputnext_pin_manifest}" headers 1)
set(three_pin_entries "[${core_pin_entry},${effects_pin_entry},${core_pin_entry}]")
string(JSON extra_header SET "${inputnext_pin_manifest}" headers "${three_pin_entries}")
file(WRITE "${inputnext_pin_manifest_path}" "${extra_header}\n")
_inputnext_run_pin_configure(extra_header FALSE)

string(JSON unexpected_header_field SET "${inputnext_pin_manifest}" headers 0 note "\"extra\"")
file(WRITE "${inputnext_pin_manifest_path}" "${unexpected_header_field}\n")
_inputnext_run_pin_configure(unexpected_header_field FALSE)

string(JSON wrong_upstream_path SET "${inputnext_pin_manifest}" headers 0 path "\"sdk/changed_core.h\"")
file(WRITE "${inputnext_pin_manifest_path}" "${wrong_upstream_path}\n")
_inputnext_run_pin_configure(wrong_upstream_path FALSE)

string(JSON wrong_vendored_path SET "${inputnext_pin_manifest}" headers 0 vendored_path "\"third_party/inputnext/include/changed_core.h\"")
file(WRITE "${inputnext_pin_manifest_path}" "${wrong_vendored_path}\n")
_inputnext_run_pin_configure(wrong_vendored_path FALSE)

string(JSON malformed_header_hash SET "${inputnext_pin_manifest}" headers 0 sha256 "\"not-a-sha256\"")
file(WRITE "${inputnext_pin_manifest_path}" "${malformed_header_hash}\n")
_inputnext_run_pin_configure(malformed_header_hash FALSE)

string(JSON wrong_header_hash SET "${inputnext_pin_manifest}" headers 0 sha256 "\"0000000000000000000000000000000000000000000000000000000000000000\"")
file(WRITE "${inputnext_pin_manifest_path}" "${wrong_header_hash}\n")
_inputnext_run_pin_configure(wrong_header_hash FALSE)

file(WRITE "${inputnext_pin_manifest_path}" "${inputnext_pin_manifest}\n")
foreach(sdk IN ITEMS core effects)
    set(header_path "third_party/inputnext/include/input_next_${sdk}.h")
    file(APPEND "${inputnext_pin_fixture}/${header_path}" "\n/* tampered SDK header */\n")
    _inputnext_run_pin_configure(tampered_${sdk}_bytes FALSE)
    file(COPY_FILE "${INPUTNEXTADAPTERS_SOURCE_DIR}/${header_path}" "${inputnext_pin_fixture}/${header_path}")
    file(REMOVE "${inputnext_pin_fixture}/${header_path}")
    _inputnext_run_pin_configure(missing_${sdk}_header FALSE)
    file(COPY_FILE "${INPUTNEXTADAPTERS_SOURCE_DIR}/${header_path}" "${inputnext_pin_fixture}/${header_path}")
endforeach()

message(STATUS "[RegressionTest] InputNext SDK header integrity passed.")
