include_guard(GLOBAL)

option(INPUTNEXTADAPTERS_PRODUCTION "Use the optimized Release policy for owned product targets" OFF)

function(inputnext_prepare_production_configuration)
    if(NOT INPUTNEXTADAPTERS_PRODUCTION)
        return()
    endif()

    if(INPUTNEXTADAPTERS_ENABLE_ASAN OR INPUTNEXTADAPTERS_ENABLE_UBSAN OR
       INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR
            "INPUTNEXTADAPTERS_PRODUCTION requires the genuine Microsoft C++ compiler.")
    endif()

    # The owned Release targets add /Ob3 privately. Remove CMake's inherited /Ob2
    # at the participating directory scope to avoid D9025 without changing the cache.
    foreach(_inputnext_language IN ITEMS C CXX)
        if(DEFINED CMAKE_${_inputnext_language}_FLAGS_RELEASE)
            string(REGEX REPLACE "(^|[ \t])[/-]Ob[0-3]([ \t]|$)" "\\1\\2"
                _inputnext_release_flags "${CMAKE_${_inputnext_language}_FLAGS_RELEASE}")
            string(REGEX REPLACE "[ \t]+" " " _inputnext_release_flags "${_inputnext_release_flags}")
            string(STRIP "${_inputnext_release_flags}" _inputnext_release_flags)
            set(CMAKE_${_inputnext_language}_FLAGS_RELEASE "${_inputnext_release_flags}" PARENT_SCOPE)
        endif()
    endforeach()
endfunction()

function(inputnext_configure_production target)
    if(NOT INPUTNEXTADAPTERS_PRODUCTION)
        return()
    endif()

    # Diagnostic configurations must remain separate from optimized production artifacts.
    if(INPUTNEXTADAPTERS_ENABLE_ASAN OR INPUTNEXTADAPTERS_ENABLE_UBSAN OR
       INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR
            "INPUTNEXTADAPTERS_PRODUCTION requires the genuine Microsoft C++ compiler.")
    endif()

    set_property(TARGET "${target}" PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    target_compile_options("${target}" PRIVATE
        "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/O2>"
        "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/Ob3>"
        "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/Gw>"
        "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/Gy>"
        "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/Zi>"
        "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/fp:precise>"
    )

    get_target_property(_inputnext_production_type "${target}" TYPE)
    if(_inputnext_production_type STREQUAL "EXECUTABLE" OR
       _inputnext_production_type STREQUAL "SHARED_LIBRARY" OR
       _inputnext_production_type STREQUAL "MODULE_LIBRARY")
        target_link_options("${target}" PRIVATE
            "$<$<AND:$<CONFIG:Release>,$<LINK_LANG_AND_ID:CXX,MSVC>>:/WX>"
            "$<$<AND:$<CONFIG:Release>,$<LINK_LANG_AND_ID:CXX,MSVC>>:/DEBUG:FULL>"
            "$<$<AND:$<CONFIG:Release>,$<LINK_LANG_AND_ID:CXX,MSVC>>:/OPT:REF,ICF>"
            "$<$<AND:$<CONFIG:Release>,$<LINK_LANG_AND_ID:CXX,MSVC>>:/INCREMENTAL:NO>"
            "$<$<AND:$<CONFIG:Release>,$<LINK_LANG_AND_ID:CXX,MSVC>>:/DYNAMICBASE>"
            "$<$<AND:$<CONFIG:Release>,$<LINK_LANG_AND_ID:CXX,MSVC>>:/NXCOMPAT>"
            "$<$<AND:$<CONFIG:Release>,$<LINK_LANG_AND_ID:CXX,MSVC>>:/PDBALTPATH:%_PDB%>"
        )
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            target_link_options("${target}" PRIVATE
                "$<$<AND:$<CONFIG:Release>,$<LINK_LANG_AND_ID:CXX,MSVC>>:/HIGHENTROPYVA>"
            )
        endif()
    endif()
endfunction()
