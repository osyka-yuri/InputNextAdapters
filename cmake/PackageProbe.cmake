include_guard(GLOBAL)

set(INPUTNEXTADAPTERS_PACKAGE_PLUGIN "" CACHE FILEPATH
    "Optional absolute plugin DLL from an extracted package for native package probes")

function(inputnext_package_probe_argument output_variable target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "Cannot select a package probe path for missing target '${target_name}'.")
    endif()

    if(INPUTNEXTADAPTERS_PACKAGE_PLUGIN)
        if(IS_ABSOLUTE "${INPUTNEXTADAPTERS_PACKAGE_PLUGIN}")
            set(_candidate "${INPUTNEXTADAPTERS_PACKAGE_PLUGIN}")
        else()
            get_filename_component(_candidate "${INPUTNEXTADAPTERS_PACKAGE_PLUGIN}"
                ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
        file(REAL_PATH "${_candidate}" _canonical)
        if(NOT EXISTS "${_canonical}" OR IS_DIRECTORY "${_canonical}")
            message(FATAL_ERROR
                "INPUTNEXTADAPTERS_PACKAGE_PLUGIN must identify an existing DLL file: '${INPUTNEXTADAPTERS_PACKAGE_PLUGIN}'.")
        endif()
        get_filename_component(_extension "${_canonical}" EXT)
        string(TOLOWER "${_extension}" _extension)
        if(NOT _extension STREQUAL ".dll")
            message(FATAL_ERROR
                "INPUTNEXTADAPTERS_PACKAGE_PLUGIN must identify a DLL file: '${_canonical}'.")
        endif()
        set(${output_variable} "${_canonical}" PARENT_SCOPE)
        return()
    endif()

    set(${output_variable} "$<TARGET_FILE:${target_name}>" PARENT_SCOPE)
endfunction()
