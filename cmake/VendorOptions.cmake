include_guard(GLOBAL)

function(_inputnext_filter_vendor_shell_option option output)
    string(SUBSTRING "${option}" 6 -1 _inputnext_shell)
    separate_arguments(_inputnext_arguments UNIX_COMMAND "${_inputnext_shell}")
    set(_inputnext_filtered "")
    foreach(_inputnext_argument IN LISTS _inputnext_arguments)
        if(NOT _inputnext_argument MATCHES "^[/-][wW][dD][0-9]+$")
            string(REPLACE "\\" "\\\\" _inputnext_argument "${_inputnext_argument}")
            string(REPLACE "\"" "\\\"" _inputnext_argument "${_inputnext_argument}")
            string(APPEND _inputnext_filtered " \"${_inputnext_argument}\"")
        endif()
    endforeach()
    string(STRIP "${_inputnext_filtered}" _inputnext_filtered)
    if(_inputnext_filtered)
        set(${output} "SHELL:${_inputnext_filtered}" PARENT_SCOPE)
    else()
        set(${output} "" PARENT_SCOPE)
    endif()
endfunction()

# PUBLIC options also live in COMPILE_OPTIONS; changing only the interface keeps
# vendor source exceptions while enforcing project warnings on consumers.
function(inputnext_scope_vendor_warning_suppressions target)
    if(NOT TARGET "${target}")
        return()
    endif()
    get_target_property(_inputnext_vendor_real_target "${target}" ALIASED_TARGET)
    if(_inputnext_vendor_real_target)
        set(target "${_inputnext_vendor_real_target}")
    endif()
    get_property(_inputnext_scoped TARGET "${target}"
        PROPERTY INPUTNEXTADAPTERS_VENDOR_COMPILE_OPTIONS SET)
    if(_inputnext_scoped)
        return()
    endif()

    get_target_property(_inputnext_options "${target}" INTERFACE_COMPILE_OPTIONS)
    if(NOT _inputnext_options OR _inputnext_options MATCHES "-NOTFOUND$")
        return()
    endif()

    set(_inputnext_public_options)
    foreach(_inputnext_option IN LISTS _inputnext_options)
        string(TOLOWER "${_inputnext_option}" _inputnext_option_lower)
        if(_inputnext_option_lower MATCHES "shell:.*[/-]wd[0-9]+")
            if(_inputnext_option MATCHES "\\$<")
                message(FATAL_ERROR
                    "Split conditional SHELL warning suppressions into standalone options on '${target}': ${_inputnext_option}")
            endif()
            if(_inputnext_option MATCHES "^SHELL:")
                _inputnext_filter_vendor_shell_option("${_inputnext_option}" _inputnext_option)
            endif()
        endif()
        if(_inputnext_option)
            list(APPEND _inputnext_public_options "${_inputnext_option}")
        endif()
    endforeach()

    # Evaluate conditional lists in the consumer context before removing exact
    # suppression arguments. The snapshot keeps commas and nested expressions intact.
    set_property(TARGET "${target}" PROPERTY INPUTNEXTADAPTERS_VENDOR_COMPILE_OPTIONS
        "${_inputnext_public_options}")
    set_property(TARGET "${target}" PROPERTY INTERFACE_COMPILE_OPTIONS
        "$<FILTER:$<GENEX_EVAL:$<TARGET_PROPERTY:${target},INPUTNEXTADAPTERS_VENDOR_COMPILE_OPTIONS>>,EXCLUDE,^[/-][wW][dD][0-9]+$>")
endfunction()
