set(_INPUTNEXT_QUALITY_RUNNER "${PROJECT_SOURCE_DIR}/tools/check_quality.ps1")

find_program(INPUTNEXT_PWSH NAMES pwsh.exe pwsh DOC "PowerShell 7 executable for local quality checks")
if(NOT INPUTNEXT_PWSH)
    set(INPUTNEXT_PWSH "pwsh.exe")
endif()

function(inputnext_add_code_quality_target target_name)
    add_custom_target(${target_name}
        COMMAND "${INPUTNEXT_PWSH}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File "${_INPUTNEXT_QUALITY_RUNNER}"
            ${ARGN}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        USES_TERMINAL
        VERBATIM
    )
endfunction()

inputnext_add_code_quality_target(inputnext_adapters_format_check)
inputnext_add_code_quality_target(inputnext_adapters_format -ApplyFormat)
