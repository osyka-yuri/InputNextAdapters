foreach(required
    INPUTNEXTADAPTERS_SOURCE_DIR
    INPUTNEXTADAPTERS_BINARY_DIR
    INPUTNEXTADAPTERS_BUILD_DIR
    INPUTNEXTADAPTERS_GENERATOR
    INPUTNEXTADAPTERS_CXX_COMPILER
)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required -D${required}=... argument.")
    endif()
endforeach()
if(NOT INPUTNEXTADAPTERS_ENABLE_UBSAN)
    message(FATAL_ERROR "The outer build must have INPUTNEXTADAPTERS_ENABLE_UBSAN=ON.")
endif()

file(STRINGS "${INPUTNEXTADAPTERS_BUILD_DIR}/CMakeCache.txt" ubsan_cache_entry
    REGEX "^INPUTNEXTADAPTERS_ENABLE_UBSAN:BOOL=")
if(NOT ubsan_cache_entry STREQUAL "INPUTNEXTADAPTERS_ENABLE_UBSAN:BOOL=ON")
    message(FATAL_ERROR "The outer build must have INPUTNEXTADAPTERS_ENABLE_UBSAN=ON.")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
cmake_path(ABSOLUTE_PATH INPUTNEXTADAPTERS_BINARY_DIR NORMALIZE OUTPUT_VARIABLE build_root)
set(test_root "${build_root}/ubsan-external-consumer-${run_id}")
cmake_path(IS_PREFIX build_root "${test_root}" NORMALIZE test_root_is_owned)
if(NOT test_root_is_owned)
    message(FATAL_ERROR "The UBSan consumer test output path is outside its owning build directory.")
endif()
file(MAKE_DIRECTORY "${test_root}")

function(run_required description)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 180
    )
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "${description} failed with exit code ${result}.\n${stdout}\n${stderr}")
    endif()
endfunction()

set(package_prefix "${test_root}/installed-prefix")
set(relocated_prefix "${test_root}/relocated-prefix")
set(install_args "${CMAKE_COMMAND}" --install "${INPUTNEXTADAPTERS_BUILD_DIR}" --prefix "${package_prefix}")
if(DEFINED INPUTNEXTADAPTERS_CONFIGURATION AND NOT INPUTNEXTADAPTERS_CONFIGURATION STREQUAL "")
    list(APPEND install_args --config "${INPUTNEXTADAPTERS_CONFIGURATION}")
endif()
run_required("package installation" ${install_args})
if(EXISTS "${relocated_prefix}")
    message(FATAL_ERROR "The isolated relocated package destination already exists: ${relocated_prefix}")
endif()
file(RENAME "${package_prefix}" "${relocated_prefix}" RESULT relocation_result)
if(NOT relocation_result STREQUAL "0")
    message(FATAL_ERROR "Could not relocate the installed package: ${relocation_result}")
endif()

function(verify_ubsan_consumer mode consumer_build)
    set(configure_args
        "${CMAKE_COMMAND}"
        -S "${INPUTNEXTADAPTERS_SOURCE_DIR}/tests/cmake/ubsan_consumer"
        -B "${consumer_build}"
        "-G${INPUTNEXTADAPTERS_GENERATOR}"
        "-DINPUTNEXTADAPTERS_CONSUMER_MODE=${mode}"
        "-DINPUTNEXTADAPTERS_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DINPUTNEXTADAPTERS_ENABLE_UBSAN:BOOL=ON"
        "-DCMAKE_CXX_COMPILER=${INPUTNEXTADAPTERS_CXX_COMPILER}"
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    )
    if(DEFINED INPUTNEXTADAPTERS_GENERATOR_PLATFORM AND NOT INPUTNEXTADAPTERS_GENERATOR_PLATFORM STREQUAL "")
        list(APPEND configure_args -A "${INPUTNEXTADAPTERS_GENERATOR_PLATFORM}")
    endif()
    if(DEFINED INPUTNEXTADAPTERS_GENERATOR_TOOLSET AND NOT INPUTNEXTADAPTERS_GENERATOR_TOOLSET STREQUAL "")
        list(APPEND configure_args -T "${INPUTNEXTADAPTERS_GENERATOR_TOOLSET}")
    endif()
    if(DEFINED INPUTNEXTADAPTERS_CONFIGURATION AND NOT INPUTNEXTADAPTERS_CONFIGURATION STREQUAL "")
        list(APPEND configure_args "-DCMAKE_BUILD_TYPE=${INPUTNEXTADAPTERS_CONFIGURATION}")
    endif()
    if(DEFINED INPUTNEXTADAPTERS_CXX_COMPILER_TARGET AND NOT INPUTNEXTADAPTERS_CXX_COMPILER_TARGET STREQUAL "")
        list(APPEND configure_args "-DCMAKE_CXX_COMPILER_TARGET=${INPUTNEXTADAPTERS_CXX_COMPILER_TARGET}")
    endif()
    if(DEFINED INPUTNEXTADAPTERS_MSVC_RUNTIME_LIBRARY)
        list(APPEND configure_args
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=${INPUTNEXTADAPTERS_MSVC_RUNTIME_LIBRARY}"
        )
    endif()
    if(mode STREQUAL "installed")
        list(APPEND configure_args "-DCMAKE_PREFIX_PATH:PATH=${relocated_prefix}")
    endif()
    run_required("${mode} UBSan consumer configure" ${configure_args})

    set(build_args "${CMAKE_COMMAND}" --build "${consumer_build}" --parallel 4)
    if(DEFINED INPUTNEXTADAPTERS_CONFIGURATION AND NOT INPUTNEXTADAPTERS_CONFIGURATION STREQUAL "")
        list(APPEND build_args --config "${INPUTNEXTADAPTERS_CONFIGURATION}")
    endif()
    run_required("${mode} UBSan consumer build" ${build_args})

    foreach(behavior IN ITEMS healthy overflow)
        set(executable_name "ubsan_external_consumer_${behavior}")
        if(DEFINED INPUTNEXTADAPTERS_CONFIGURATION AND NOT INPUTNEXTADAPTERS_CONFIGURATION STREQUAL "")
            set(executable_path "${consumer_build}/${INPUTNEXTADAPTERS_CONFIGURATION}/${executable_name}.exe")
            if(NOT EXISTS "${executable_path}")
                set(executable_path "${consumer_build}/${executable_name}.exe")
            endif()
        else()
            set(executable_path "${consumer_build}/${executable_name}.exe")
        endif()
        if(NOT EXISTS "${executable_path}")
            message(FATAL_ERROR "The ${mode} consumer did not produce ${executable_name}.exe.")
        endif()

        execute_process(COMMAND "${executable_path}"
            RESULT_VARIABLE run_result
            OUTPUT_VARIABLE stdout
            ERROR_VARIABLE stderr
            TIMEOUT 30
        )
        if(behavior STREQUAL "healthy")
            if(NOT run_result STREQUAL "0")
                message(FATAL_ERROR
                    "The healthy ${mode} UBSan consumer failed with exit code ${run_result}.\n${stdout}\n${stderr}")
            endif()
        else()
            if(run_result STREQUAL "0")
                message(FATAL_ERROR "The overflowing ${mode} UBSan consumer unexpectedly succeeded.")
            endif()
            string(CONCAT overflow_output "${stdout}" "${stderr}")
            if(NOT overflow_output MATCHES "signed integer overflow")
                message(FATAL_ERROR
                    "The overflowing ${mode} consumer failed without a UBSan signed-overflow report.\n${overflow_output}")
            endif()
        endif()
    endforeach()
