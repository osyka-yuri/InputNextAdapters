if(NOT DEFINED product_name OR product_name STREQUAL "")
    set(product_name "Windows plugin")
endif()
if(NOT DEFINED package_archive OR NOT EXISTS "${package_archive}")
    message(FATAL_ERROR "The ${product_name} install archive does not exist.")
endif()
if(NOT DEFINED verify_directory OR verify_directory STREQUAL "")
    message(FATAL_ERROR "The ${product_name} package audit directory was not configured.")
endif()
if(NOT DEFINED expected_entries OR expected_entries STREQUAL "")
    message(FATAL_ERROR "The expected ${product_name} archive manifest was not configured.")
endif()
if(NOT DEFINED expected_dll_entry OR NOT DEFINED expected_dll OR NOT EXISTS "${expected_dll}")
    message(FATAL_ERROR "The expected ${product_name} plugin image was not configured.")
endif()
if(IS_ABSOLUTE "${expected_dll_entry}" OR "${expected_dll_entry}" MATCHES "(^|/)\\.\\.(/|$)")
    message(FATAL_ERROR "The ${product_name} archive plugin path must be relative and contained.")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${package_archive}"
    RESULT_VARIABLE list_result
    OUTPUT_VARIABLE archive_files
    ERROR_VARIABLE list_error
)
if(NOT list_result EQUAL 0)
    message(FATAL_ERROR "Could not list the ${product_name} archive: ${list_error}")
endif()

string(REPLACE "\r\n" "\n" archive_files "${archive_files}")
string(REGEX REPLACE "\n+$" "" archive_files "${archive_files}")
string(REPLACE "\n" ";" actual_entries "${archive_files}")
list(REMOVE_ITEM actual_entries "")
set(expected_directories)
foreach(file IN LISTS expected_entries)
    get_filename_component(parent "${file}" DIRECTORY)
    while(NOT parent STREQUAL "")
        list(APPEND expected_directories "${parent}")
        get_filename_component(parent "${parent}" DIRECTORY)
    endwhile()
endforeach()
list(REMOVE_DUPLICATES expected_directories)
set(file_entries)
foreach(entry IN LISTS actual_entries)
    if(entry MATCHES "/$")
        string(REGEX REPLACE "/+$" "" directory "${entry}")
        if(NOT directory IN_LIST expected_directories)
            message(FATAL_ERROR
                "Unexpected directory '${entry}' in the ${product_name} archive.")
        endif()
    else()
        list(APPEND file_entries "${entry}")
    endif()
endforeach()
list(SORT file_entries)
list(SORT expected_entries)
if(NOT file_entries STREQUAL expected_entries)
    message(FATAL_ERROR
        "Expected exactly '${expected_entries}' in the ${product_name} archive, found '${file_entries}'.")
endif()

file(MAKE_DIRECTORY "${verify_directory}")
file(REAL_PATH "${verify_directory}" verify_root)
string(RANDOM LENGTH 16 audit_token)
set(extract_directory "${verify_root}/.zip-audit-${audit_token}")
if(EXISTS "${extract_directory}" OR IS_SYMLINK "${extract_directory}")
    message(FATAL_ERROR "The ${product_name} package audit path is already in use.")
endif()
file(MAKE_DIRECTORY "${extract_directory}")
file(ARCHIVE_EXTRACT
    INPUT "${package_archive}"
    DESTINATION "${extract_directory}"
    PATTERNS "${expected_dll_entry}"
)
set(archived_dll "${extract_directory}/${expected_dll_entry}")
if(NOT EXISTS "${archived_dll}")
    message(FATAL_ERROR
        "The ${product_name} archive is missing its plugin image '${expected_dll_entry}'.")
endif()
if(IS_SYMLINK "${archived_dll}")
    message(FATAL_ERROR "The ${product_name} archive plugin image must be a regular file.")
endif()

file(SHA256 "${expected_dll}" expected_dll_sha256)
file(SHA256 "${archived_dll}" archived_dll_sha256)
if(NOT archived_dll_sha256 STREQUAL expected_dll_sha256)
    message(FATAL_ERROR
        "The ${product_name} archive plugin does not match the audited build image "
        "(expected ${expected_dll_sha256}, found ${archived_dll_sha256}).")
endif()
file(REAL_PATH "${extract_directory}" resolved_extract_directory)
if(resolved_extract_directory STREQUAL extract_directory)
    file(REMOVE_RECURSE "${extract_directory}")
endif()
