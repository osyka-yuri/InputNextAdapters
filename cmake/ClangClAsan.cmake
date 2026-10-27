function(_inputnext_capture_clangcl_asan_crt_state directory)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(target_type "${target}" TYPE)
        if(target_type STREQUAL "EXECUTABLE" OR
           target_type STREQUAL "SHARED_LIBRARY" OR
           target_type STREQUAL "MODULE_LIBRARY")
            get_property(crt_is_set TARGET "${target}" PROPERTY MSVC_RUNTIME_LIBRARY SET)
            set_property(TARGET "${target}" PROPERTY
                INPUTNEXTADAPTERS_CLANGCL_ASAN_CRT_IS_SET "${crt_is_set}"
            )
        endif()
    endforeach()

    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        _inputnext_capture_clangcl_asan_crt_state("${subdirectory}")
    endforeach()
endfunction()

function(inputnext_configure_clangcl_asan_runtime)
    if(TARGET InputNextAdapters::ClangClAsanRuntimeRequirements)
        return()
    endif()

    get_property(capture_registered GLOBAL PROPERTY INPUTNEXTADAPTERS_CLANGCL_ASAN_CRT_CAPTURE_REGISTERED)
    if(NOT capture_registered)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
            CALL _inputnext_capture_clangcl_asan_crt_state "${CMAKE_SOURCE_DIR}"
        )
        set_property(GLOBAL PROPERTY INPUTNEXTADAPTERS_CLANGCL_ASAN_CRT_CAPTURE_REGISTERED TRUE)
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
       NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR
            "This AddressSanitizer package was built with clang-cl and requires a clang-cl consumer. "
            "Configure with CMAKE_CXX_COMPILER set to clang-cl.")
    endif()

    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The clang-cl AddressSanitizer runtime is available here only for x64 Windows.")
    endif()

    if(DEFINED CMAKE_CXX_COMPILER_TARGET AND
       NOT CMAKE_CXX_COMPILER_TARGET STREQUAL "" AND
       NOT CMAKE_CXX_COMPILER_TARGET MATCHES "^(x86_64|amd64|x64)([-_].*)?$")
        message(FATAL_ERROR
            "The clang-cl AddressSanitizer runtime does not support target '${CMAKE_CXX_COMPILER_TARGET}'. "
            "Configure an x64 Windows target.")
    endif()

    if(DEFINED CMAKE_CXX_COMPILER_ARCHITECTURE_ID AND
       NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "" AND
       NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|X64|AMD64)$")
        message(FATAL_ERROR
            "The clang-cl AddressSanitizer runtime does not support compiler architecture "
            "'${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}'. Configure x64 tools.")
    endif()

    set(runtime_filenames
        "clang_rt.asan_dynamic-x86_64.dll"
        "clang_rt.asan_dynamic-x86_64.lib"
        "clang_rt.asan_dynamic_runtime_thunk-x86_64.lib"
        "clang_rt.asan_static_runtime_thunk-x86_64.lib"
    )
    set(runtime_paths)
    foreach(runtime_filename IN LISTS runtime_filenames)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" "--print-file-name=${runtime_filename}"
            RESULT_VARIABLE lookup_result
            OUTPUT_VARIABLE runtime_path
            ERROR_VARIABLE lookup_error
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT lookup_result STREQUAL "0" OR
           NOT IS_ABSOLUTE "${runtime_path}" OR
           NOT EXISTS "${runtime_path}")
            message(FATAL_ERROR
                "clang-cl could not resolve the required AddressSanitizer runtime '${runtime_filename}'. "
                "Compiler output: '${runtime_path}'. ${lookup_error}")
        endif()
        list(APPEND runtime_paths "${runtime_path}")
    endforeach()

    list(GET runtime_paths 0 runtime_dll)
    list(GET runtime_paths 1 runtime_import_library)
    list(GET runtime_paths 2 dynamic_thunk)
    list(GET runtime_paths 3 static_thunk)

    add_library(InputNextAdapters::ClangClAsanRuntime SHARED IMPORTED GLOBAL)
    set_target_properties(InputNextAdapters::ClangClAsanRuntime PROPERTIES
        IMPORTED_LOCATION "${runtime_dll}"
        IMPORTED_IMPLIB "${runtime_import_library}"
    )

    add_library(InputNextAdapters::ClangClAsanDynamicThunk STATIC IMPORTED GLOBAL)
    set_target_properties(InputNextAdapters::ClangClAsanDynamicThunk PROPERTIES
        IMPORTED_LOCATION "${dynamic_thunk}"
    )

    add_library(InputNextAdapters::ClangClAsanStaticThunk STATIC IMPORTED GLOBAL)
    set_target_properties(InputNextAdapters::ClangClAsanStaticThunk PROPERTIES
        IMPORTED_LOCATION "${static_thunk}"
    )

    add_library(InputNextAdapters::ClangClAsanRuntimeRequirements INTERFACE IMPORTED GLOBAL)

    # Capture the property's SET state after the parent directory finishes adding targets:
    # evaluating an explicitly empty value alone is indistinguishable from an unset property.
    # The latter uses CMake's default dynamic CRT, while clang-cl uses its static default
    # when the property is explicitly empty and therefore emits no CRT selection flag.
    set(runtime_is_set "$<BOOL:$<TARGET_PROPERTY:INPUTNEXTADAPTERS_CLANGCL_ASAN_CRT_IS_SET>>")
    set(runtime_property
        "$<GENEX_EVAL:$<IF:${runtime_is_set},$<TARGET_PROPERTY:MSVC_RUNTIME_LIBRARY>,MultiThreadedDLL>>"
    )
    set(dynamic_crt
        "$<OR:$<STREQUAL:${runtime_property},MultiThreadedDLL>,$<STREQUAL:${runtime_property},MultiThreadedDebugDLL>>"
    )
    set_property(TARGET InputNextAdapters::ClangClAsanRuntimeRequirements PROPERTY
        INTERFACE_LINK_LIBRARIES
        "InputNextAdapters::ClangClAsanRuntime;$<IF:${dynamic_crt},$<LINK_LIBRARY:WHOLE_ARCHIVE,InputNextAdapters::ClangClAsanDynamicThunk>,$<LINK_LIBRARY:WHOLE_ARCHIVE,InputNextAdapters::ClangClAsanStaticThunk>>"
    )
    set_property(TARGET InputNextAdapters::ClangClAsanRuntimeRequirements PROPERTY
        INTERFACE_LINK_OPTIONS
        "/INCREMENTAL:NO;$<$<BOOL:${dynamic_crt}>:/INCLUDE:__asan_seh_interceptor>"
    )
endfunction()
