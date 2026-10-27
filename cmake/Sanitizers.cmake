function(inputnext_enable_asan target)
    if(NOT INPUTNEXTADAPTERS_ENABLE_ASAN)
        return()
    endif()

    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The configured AddressSanitizer gate requires an x64 build.")
    endif()

    if(NOT MSVC)
        message(FATAL_ERROR "AddressSanitizer requires MSVC or clang-cl.")
    endif()

    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "STATIC_LIBRARY" OR target_type STREQUAL "OBJECT_LIBRARY")
        target_compile_options(${target} PUBLIC /fsanitize=address)
        set(runtime_visibility PUBLIC)
        set(link_options_visibility INTERFACE)
    else()
        target_compile_options(${target} PRIVATE /fsanitize=address)
        set(runtime_visibility PRIVATE)
        set(link_options_visibility PRIVATE)
    endif()

    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_CHECKS "")
    target_link_options(${target} ${link_options_visibility} /INCREMENTAL:NO)

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        if(NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            message(FATAL_ERROR "AddressSanitizer with Clang requires the clang-cl MSVC frontend.")
        endif()

        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ClangClAsan.cmake")
        inputnext_configure_clangcl_asan_runtime()
        target_link_libraries(${target} ${runtime_visibility} InputNextAdapters::ClangClAsanRuntimeRequirements)
    elseif(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR "AddressSanitizer requires MSVC or clang-cl.")
    endif()
endfunction()

function(inputnext_configure_ubsan_runtime)
    if(NOT INPUTNEXTADAPTERS_ENABLE_UBSAN)
        return()
    endif()

    if(INPUTNEXTADAPTERS_ENABLE_ASAN)
        message(FATAL_ERROR "AddressSanitizer and UndefinedBehaviorSanitizer are separate profiles; enable only one.")
    endif()

    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ClangClUbsan.cmake")
    inputnext_configure_clangcl_ubsan_runtime()
endfunction()

function(inputnext_enable_ubsan target)
    if(NOT INPUTNEXTADAPTERS_ENABLE_UBSAN)
        return()
    endif()

    inputnext_configure_ubsan_runtime()

    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "STATIC_LIBRARY" OR target_type STREQUAL "OBJECT_LIBRARY")
        set(runtime_visibility PUBLIC)
    else()
        set(runtime_visibility PRIVATE)
    endif()

    target_link_libraries(${target} ${runtime_visibility} InputNextAdapters::ClangClUbsanRuntimeRequirements)
endfunction()
