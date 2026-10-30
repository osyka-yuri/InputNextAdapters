if(NOT DEFINED INPUTNEXTADAPTERS_MODE OR
   NOT INPUTNEXTADAPTERS_MODE MATCHES "^(add_subdirectory|installed)$")
    message(FATAL_ERROR "Set INPUTNEXTADAPTERS_MODE to add_subdirectory or installed.")
endif()
foreach(required INPUTNEXTADAPTERS_SOURCE_DIR INPUTNEXTADAPTERS_BINARY_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing required -D${required}=... argument.")
    endif()
endforeach()
if(NOT DEFINED INPUTNEXTADAPTERS_GENERATOR OR INPUTNEXTADAPTERS_GENERATOR STREQUAL "")
    set(INPUTNEXTADAPTERS_GENERATOR "Ninja")
endif()
if(NOT DEFINED INPUTNEXTADAPTERS_CONSUMER OR INPUTNEXTADAPTERS_CONSUMER STREQUAL "")
    set(INPUTNEXTADAPTERS_CONSUMER "effects_probe")
endif()
foreach(flag INPUTNEXTADAPTERS_ENABLE_ASAN INPUTNEXTADAPTERS_ENABLE_UBSAN
        INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY)
    if(NOT DEFINED ${flag})
        set(${flag} OFF)
    endif()
endforeach()
if(NOT INPUTNEXTADAPTERS_CONSUMER MATCHES "^(effects_probe|common_effects)$")
    message(FATAL_ERROR "INPUTNEXTADAPTERS_CONSUMER must be effects_probe or common_effects.")
endif()

get_filename_component(source_dir "${INPUTNEXTADAPTERS_SOURCE_DIR}" REALPATH)
get_filename_component(binary_dir "${INPUTNEXTADAPTERS_BINARY_DIR}" REALPATH)
include("${CMAKE_CURRENT_LIST_DIR}/VerifyConsumerLogic.cmake")
if(INPUTNEXTADAPTERS_CONSUMER STREQUAL "effects_probe")
    set(consumer_source_dir "${source_dir}/examples/effects_probe")
    set(consumer_executable "effects_probe")
else()
    set(consumer_source_dir "${source_dir}/tests/consumer")
    set(consumer_executable "common_effects_consumer")
endif()
if(NOT EXISTS "${consumer_source_dir}/CMakeLists.txt")
    message(FATAL_ERROR "The adapter source tree does not contain the selected standalone consumer.")
endif()

inputnext_prepare_consumer_directory(
    "${binary_dir}" "${INPUTNEXTADAPTERS_MODE}" "${INPUTNEXTADAPTERS_CONSUMER}"
    binary_dir integration_root consumer_build
)
set(configure_args
    -S "${consumer_source_dir}"
    -B "${consumer_build}"
    "-G${INPUTNEXTADAPTERS_GENERATOR}"
)
if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND configure_args -A "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND configure_args -T "${CMAKE_GENERATOR_TOOLSET}")
endif()
if(DEFINED INPUTNEXTADAPTERS_CONFIGURATION AND NOT INPUTNEXTADAPTERS_CONFIGURATION STREQUAL "")
    list(APPEND configure_args "-DCMAKE_BUILD_TYPE=${INPUTNEXTADAPTERS_CONFIGURATION}")
endif()
if(DEFINED INPUTNEXTADAPTERS_CXX_COMPILER AND NOT INPUTNEXTADAPTERS_CXX_COMPILER STREQUAL "")
    list(APPEND configure_args "-DCMAKE_CXX_COMPILER=${INPUTNEXTADAPTERS_CXX_COMPILER}")
endif()
if(DEFINED INPUTNEXTADAPTERS_CXX_COMPILER_TARGET AND NOT INPUTNEXTADAPTERS_CXX_COMPILER_TARGET STREQUAL "")
    list(APPEND configure_args "-DCMAKE_CXX_COMPILER_TARGET=${INPUTNEXTADAPTERS_CXX_COMPILER_TARGET}")
endif()
if(DEFINED INPUTNEXTADAPTERS_MSVC_RUNTIME_LIBRARY)
    list(APPEND configure_args
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=${INPUTNEXTADAPTERS_MSVC_RUNTIME_LIBRARY}"
    )
endif()
list(APPEND configure_args
    "-DINPUTNEXTADAPTERS_ENABLE_ASAN:BOOL=${INPUTNEXTADAPTERS_ENABLE_ASAN}"
    "-DINPUTNEXTADAPTERS_ENABLE_UBSAN:BOOL=${INPUTNEXTADAPTERS_ENABLE_UBSAN}"
    "-DINPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE:BOOL=${INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE}"
    "-DINPUTNEXTADAPTERS_ENABLE_CLANG_TIDY:BOOL=${INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY}"
    "-DINPUTNEXTADAPTERS_HELPERS_SOURCE_DIR:PATH=${source_dir}"
    -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON
)

if(INPUTNEXTADAPTERS_MODE STREQUAL "add_subdirectory")
    list(APPEND configure_args "-DINPUTNEXTADAPTERS_SOURCE_DIR:PATH=${source_dir}")
else()
    if(DEFINED INPUTNEXTADAPTERS_INSTALL_PREFIX AND
       IS_DIRECTORY "${INPUTNEXTADAPTERS_INSTALL_PREFIX}")
        set(install_source_prefix "${INPUTNEXTADAPTERS_INSTALL_PREFIX}")
        set(install_prefix_is_owned FALSE)
    elseif(DEFINED INPUTNEXTADAPTERS_BUILD_DIR AND
           IS_DIRECTORY "${INPUTNEXTADAPTERS_BUILD_DIR}")
        set(install_source_prefix "${consumer_build}/install-prefix")
        set(install_prefix_is_owned TRUE)
    else()
        message(FATAL_ERROR "installed mode requires an existing INPUTNEXTADAPTERS_INSTALL_PREFIX or INPUTNEXTADAPTERS_BUILD_DIR.")
    endif()

    set(relocated_prefix "${consumer_build}/relocated-prefix")
    if(install_prefix_is_owned)
        inputnext_assert_removal_preserves_path(
            "${install_source_prefix}" "${INPUTNEXTADAPTERS_BUILD_DIR}" "package installation cleanup"
        )
        inputnext_remove_owned_package_prefix(
            "${install_source_prefix}" "${binary_dir}" "${integration_root}" "${consumer_build}"
        )
        set(install_args --install "${INPUTNEXTADAPTERS_BUILD_DIR}" --prefix "${install_source_prefix}")
        if(DEFINED INPUTNEXTADAPTERS_CONFIGURATION AND NOT INPUTNEXTADAPTERS_CONFIGURATION STREQUAL "")
            list(APPEND install_args --config "${INPUTNEXTADAPTERS_CONFIGURATION}")
        endif()
        execute_process(COMMAND "${CMAKE_COMMAND}" ${install_args} RESULT_VARIABLE install_result)
        if(NOT install_result EQUAL 0)
            message(FATAL_ERROR "Installing the package for relocated-consumer verification failed with exit code ${install_result}.")
        endif()
    endif()
    inputnext_relocate_install_prefix(
        "${install_source_prefix}" "${relocated_prefix}" "${binary_dir}" "${integration_root}" "${consumer_build}"
    )
    list(APPEND configure_args "-DCMAKE_PREFIX_PATH:PATH=${relocated_prefix}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" ${configure_args} RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Standalone consumer configure failed with exit code ${configure_result}.")
endif()

function(assert_cache_bool build_directory flag expected)
    file(STRINGS "${build_directory}/CMakeCache.txt" cache_entry REGEX "^${flag}:BOOL=")
    if(NOT cache_entry STREQUAL "${flag}:BOOL=${expected}")
        message(FATAL_ERROR "${flag} was not explicitly configured as ${expected} in ${build_directory}.")
    endif()
endfunction()

assert_cache_bool("${consumer_build}" INPUTNEXTADAPTERS_ENABLE_ASAN "${INPUTNEXTADAPTERS_ENABLE_ASAN}")
assert_cache_bool("${consumer_build}" INPUTNEXTADAPTERS_ENABLE_UBSAN "${INPUTNEXTADAPTERS_ENABLE_UBSAN}")
assert_cache_bool("${consumer_build}" INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE "${INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE}")
assert_cache_bool("${consumer_build}" INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY "${INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY}")

set(build_args --build "${consumer_build}")
if(DEFINED INPUTNEXTADAPTERS_CONFIGURATION AND NOT INPUTNEXTADAPTERS_CONFIGURATION STREQUAL "")
    list(APPEND build_args --config "${INPUTNEXTADAPTERS_CONFIGURATION}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${build_args} RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Standalone consumer build failed with exit code ${build_result}.")
endif()

if(INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE)
    if(INPUTNEXTADAPTERS_CONSUMER STREQUAL "effects_probe")
        set(consumer_source_pattern "effects_probe\\.cpp")
    else()
        set(consumer_source_pattern "main\\.cpp")
    endif()
    assert_analyze_compile_command(
        "${consumer_build}" "${consumer_source_pattern}" "${consumer_executable}" "${INPUTNEXTADAPTERS_CONFIGURATION}"
    )
    if(INPUTNEXTADAPTERS_MODE STREQUAL "add_subdirectory")
        set(common_library_build "${consumer_build}")
    else()
        set(common_library_build "${INPUTNEXTADAPTERS_BUILD_DIR}")
    endif()
    assert_analyze_compile_command(
        "${common_library_build}" "body_program\\.cpp" "inputnext_adapters_effects" "${INPUTNEXTADAPTERS_CONFIGURATION}"
    )
endif()

resolve_strict_consumer_executable(
    "${consumer_build}"
    "${consumer_executable}"
    "${INPUTNEXTADAPTERS_CONFIGURATION}"
    consumer_executable_path
)

if(INPUTNEXTADAPTERS_CONSUMER STREQUAL "effects_probe")
    set(run_args "${consumer_executable_path}" --help)
else()
    set(run_args "${consumer_executable_path}")
endif()
execute_process(COMMAND ${run_args} RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "The standalone ${INPUTNEXTADAPTERS_CONSUMER} consumer failed to run with exit code ${run_result}.")
endif()
