if(NOT DEFINED OUTPUT_FILE OR OUTPUT_FILE STREQUAL "")
    message(FATAL_ERROR "OUTPUT_FILE must name the generated built-in bank header.")
endif()

function(_inputnext_append_builtin_bank output_variable symbol input_file)
    if(NOT EXISTS "${input_file}" OR IS_DIRECTORY "${input_file}")
        message(FATAL_ERROR "Built-in audio haptic bank is missing: '${input_file}'.")
    endif()

    # Keep this aligned with the bank parser's 24-byte header and 64 KiB payload limit.
    set(_maximum_bank_size 65560)
    file(SIZE "${input_file}" _bank_size)
    if(_bank_size LESS 24 OR _bank_size GREATER _maximum_bank_size)
        message(FATAL_ERROR "Built-in audio haptic bank is outside the supported size: '${input_file}'.")
    endif()
    file(READ "${input_file}" _bank_hex HEX)
    math(EXPR _hex_length "${_bank_size} * 2")

    set(_bank_source "inline constexpr std::array<unsigned char, ${_bank_size}> ${symbol}_bank{\n")
    set(_line "")
    set(_column 0)
    set(_position 0)
    while(_position LESS _hex_length)
        string(SUBSTRING "${_bank_hex}" ${_position} 2 _byte)
        if(_column GREATER 0)
            string(APPEND _line " ")
        endif()
        string(APPEND _line "0x${_byte},")
        math(EXPR _position "${_position} + 2")
        math(EXPR _column "${_column} + 1")
        if(_column EQUAL 12 OR _position GREATER_EQUAL _hex_length)
            string(APPEND _bank_source "    ${_line}\n")
            set(_line "")
            set(_column 0)
        endif()
    endwhile()
    string(APPEND _bank_source "};\n")
    set(${output_variable} "${_bank_source}" PARENT_SCOPE)
endfunction()

foreach(_required INPUT_FALLOUT4)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} must name a source audio haptic bank.")
    endif()
endforeach()

_inputnext_append_builtin_bank(_fallout4_source fallout4 "${INPUT_FALLOUT4}")

string(CONCAT _generated_source
    "// Generated from the checked-in audio haptic banks. Do not edit.\n"
    "#pragma once\n\n"
    "#include <array>\n"
    "\n"
    "namespace inputnext::adapters::detail {\n"
    "${_fallout4_source}"
    "} // namespace inputnext::adapters::detail\n"
)
get_filename_component(_output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_directory}")
set(_temporary_file "${OUTPUT_FILE}.tmp")
file(WRITE "${_temporary_file}" "${_generated_source}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_temporary_file}" "${OUTPUT_FILE}"
    RESULT_VARIABLE _copy_result
    ERROR_VARIABLE _copy_error
)
file(REMOVE "${_temporary_file}")
if(NOT _copy_result EQUAL 0)
    message(FATAL_ERROR "Could not write generated built-in audio haptic banks: ${_copy_error}")
endif()
