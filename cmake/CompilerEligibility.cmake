function(inputnext_check_compiler)
    if(NOT WIN32)
        message(FATAL_ERROR "InputNextAdapters supports Windows targets only.")
    endif()

    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "InputNextAdapters supports 32-bit and 64-bit Windows targets.")
    endif()

    if(DEFINED INPUTNEXTADAPTERS_EXPECTED_BITS AND NOT INPUTNEXTADAPTERS_EXPECTED_BITS STREQUAL "")
        math(EXPR actual_bits "${CMAKE_SIZEOF_VOID_P} * 8")
        if(NOT "${actual_bits}" STREQUAL "${INPUTNEXTADAPTERS_EXPECTED_BITS}")
            message(FATAL_ERROR
                "This preset expects ${INPUTNEXTADAPTERS_EXPECTED_BITS}-bit tools. "
                "Start the matching Visual Studio Developer PowerShell before configuring.")
        endif()
    endif()

    if(NOT MSVC)
        message(FATAL_ERROR "Use MSVC or clang-cl with the Windows SDK and MSVC-compatible linker.")
    endif()
endfunction()
