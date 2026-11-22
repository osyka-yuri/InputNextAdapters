cmake_minimum_required(VERSION 4.4.4)

if(NOT IS_ABSOLUTE "${package_archive}" OR NOT IS_DIRECTORY "${install_build_dir}")
    message(FATAL_ERROR "The Fallout 3 package command requires build and archive paths.")
endif()

set(product_name "Fallout 3 plugin")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/VerifyPe32Image.cmake")
set(expected_exports "FOSEPlugin_Load;FOSEPlugin_Query")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/VerifyWindowsDll.cmake")

get_filename_component(package_root "${package_archive}" DIRECTORY)
get_filename_component(package_name "${package_archive}" NAME)
file(MAKE_DIRECTORY "${package_root}")
file(REAL_PATH "${package_root}" package_root)
set(release_archive "${package_root}/${package_name}")
string(RANDOM LENGTH 16 package_token)
set(package_stage "${package_root}/.fallout3-package-${package_token}")
if(EXISTS "${package_stage}" OR IS_SYMLINK "${package_stage}")
    message(FATAL_ERROR "The Fallout 3 package staging path is already in use.")
endif()
file(MAKE_DIRECTORY "${package_stage}")
if(configuration)
    set(install_configuration --config "${configuration}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${install_build_dir}"
        --prefix "${package_stage}" --component Fallout3Adapter
        ${install_configuration}
    COMMAND_ERROR_IS_FATAL ANY
)

set(expected_dll "${image}")
set(expected_dll_entry "Data/FOSE/Plugins/InputNextFallout3.dll")
set(expected_entries "${expected_dll_entry};LICENSE;README.md")
set(package_archive "${package_stage}/${package_name}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${package_archive}" --format=zip -- ${expected_entries}
    WORKING_DIRECTORY "${package_stage}"
    COMMAND_ERROR_IS_FATAL ANY
)

set(verify_directory "${package_stage}/.package-audit")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/VerifyZipPackage.cmake")
file(RENAME "${package_archive}" "${release_archive}")

file(REAL_PATH "${package_stage}" resolved_stage)
if(resolved_stage STREQUAL package_stage)
    file(REMOVE_RECURSE "${package_stage}")
endif()
