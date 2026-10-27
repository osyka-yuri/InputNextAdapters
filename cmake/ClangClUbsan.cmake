function(inputnext_configure_clangcl_ubsan_runtime)
    if(TARGET InputNextAdapters::ClangClUbsanRuntimeRequirements)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
       NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR
            "UndefinedBehaviorSanitizer requires clang-cl. Configure with CMAKE_CXX_COMPILER set to clang-cl.")
    endif()

    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The clang-cl UndefinedBehaviorSanitizer runtime is supported only for x64 Windows.")
    endif()

    if(DEFINED CMAKE_CXX_COMPILER_TARGET AND
       NOT CMAKE_CXX_COMPILER_TARGET STREQUAL "" AND
       NOT CMAKE_CXX_COMPILER_TARGET MATCHES "^(x86_64|amd64|x64)([-_].*)?$")
        message(FATAL_ERROR
            "The clang-cl UndefinedBehaviorSanitizer runtime does not support target '${CMAKE_CXX_COMPILER_TARGET}'. "
            "Configure an x64 Windows target.")
    endif()

    if(DEFINED CMAKE_CXX_COMPILER_ARCHITECTURE_ID AND
       NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "" AND
       NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|X64|AMD64)$")
        message(FATAL_ERROR
            "The clang-cl UndefinedBehaviorSanitizer runtime does not support compiler architecture "
            "'${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}'. Configure x64 tools.")
    endif()

    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" --print-runtime-dir
        RESULT_VARIABLE runtime_result
        OUTPUT_VARIABLE runtime_dir
        ERROR_VARIABLE runtime_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT runtime_result STREQUAL "0" OR NOT IS_DIRECTORY "${runtime_dir}")
        message(FATAL_ERROR
            "clang-cl could not resolve its UndefinedBehaviorSanitizer runtime directory. "
            "Compiler output: '${runtime_dir}'. ${runtime_error}")
    endif()

    foreach(runtime_filename IN ITEMS
        clang_rt.ubsan_standalone-x86_64.lib
        clang_rt.ubsan_standalone_cxx-x86_64.lib
    )
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
                "clang-cl could not resolve the required UndefinedBehaviorSanitizer runtime "
                "'${runtime_filename}'. Compiler output: '${runtime_path}'. ${lookup_error}")
        endif()
    endforeach()

    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    unset(INPUTNEXTADAPTERS_CLANGCL_UBSAN_RUNTIME_LINKS CACHE)

    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined -fno-sanitize-recover=undefined")
    set(CMAKE_REQUIRED_LINK_DIRECTORIES "${runtime_dir}")
    set(CMAKE_REQUIRED_LINK_OPTIONS
        "-fsanitize=undefined;-fno-sanitize-recover=undefined;LINKER:/INCLUDE:__ubsan_handle_dynamic_type_cache_miss_abort"
    )
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")

    check_cxx_source_compiles([=[
#include <climits>
#include <vector>

int main(int argc, char **argv)
{
    (void)argv;
    std::vector<int> values{argc};
    volatile int operand = values.front();
    volatile int overflow = operand + INT_MAX;
    return overflow == 0;
}
]=] INPUTNEXTADAPTERS_CLANGCL_UBSAN_RUNTIME_LINKS)

    cmake_pop_check_state()

    if(NOT INPUTNEXTADAPTERS_CLANGCL_UBSAN_RUNTIME_LINKS)
        message(FATAL_ERROR
            "UndefinedBehaviorSanitizer requires a working x64 clang-cl C++ compile-and-link runtime, including "
            "__ubsan_handle_dynamic_type_cache_miss_abort. The active compiler and MSVC runtime could not link "
            "the required runtime. See CMakeFiles/CMakeConfigureLog.yaml for linker diagnostics.")
    endif()

    add_library(InputNextAdapters::ClangClUbsanRuntimeRequirements INTERFACE IMPORTED GLOBAL)
    set_target_properties(InputNextAdapters::ClangClUbsanRuntimeRequirements PROPERTIES
        INTERFACE_COMPILE_OPTIONS "-fsanitize=undefined;-fno-sanitize-recover=undefined"
        INTERFACE_LINK_OPTIONS "-fsanitize=undefined;-fno-sanitize-recover=undefined"
        INTERFACE_LINK_DIRECTORIES "${runtime_dir}"
    )
endfunction()
