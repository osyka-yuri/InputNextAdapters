include_guard(GLOBAL)

file(TO_CMAKE_PATH "${sandbox}" _target_options_sandbox)
set(_target_options_build "${_target_options_sandbox}/target-options")
message(STATUS "[RegressionTest] Checking owned-target compiler and production policy...")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${INPUTNEXTADAPTERS_SOURCE_DIR}/tests/cmake/regression/TargetOptionsFixture"
        -B "${_target_options_build}"
        "-DINPUTNEXTADAPTERS_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
    RESULT_VARIABLE _target_options_result
    OUTPUT_VARIABLE _target_options_stdout
    ERROR_VARIABLE _target_options_stderr
)
if(NOT _target_options_result EQUAL 0)
    message(FATAL_ERROR
        "Owned-target policy regression configure failed:\n${_target_options_stdout}\n${_target_options_stderr}")
endif()
