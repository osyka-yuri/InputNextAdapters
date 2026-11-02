include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/NativeDependencies.cmake")
inputnext_native_dependency_patch_ids(fallout4 commonlibf4 _commonlibf4_patch_ids)
list(LENGTH _commonlibf4_patch_ids _commonlibf4_patch_count)
if(NOT _commonlibf4_patch_count EQUAL 1)
    message(FATAL_ERROR "The CommonLibF4 dependency must declare exactly one repository-owned amendment.")
endif()
list(GET _commonlibf4_patch_ids 0 _commonlibf4_patch_id)
inputnext_native_dependency_patch_metadata(
    fallout4 commonlibf4 "${_commonlibf4_patch_id}" _commonlibf4_patch)
set(INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_ID "${_commonlibf4_patch_ID}")
set(INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_PATH "${_commonlibf4_patch_PATH}")
set(INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_INPUT_LF "${_commonlibf4_patch_INPUT_lf}")
set(INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_INPUT_CRLF "${_commonlibf4_patch_INPUT_crlf}")
set(INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_OUTPUT_LF "${_commonlibf4_patch_OUTPUT_lf}")
set(INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_OUTPUT_CRLF "${_commonlibf4_patch_OUTPUT_crlf}")
set(INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_OUTPUT_HASHES
    "${INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_OUTPUT_LF},${INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_OUTPUT_CRLF}")

function(fallout4_patch_commonlibf4 source_dir)
    set(_header "${source_dir}/${INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_PATH}")
    if(NOT EXISTS "${_header}")
        message(FATAL_ERROR
            "The pinned CommonLibF4 animation manager header is missing: ${_header}")
    endif()

    file(SHA256 "${_header}" _raw_hash)
    if(_raw_hash STREQUAL "${INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_OUTPUT_LF}" OR
       _raw_hash STREQUAL "${INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_OUTPUT_CRLF}")
        set(INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_SHA256 "${_raw_hash}" PARENT_SCOPE)
        return()
    elseif(_raw_hash STREQUAL "${INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_INPUT_LF}")
        set(_newline_style LF)
    elseif(_raw_hash STREQUAL "${INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_INPUT_CRLF}")
        set(_newline_style CRLF)
    else()
        message(FATAL_ERROR
            "The CommonLibF4 animation manager header has an unexpected SHA256 "
            "(${_raw_hash}); refusing to modify ${_header}.")
    endif()

    file(READ "${_header}" _header_contents)
    string(REPLACE "\r\n" "\n" _normalized_contents "${_header_contents}")
    set(_original_anchor "\n\tpublic:\n\t\tclass DependentManagerSmartPtr")
    set(_patched_anchor "\n\tpublic:\n\t\t~BSAnimationGraphManager() override;\n\n\t\tclass DependentManagerSmartPtr")
    string(REPLACE "${_original_anchor}" "${_patched_anchor}" _patched_contents "${_normalized_contents}")
    if(_patched_contents STREQUAL _normalized_contents)
        message(FATAL_ERROR
            "The verified CommonLibF4 header did not contain the expected destructor insertion point: ${_header}")
    endif()

    string(SHA256 _patched_hash "${_patched_contents}")
    if(NOT _patched_hash STREQUAL "${INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_OUTPUT_LF}")
        message(FATAL_ERROR
            "The CommonLibF4 animation manager patch produced an unexpected SHA256 "
            "(${_patched_hash}); refusing to modify ${_header}.")
    endif()

    file(CONFIGURE
        OUTPUT "${_header}"
        CONTENT "${_patched_contents}"
        @ONLY
        NEWLINE_STYLE "${_newline_style}"
    )
    set(INPUTNEXT_FALLOUT4_COMMONLIBF4_PATCH_SHA256 "${_patched_hash}" PARENT_SCOPE)
endfunction()
