foreach(required INPUTNEXTADAPTERS_SOURCE_DIR INPUTNEXTADAPTERS_BINARY_DIR INPUTNEXTADAPTERS_BUILD_DIR INPUTNEXTADAPTERS_GENERATOR INPUTNEXTADAPTERS_CTEST_COMMAND)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required -D${required}=... argument.")
    endif()
endforeach()

file(STRINGS "${INPUTNEXTADAPTERS_BUILD_DIR}/CMakeCache.txt" asan_cache_entry REGEX "^INPUTNEXTADAPTERS_ENABLE_ASAN:BOOL=")
if(NOT asan_cache_entry STREQUAL "INPUTNEXTADAPTERS_ENABLE_ASAN:BOOL=ON")
    message(FATAL_ERROR "The outer build must have INPUTNEXTADAPTERS_ENABLE_ASAN=ON.")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
cmake_path(ABSOLUTE_PATH INPUTNEXTADAPTERS_BINARY_DIR NORMALIZE OUTPUT_VARIABLE build_root)
set(test_root "${build_root}/asan-external-consumer-${run_id}")
cmake_path(IS_PREFIX build_root "${test_root}" NORMALIZE test_root_is_owned)
if(NOT test_root_is_owned)
    message(FATAL_ERROR "The ASAN consumer test output path is outside its owning build directory.")
endif()
file(MAKE_DIRECTORY "${test_root}")

function(run_required description)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 150
    )
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "${description} failed with exit code ${result}.\n${stdout}\n${stderr}")
    endif()
endfunction()

set(package_prefix "${test_root}/installed-prefix")
set(install_args "${CMAKE_COMMAND}" --install "${INPUTNEXTADAPTERS_BUILD_DIR}" --prefix "${package_prefix}")
if(DEFINED INPUTNEXTADAPTERS_CONFIGURATION AND NOT INPUTNEXTADAPTERS_CONFIGURATION STREQUAL "")
    list(APPEND install_args --config "${INPUTNEXTADAPTERS_CONFIGURATION}")
endif()
run_required("package installation" ${install_args})

function(verify_external_consumer mode consumer_build)
    set(configure_args
        "${CMAKE_COMMAND}"
        -S "${INPUTNEXTADAPTERS_SOURCE_DIR}/tests/cmake/asan_consumer"
        -B "${consumer_build}"
        "-G${INPUTNEXTADAPTERS_GENERATOR}"
        "-DINPUTNEXTADAPTERS_CONSUMER_MODE=${mode}"
        "-DINPUTNEXTADAPTERS_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DINPUTNEXTADAPTERS_CONSUMER_MAIN=${INPUTNEXTADAPTERS_SOURCE_DIR}/tests/consumer/main.cpp"
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
        list(APPEND configure_args "-DCMAKE_PREFIX_PATH:PATH=${package_prefix}")
    endif()
    run_required("${mode} consumer configure" ${configure_args})

    set(build_args "${CMAKE_COMMAND}" --build "${consumer_build}" --parallel 4)
    if(DEFINED INPUTNEXTADAPTERS_CONFIGURATION AND NOT INPUTNEXTADAPTERS_CONFIGURATION STREQUAL "")
        list(APPEND build_args --config "${INPUTNEXTADAPTERS_CONFIGURATION}")
    endif()
    run_required("${mode} consumer build" ${build_args})

    set(ctest_args "${INPUTNEXTADAPTERS_CTEST_COMMAND}" --test-dir "${consumer_build}" --output-on-failure --timeout 60 --no-tests=error)
    if(DEFINED INPUTNEXTADAPTERS_CONFIGURATION AND NOT INPUTNEXTADAPTERS_CONFIGURATION STREQUAL "")
        list(APPEND ctest_args -C "${INPUTNEXTADAPTERS_CONFIGURATION}")
    endif()
    run_required("${mode} consumer CTest" ${ctest_args})
endfunction()

verify_external_consumer("source" "${test_root}/source-consumer")
verify_external_consumer("installed" "${test_root}/installed-consumer")
