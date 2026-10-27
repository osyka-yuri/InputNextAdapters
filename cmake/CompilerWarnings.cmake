function(inputnext_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX
            /utf-8
            "$<$<COMPILE_LANGUAGE:CXX>:/permissive->"
            "$<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>"
            "$<$<COMPILE_LANGUAGE:CXX>:/Zc:inline>"
            "$<$<COMPILE_LANGUAGE:CXX>:/volatile:iso>"
        )
        if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            target_compile_options(${target} PRIVATE
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:preprocessor>"
                "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/sdl>"
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/sdl>"
                "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/options:strict>"
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/options:strict>"
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w44242>"
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w44254>"
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w44287>"
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w44296>"
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w44062>"
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w44265>"
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w44365>"
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w44388>"
            )
        endif()
    endif()
endfunction()
