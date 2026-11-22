if(NOT DEFINED cpack OR NOT EXISTS "${cpack}")
    message(FATAL_ERROR "The CPack executable could not be found.")
endif()
if(NOT DEFINED cpack_config OR NOT EXISTS "${cpack_config}")
    message(FATAL_ERROR "The CPack configuration does not exist.")
endif()
if(NOT DEFINED package_directory OR package_directory STREQUAL "")
    message(FATAL_ERROR "The package test output directory was not configured.")
endif()
if(NOT DEFINED package_filename OR package_filename STREQUAL "")
    message(FATAL_ERROR "The package test filename was not configured.")
endif()

file(MAKE_DIRECTORY "${package_directory}")
set(cpack_configuration_arguments)
if(DEFINED package_configuration AND NOT package_configuration STREQUAL "")
    list(APPEND cpack_configuration_arguments -C "${package_configuration}")
endif()
execute_process(
    COMMAND "${cpack}" --config "${cpack_config}" ${cpack_configuration_arguments} -B "${package_directory}"
    RESULT_VARIABLE cpack_result
    OUTPUT_VARIABLE cpack_output
    ERROR_VARIABLE cpack_error
)
if(NOT cpack_result EQUAL 0)
    message(FATAL_ERROR "CPack could not create the audited package:\n${cpack_output}${cpack_error}")
endif()

if(NOT EXISTS "${package_directory}/${package_filename}")
    message(FATAL_ERROR "The audited package was not copied to '${package_directory}'.")
endif()