endfunction()

verify_ubsan_consumer("source" "${test_root}/source-consumer")
verify_ubsan_consumer("installed" "${test_root}/installed-consumer")

function(assert_unsupported_consumer case_name compiler runtime expected_diagnostic)
    set(build_directory "${test_root}/unsupported-${case_name}")
    set(configure_args
        "${CMAKE_COMMAND}"
        -S "${INPUTNEXTADAPTERS_SOURCE_DIR}/tests/cmake/ubsan_consumer"
        -B "${build_directory}"
        "-G${INPUTNEXTADAPTERS_GENERATOR}"
        "-DINPUTNEXTADAPTERS_CONSUMER_MODE=installed"
        "-DINPUTNEXTADAPTERS_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DINPUTNEXTADAPTERS_ENABLE_UBSAN:BOOL=ON"
        "-DCMAKE_CXX_COMPILER=${compiler}"
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=${runtime}"
        "-DCMAKE_PREFIX_PATH:PATH=${relocated_prefix}"
    )
    if(DEFINED INPUTNEXTADAPTERS_GENERATOR_PLATFORM AND NOT INPUTNEXTADAPTERS_GENERATOR_PLATFORM STREQUAL "")
        list(APPEND configure_args -A "${INPUTNEXTADAPTERS_GENERATOR_PLATFORM}")
    endif()
    if(DEFINED INPUTNEXTADAPTERS_GENERATOR_TOOLSET AND NOT INPUTNEXTADAPTERS_GENERATOR_TOOLSET STREQUAL "")
        list(APPEND configure_args -T "${INPUTNEXTADAPTERS_GENERATOR_TOOLSET}")
    endif()
    if(DEFINED INPUTNEXTADAPTERS_CONFIGURATION AND NOT INPUTNEXTADAPTERS_CONFIGURATION STREQUAL "")
        list(APPEND configure_args "-DCMAKE_BUILD_TYPE=${INPUTNEXTADAPTERS_CONFIGURATION}")
    endif()
    if(NOT case_name STREQUAL "unsupported-compiler" AND
       DEFINED INPUTNEXTADAPTERS_CXX_COMPILER_TARGET AND
       NOT INPUTNEXTADAPTERS_CXX_COMPILER_TARGET STREQUAL "")
        list(APPEND configure_args "-DCMAKE_CXX_COMPILER_TARGET=${INPUTNEXTADAPTERS_CXX_COMPILER_TARGET}")
    endif()
    execute_process(COMMAND ${configure_args}
        RESULT_VARIABLE configure_result
        OUTPUT_VARIABLE configure_stdout
        ERROR_VARIABLE configure_stderr
        TIMEOUT 180
    )
    if(configure_result STREQUAL "0")
        message(FATAL_ERROR "The installed UBSan package unexpectedly accepted ${case_name}.")
    endif()
    string(CONCAT configure_output "${configure_stdout}" "${configure_stderr}")
    if(NOT configure_output MATCHES "${expected_diagnostic}")
        message(FATAL_ERROR
            "The ${case_name} consumer failed without the expected UBSan diagnostic.\n${configure_output}")
    endif()
endfunction()

assert_unsupported_consumer(
    "dynamic-crt"
    "${INPUTNEXTADAPTERS_CXX_COMPILER}"
    "MultiThreadedDLL"
    "could not link the required runtime"
)
find_program(_inputnext_ubsan_test_msvc_compiler NAMES cl.exe)
if(_inputnext_ubsan_test_msvc_compiler)
    assert_unsupported_consumer(
        "unsupported-compiler"
        "${_inputnext_ubsan_test_msvc_compiler}"
        "${INPUTNEXTADAPTERS_MSVC_RUNTIME_LIBRARY}"
        "UndefinedBehaviorSanitizer requires clang-cl"
    )
endif()
