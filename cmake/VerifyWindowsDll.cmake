if(NOT DEFINED product_name OR product_name STREQUAL "")
    set(product_name "Windows plugin")
endif()
if(NOT DEFINED image OR NOT EXISTS "${image}")
    message(FATAL_ERROR "The ${product_name} image does not exist.")
endif()
if(NOT DEFINED dumpbin OR NOT EXISTS "${dumpbin}")
    message(FATAL_ERROR "The Visual C++ dumpbin tool could not be found.")
endif()
if(NOT DEFINED expected_exports OR expected_exports STREQUAL "")
    message(FATAL_ERROR "The expected exports for ${product_name} were not configured.")
endif()
string(REPLACE "," ";" expected_exports "${expected_exports}")

execute_process(
    COMMAND "${dumpbin}" /nologo /exports "${image}"
    RESULT_VARIABLE dumpbin_result
    OUTPUT_VARIABLE dumpbin_output
    ERROR_VARIABLE dumpbin_error
)
if(NOT "${dumpbin_result}" STREQUAL "0")
    message(FATAL_ERROR "Could not read the ${product_name} exports: ${dumpbin_error}")
endif()

string(REPLACE "\r\n" "\n" dumpbin_output "${dumpbin_output}")
string(REPLACE "\n" ";" export_lines "${dumpbin_output}")
set(actual_exports)
foreach(line IN LISTS export_lines)
    if(NOT line MATCHES "^[ \t]*([0-9]+)[ \t]+[A-Fa-f0-9]+([ \t]+(.*))?$")
        continue()
    endif()
    set(export_ordinal "${CMAKE_MATCH_1}")
    string(STRIP "${CMAKE_MATCH_3}" export_row)
    # A named, non-forwarded export has both a hint and an RVA before its name.
    if(export_row MATCHES "^[A-Fa-f0-9]+[ \t]+(.+)$")
        set(export_name "${CMAKE_MATCH_1}")
        string(STRIP "${export_name}" export_name)
        # Incremental links annotate the internal x86 symbol with a linker thunk.
        string(REGEX REPLACE "@ILT\\+[0-9]+\\((_[A-Za-z_][A-Za-z_0-9]*)\\)$" "\\1"
            export_name "${export_name}")
        if(export_name MATCHES "^([A-Za-z_][A-Za-z_0-9]*)[ \t]+=[ \t]+_([A-Za-z_][A-Za-z_0-9]*)$"
            AND CMAKE_MATCH_1 STREQUAL CMAKE_MATCH_2)
            set(export_name "${CMAKE_MATCH_1}")
        endif()
    else()
        set(export_name "<ordinal ${export_ordinal}> ${export_row}")
    endif()
    list(APPEND actual_exports "${export_name}")
endforeach()
list(SORT actual_exports)
list(SORT expected_exports)
if(NOT actual_exports STREQUAL expected_exports)
    message(FATAL_ERROR
        "Expected exactly '${expected_exports}' exported by ${product_name}, found '${actual_exports}'.")
endif()

execute_process(
    COMMAND "${dumpbin}" /nologo /dependents "${image}"
    RESULT_VARIABLE dependents_result
    OUTPUT_VARIABLE dependents_output
    ERROR_VARIABLE dependents_error
)
if(NOT "${dependents_result}" STREQUAL "0")
    message(FATAL_ERROR
        "Could not read the ${product_name} dependencies: ${dependents_error}")
endif()

string(REPLACE "\r\n" "\n" dependents_output "${dependents_output}")
string(REPLACE "\n" ";" dependency_lines "${dependents_output}")
foreach(line IN LISTS dependency_lines)
    string(STRIP "${line}" dependency)
    string(TOLOWER "${dependency}" dependency_lower)
    if(dependency_lower MATCHES
            "^(api-ms-win-crt-[a-z0-9-]+|msvcp([0-9]+([_][a-z0-9]+)*d?|_win)|msvcr[0-9]+d?|msvcrt(d)?|ucrtbase[d]?|vcruntime[0-9]+([_][a-z0-9]+)*d?)\\.dll$")
        message(FATAL_ERROR
            "${product_name} must not depend on dynamic MSVC or UCRT libraries:\n${dependency}")
    endif()
endforeach()
