foreach(flag INPUTNEXTADAPTERS_ENABLE_ASAN INPUTNEXTADAPTERS_ENABLE_UBSAN
        INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY)
    if(NOT DEFINED ${flag})
        set(${flag} OFF)
    endif()
endforeach()

set(_inputnext_helpers_dir)
if(DEFINED INPUTNEXTADAPTERS_HELPERS_SOURCE_DIR AND
   EXISTS "${INPUTNEXTADAPTERS_HELPERS_SOURCE_DIR}/cmake/TargetOptions.cmake")
    set(_inputnext_helpers_dir "${INPUTNEXTADAPTERS_HELPERS_SOURCE_DIR}/cmake")
elseif(DEFINED INPUTNEXTADAPTERS_SOURCE_DIR AND
       EXISTS "${INPUTNEXTADAPTERS_SOURCE_DIR}/cmake/TargetOptions.cmake")
    set(_inputnext_helpers_dir "${INPUTNEXTADAPTERS_SOURCE_DIR}/cmake")
endif()

if(_inputnext_helpers_dir)
    include("${_inputnext_helpers_dir}/TargetOptions.cmake")
    set(_inputnext_have_check_helpers ON)
elseif(INPUTNEXTADAPTERS_ENABLE_ASAN OR INPUTNEXTADAPTERS_ENABLE_UBSAN OR
       INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE OR INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY)
    message(FATAL_ERROR "Enabled compiler checks require the source-relative InputNextAdapters CMake helpers.")
else()
    set(_inputnext_have_check_helpers OFF)
endif()

function(inputnext_apply_consumer_checks target)
    if(_inputnext_have_check_helpers)
        inputnext_configure_target(${target})
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    endif()
endfunction()
