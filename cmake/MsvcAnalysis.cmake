function(inputnext_enable_msvc_analysis target)
    if(NOT INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
       OR NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR
            "INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE requires the genuine Microsoft C++ compiler; "
            "clang-cl is not supported."
        )
    endif()

    target_compile_options(${target} PRIVATE
        /analyze
        /analyze:external-
        /external:env:INCLUDE
        /external:W0
        /analyze:autolog:ext
        .sarif
    )
endfunction()
