include_guard(GLOBAL)

option(INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY "Run clang-tidy for configured owned targets" OFF)

file(READ "${CMAKE_CURRENT_LIST_DIR}/../tools/llvm-version.txt" _inputnext_llvm_version)
string(STRIP "${_inputnext_llvm_version}" _inputnext_llvm_version)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../tools/llvm-version.txt")
get_filename_component(_inputnext_repository_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set_property(GLOBAL PROPERTY INPUTNEXTADAPTERS_SOURCE_ROOT "${_inputnext_repository_root}")
if(NOT _inputnext_llvm_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "tools/llvm-version.txt must contain one LLVM version in MAJOR.MINOR.PATCH form.")
endif()

if(INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY)
    find_program(INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE NAMES clang-tidy.exe clang-tidy)
    if(NOT INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE)
        message(FATAL_ERROR "clang-tidy ${_inputnext_llvm_version} is required when INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY=ON.")
    endif()

    execute_process(
        COMMAND "${INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE}" --version
        RESULT_VARIABLE _inputnext_version_result
        OUTPUT_VARIABLE _inputnext_version_output
        ERROR_VARIABLE _inputnext_version_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _inputnext_detected_version
        "${_inputnext_version_output}${_inputnext_version_error}")
    if(NOT _inputnext_version_result EQUAL 0
       OR NOT _inputnext_detected_version STREQUAL _inputnext_llvm_version)
        message(FATAL_ERROR
            "Expected clang-tidy ${_inputnext_llvm_version}, got: ${_inputnext_version_output}${_inputnext_version_error}")
    endif()
    set_property(GLOBAL PROPERTY INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE "${INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE}")
endif()

function(_inputnext_verify_clang_tidy_config config)
    get_property(_inputnext_verified GLOBAL PROPERTY INPUTNEXTADAPTERS_VERIFIED_TIDY_CONFIGS)
    if(config IN_LIST _inputnext_verified)
        return()
    endif()
    get_property(_inputnext_clang_tidy GLOBAL PROPERTY INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE)
    get_filename_component(_inputnext_config_dir "${config}" DIRECTORY)
    execute_process(
        COMMAND ${_inputnext_clang_tidy} --verify-config
        WORKING_DIRECTORY "${_inputnext_config_dir}"
        RESULT_VARIABLE _inputnext_config_result
        OUTPUT_VARIABLE _inputnext_config_output
        ERROR_VARIABLE _inputnext_config_error
    )
    if(NOT _inputnext_config_result EQUAL 0)
        message(FATAL_ERROR
            "Invalid clang-tidy configuration '${config}':\n${_inputnext_config_output}${_inputnext_config_error}")
    endif()
    set_property(GLOBAL APPEND PROPERTY INPUTNEXTADAPTERS_VERIFIED_TIDY_CONFIGS "${config}")
endfunction()

function(inputnext_enable_clang_tidy target)
    if(NOT INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY)
        return()
    endif()

    # Sources and PCH inputs may be added after the common target configuration.
    cmake_language(EVAL CODE
        "cmake_language(DEFER CALL _inputnext_finalize_clang_tidy [[${target}]])")
endfunction()

function(_inputnext_skip_clang_tidy_for_precompiled_headers target)
    get_target_property(_inputnext_precompiled_headers "${target}" PRECOMPILE_HEADERS)
    get_target_property(_inputnext_reused_precompiled_headers "${target}" PRECOMPILE_HEADERS_REUSE_FROM)
    if(NOT _inputnext_precompiled_headers OR _inputnext_reused_precompiled_headers)
        return()
    endif()

    get_target_property(_inputnext_binary_dir "${target}" BINARY_DIR)
    # This profile requires an MSVC-compatible frontend, which uses these PCH source names.
    foreach(_inputnext_extension IN ITEMS c cxx)
        set_property(SOURCE
            "${_inputnext_binary_dir}/CMakeFiles/${target}.dir/cmake_pch.${_inputnext_extension}"
            TARGET_DIRECTORY "${target}"
            PROPERTY SKIP_LINTING TRUE
        )
    endforeach()
endfunction()

function(_inputnext_finalize_clang_tidy target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Cannot enable clang-tidy for missing target '${target}'.")
    endif()
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P MATCHES "^(4|8)$")
        message(FATAL_ERROR "The clang-tidy profile requires a Windows x86 or x64 target.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(MSVC|Clang)$"
       OR NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR "The clang-tidy profile requires MSVC or clang-cl compilation flags.")
    endif()

    get_property(_inputnext_clang_tidy GLOBAL PROPERTY INPUTNEXTADAPTERS_CLANG_TIDY_EXECUTABLE)
    if(NOT _inputnext_clang_tidy)
        message(FATAL_ERROR "The clang-tidy executable was not initialized for the enabled profile.")
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(_inputnext_target_triple i686-pc-windows-msvc)
    else()
        set(_inputnext_target_triple x86_64-pc-windows-msvc)
    endif()

    # The trailing /Y- overrides /Yu from the compiler database for analysis only.
    set(_inputnext_tidy_command
        "${_inputnext_clang_tidy}"
        "--extra-arg-before=--target=${_inputnext_target_triple}"
        "--extra-arg=/Y-"
        "--extra-arg-before=-Qunused-arguments"
    )
    set_property(TARGET "${target}" PROPERTY C_CLANG_TIDY "${_inputnext_tidy_command}")
    # Keep MSVC exception and textual forced-include semantics in Clang tooling.
    list(APPEND _inputnext_tidy_command "--extra-arg=/EHsc")
    get_target_property(_inputnext_pch_headers "${target}" PRECOMPILE_HEADERS)
    if(_inputnext_pch_headers)
        foreach(_inputnext_pch_header IN LISTS _inputnext_pch_headers)
            string(REGEX REPLACE "^<(.+)>$" "\\1" _inputnext_pch_header "${_inputnext_pch_header}")
            list(APPEND _inputnext_tidy_command "--extra-arg-before=/FI${_inputnext_pch_header}")
        endforeach()
    endif()
    set_property(TARGET "${target}" PROPERTY CXX_CLANG_TIDY "${_inputnext_tidy_command}")

    # CMake's generated PCH translation unit lives in the binary tree, where it has no
    # source-tree .clang-tidy configuration. The actual owned sources still lint the PCH
    # headers through their forced includes, so skip only this synthetic translation unit.
    _inputnext_skip_clang_tidy_for_precompiled_headers("${target}")

    get_target_property(_inputnext_sources "${target}" SOURCES)
    get_target_property(_inputnext_source_dir "${target}" SOURCE_DIR)
    get_target_property(_inputnext_binary_dir "${target}" BINARY_DIR)
    get_property(_inputnext_repository_root GLOBAL PROPERTY INPUTNEXTADAPTERS_SOURCE_ROOT)
    foreach(_inputnext_source IN LISTS _inputnext_sources)
        if(_inputnext_source MATCHES "\\$<")
            continue()
        endif()

        if(IS_ABSOLUTE "${_inputnext_source}")
            cmake_path(NORMAL_PATH _inputnext_source OUTPUT_VARIABLE _inputnext_source_path)
        else()
            cmake_path(ABSOLUTE_PATH _inputnext_source
                BASE_DIRECTORY "${_inputnext_source_dir}"
                NORMALIZE
                OUTPUT_VARIABLE _inputnext_source_path
            )
        endif()

        cmake_path(IS_PREFIX _inputnext_binary_dir "${_inputnext_source_path}" NORMALIZE _inputnext_in_build_tree)
        get_property(_inputnext_generated SOURCE "${_inputnext_source}" TARGET_DIRECTORY "${target}" PROPERTY GENERATED)
        if(_inputnext_in_build_tree OR _inputnext_generated OR _inputnext_source_path MATCHES "[/\\\\]cmake_pch([.]|\\.)")
            set_property(SOURCE "${_inputnext_source}" TARGET_DIRECTORY "${target}" PROPERTY SKIP_LINTING TRUE)
            continue()
        endif()

        if(NOT _inputnext_source_path MATCHES "[.](c|cc|cpp|cxx|c[+][+])$")
            continue()
        endif()

        get_filename_component(_inputnext_config_dir "${_inputnext_source_path}" DIRECTORY)
        cmake_path(IS_PREFIX _inputnext_repository_root "${_inputnext_config_dir}" NORMALIZE _inputnext_in_repository)
        if(NOT _inputnext_in_repository)
            continue()
        endif()

        # Parent configs may be inherited, so edits at any level must rerun this source.
        set(_inputnext_configs "")
        while(TRUE)
            if(EXISTS "${_inputnext_config_dir}/.clang-tidy")
                list(APPEND _inputnext_configs "${_inputnext_config_dir}/.clang-tidy")
            endif()
            if(_inputnext_config_dir STREQUAL _inputnext_repository_root)
                break()
            endif()
            get_filename_component(_inputnext_parent_dir "${_inputnext_config_dir}" DIRECTORY)
            if(_inputnext_parent_dir STREQUAL _inputnext_config_dir)
                break()
            endif()
            cmake_path(IS_PREFIX _inputnext_repository_root "${_inputnext_parent_dir}" NORMALIZE _inputnext_parent_in_repository)
            if(NOT _inputnext_parent_in_repository)
                break()
            endif()
            set(_inputnext_config_dir "${_inputnext_parent_dir}")
        endwhile()

        list(REMOVE_DUPLICATES _inputnext_configs)
        foreach(_inputnext_config IN LISTS _inputnext_configs)
            _inputnext_verify_clang_tidy_config("${_inputnext_config}")
            set_property(SOURCE "${_inputnext_source}" TARGET_DIRECTORY "${target}" APPEND
                PROPERTY OBJECT_DEPENDS "${_inputnext_config}")
            set_property(DIRECTORY "${_inputnext_source_dir}" APPEND
                PROPERTY CMAKE_CONFIGURE_DEPENDS "${_inputnext_config}")
        endforeach()
    endforeach()
endfunction()
