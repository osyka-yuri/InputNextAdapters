set(_patch_fixture_root "${sandbox}/commonlibf4-patch-guard")
set(_header "${_patch_fixture_root}/include/RE/B/BSAnimationGraphManager.h")
file(MAKE_DIRECTORY "${_patch_fixture_root}/include/RE/B")
file(WRITE "${_header}" "unrecognized dependency source\n")
file(READ "${_header}" _original_contents)
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUTNEXTADAPTERS_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DINPUTNEXTADAPTERS_FIXTURE_ROOT=${_patch_fixture_root}"
        -P "${CMAKE_CURRENT_LIST_DIR}/CommonLibF4PatchGuardFixture.cmake"
    RESULT_VARIABLE _patch_result
    OUTPUT_VARIABLE _patch_output
    ERROR_VARIABLE _patch_error
)
if(_patch_result EQUAL 0)
    message(FATAL_ERROR "The CommonLibF4 amendment accepted an unrecognized source header.")
endif()
file(READ "${_header}" _after_contents)
if(NOT _after_contents STREQUAL _original_contents)
    message(FATAL_ERROR "The CommonLibF4 amendment modified an unrecognized source header.")
endif()
