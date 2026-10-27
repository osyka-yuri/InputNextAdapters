include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CompilerWarnings.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Sanitizers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/MsvcAnalysis.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ClangTidy.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ProductionOptions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VendorOptions.cmake")

function(inputnext_configure_target target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Cannot configure missing owned target '${target}'.")
    endif()
    cmake_parse_arguments(_inputnext_configure "PRODUCTION" "" "" ${ARGN})
    if(_inputnext_configure_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown inputnext_configure_target arguments: ${_inputnext_configure_UNPARSED_ARGUMENTS}")
    endif()

    set_target_properties("${target}" PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_features("${target}" PUBLIC cxx_std_23)
    target_compile_definitions("${target}" PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    inputnext_enable_warnings("${target}")
    inputnext_enable_asan("${target}")
    inputnext_enable_ubsan("${target}")
    inputnext_enable_msvc_analysis("${target}")
    inputnext_enable_clang_tidy("${target}")
    if(_inputnext_configure_PRODUCTION)
        inputnext_configure_production("${target}")
    endif()
endfunction()

function(inputnext_configure_c_target target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Cannot configure missing owned target '${target}'.")
    endif()

    set_target_properties("${target}" PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_compile_features("${target}" PRIVATE c_std_11)
    target_compile_definitions("${target}" PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    inputnext_enable_warnings("${target}")
    inputnext_enable_asan("${target}")
    inputnext_enable_ubsan("${target}")
    inputnext_enable_msvc_analysis("${target}")
    inputnext_enable_clang_tidy("${target}")
endfunction()

# Vendor warning exceptions belong to vendor source compilation, not every consumer TU.
# PUBLIC compile options are also stored in COMPILE_OPTIONS, so filtering only the
# interface preserves the dependency's own ABI/header exceptions.
