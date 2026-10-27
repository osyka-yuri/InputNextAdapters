function(inputnext_verify_sdk_header)
    set(manifest "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inputnext/manifest.json")
    if(NOT EXISTS "${manifest}")
        message(FATAL_ERROR "Missing InputNext SDK header manifest: ${manifest}")
    endif()

    file(READ "${manifest}" manifest_json)
    string(JSON manifest_type ERROR_VARIABLE json_error TYPE "${manifest_json}")
    if(NOT json_error STREQUAL "NOTFOUND" OR NOT manifest_type STREQUAL "OBJECT")
        message(FATAL_ERROR "The InputNext SDK header manifest must be a valid JSON object.")
    endif()

    string(JSON headers_type ERROR_VARIABLE field_error TYPE "${manifest_json}" headers)
    if(NOT field_error STREQUAL "NOTFOUND" OR NOT headers_type STREQUAL "ARRAY")
        message(FATAL_ERROR "The InputNext SDK header manifest must contain a header array.")
    endif()
    string(JSON header_count ERROR_VARIABLE field_error LENGTH "${manifest_json}" headers)
    if(NOT field_error STREQUAL "NOTFOUND" OR NOT header_count EQUAL 2)
        message(FATAL_ERROR "The InputNext SDK pin must contain exactly the Core and Effects headers.")
    endif()

    set(expected_upstream_paths
        "sdk/input_next_core.h"
        "sdk/input_next_effects.h"
    )
    set(expected_vendored_paths
        "third_party/inputnext/include/input_next_core.h"
        "third_party/inputnext/include/input_next_effects.h"
    )
    foreach(index RANGE 0 1)
        string(JSON header_entry_type ERROR_VARIABLE field_error TYPE "${manifest_json}" headers ${index})
        if(NOT field_error STREQUAL "NOTFOUND" OR NOT header_entry_type STREQUAL "OBJECT")
            message(FATAL_ERROR "InputNext SDK header entry ${index} must be a JSON object.")
        endif()
        string(JSON header_field_count ERROR_VARIABLE field_error LENGTH "${manifest_json}" headers ${index})
        if(NOT field_error STREQUAL "NOTFOUND" OR NOT header_field_count EQUAL 3)
            message(FATAL_ERROR "InputNext SDK header entry ${index} must contain only path, vendored_path, and sha256.")
        endif()
        foreach(field IN ITEMS path vendored_path sha256)
            string(JSON field_type ERROR_VARIABLE field_error TYPE "${manifest_json}" headers ${index} ${field})
            if(NOT field_error STREQUAL "NOTFOUND" OR NOT field_type STREQUAL "STRING")
                message(FATAL_ERROR "InputNext SDK header entry ${index} must contain string field '${field}'.")
            endif()
            string(JSON field_value ERROR_VARIABLE field_error GET "${manifest_json}" headers ${index} ${field})
            if(NOT field_error STREQUAL "NOTFOUND")
                message(FATAL_ERROR "Could not read InputNext SDK header field '${field}' at entry ${index}.")
            endif()

            if(field STREQUAL "path")
                set(upstream_path "${field_value}")
            elseif(field STREQUAL "vendored_path")
                set(vendored_path "${field_value}")
            else()
                set(header_hash "${field_value}")
            endif()
        endforeach()

        list(GET expected_upstream_paths ${index} required_upstream_path)
        list(GET expected_vendored_paths ${index} required_vendored_path)
        if(NOT upstream_path STREQUAL required_upstream_path OR
           NOT vendored_path STREQUAL required_vendored_path)
            message(FATAL_ERROR "The InputNext SDK pin contains an unexpected upstream or vendored header path.")
        endif()
        string(LENGTH "${header_hash}" header_hash_length)
        if(NOT header_hash_length EQUAL 64 OR
           NOT header_hash MATCHES "^[0-9a-f]+$")
            message(FATAL_ERROR "The InputNext SDK pin must contain lower-case SHA-256 header hashes.")
        endif()

        set(header "${CMAKE_CURRENT_SOURCE_DIR}/${required_vendored_path}")
        if(NOT EXISTS "${header}")
            message(FATAL_ERROR "Missing pinned InputNext SDK header: ${header}")
        endif()

        file(SHA256 "${header}" actual_hash)
        string(TOLOWER "${actual_hash}" actual_hash)
        if(NOT actual_hash STREQUAL header_hash)
            message(FATAL_ERROR
                "The pinned InputNext SDK header hash does not match the header manifest. "
                "Expected ${header_hash}, got ${actual_hash}.")
        endif()
    endforeach()
endfunction()
