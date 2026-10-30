include_guard(GLOBAL)
include("${INPUTNEXTADAPTERS_SOURCE_DIR}/tests/cmake/VerifyConsumerLogic.cmake")
file(TO_CMAKE_PATH "${sandbox}" sandbox)

function(_expect_prefix_guard_failure name call expected_message)
    file(TO_CMAKE_PATH "${INPUTNEXTADAPTERS_SOURCE_DIR}" TEST_SOURCE_DIRECTORY)
    set(PREFIX_GUARD_CALL "${call}")
    set(script_path "${sandbox}/${name}.cmake")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/PrefixGuard.cmake.in"
        "${script_path}"
        @ONLY
    )
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -P "${script_path}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(result EQUAL 0)
        message(FATAL_ERROR "Consumer prefix safety regression '${name}' unexpectedly succeeded.")
    endif()
    if(NOT expected_message STREQUAL "" AND
       NOT "${output}\n${error}" MATCHES "${expected_message}")
        message(FATAL_ERROR "Consumer prefix safety regression '${name}' failed for the wrong reason: ${output}${error}")
    endif()
endfunction()

message(STATUS "[RegressionTest] Checking consumer package prefix freshness and cleanup boundaries...")
set(prefix_test_binary "${sandbox}/prefix-safety/binary")
file(MAKE_DIRECTORY "${prefix_test_binary}")
inputnext_prepare_consumer_directory(
    "${prefix_test_binary}" "installed" "common_effects"
    prefix_binary prefix_integration prefix_consumer
)
set(missing_derived_path "${prefix_consumer}/not-created-yet/deeper")
_inputnext_real_path("${missing_derived_path}" resolved_missing_derived_path)
file(TO_CMAKE_PATH "${missing_derived_path}" expected_missing_derived_path)
if(NOT resolved_missing_derived_path STREQUAL expected_missing_derived_path)
    message(FATAL_ERROR "Resolving a missing derived path produced '${resolved_missing_derived_path}', expected '${expected_missing_derived_path}'.")
endif()

set(package_source "${prefix_consumer}/fixture-package")
set(relocated_prefix "${prefix_consumer}/relocated-prefix")
set(required_header "include/inputnext_adapters/sdk/input_next_effects.h")
file(MAKE_DIRECTORY "${package_source}/include/inputnext_adapters/sdk")
file(MAKE_DIRECTORY "${relocated_prefix}/include/inputnext_adapters/sdk")
file(WRITE "${package_source}/${required_header}" "current package header")
file(WRITE "${relocated_prefix}/${required_header}" "stale header")
file(WRITE "${relocated_prefix}/include/obsolete-header.h" "stale file")
inputnext_relocate_install_prefix(
    "${package_source}" "${relocated_prefix}" "${prefix_binary}" "${prefix_integration}" "${prefix_consumer}"
)
if(NOT EXISTS "${relocated_prefix}/${required_header}" OR
   EXISTS "${relocated_prefix}/include/obsolete-header.h")
    message(FATAL_ERROR "Relocation retained stale package files or lost the current required header.")
endif()
file(READ "${relocated_prefix}/${required_header}" relocated_header_contents)
if(NOT relocated_header_contents STREQUAL "current package header")
    message(FATAL_ERROR "Relocation did not copy the current package header contents.")
endif()
file(READ "${package_source}/${required_header}" external_header_contents)
if(NOT external_header_contents STREQUAL "current package header")
    message(FATAL_ERROR "Relocation modified the provided install-prefix source.")
endif()

file(REMOVE "${package_source}/${required_header}")
file(WRITE "${relocated_prefix}/${required_header}" "prior artifact that must not survive")
inputnext_relocate_install_prefix(
    "${package_source}" "${relocated_prefix}" "${prefix_binary}" "${prefix_integration}" "${prefix_consumer}"
)
if(EXISTS "${relocated_prefix}/${required_header}")
    message(FATAL_ERROR "A header missing from the current package remained available from a prior relocated prefix.")
endif()

set(owned_install_prefix "${prefix_consumer}/install-prefix")
file(MAKE_DIRECTORY "${owned_install_prefix}")
file(WRITE "${owned_install_prefix}/stale-package-file" "old install")
inputnext_remove_owned_package_prefix(
    "${owned_install_prefix}" "${prefix_binary}" "${prefix_integration}" "${prefix_consumer}"
)
if(EXISTS "${owned_install_prefix}")
    message(FATAL_ERROR "The auto-install prefix was not reset before installation.")
endif()
inputnext_assert_removal_preserves_path("${owned_install_prefix}" "${prefix_binary}" "test package installation cleanup")

file(MAKE_DIRECTORY "${relocated_prefix}")
file(WRITE "${relocated_prefix}/preserve.txt" "overlap guard")
_expect_prefix_guard_failure(
    "overlapping-package-source-and-destination"
    "inputnext_relocate_install_prefix(\"${prefix_binary}\" \"${relocated_prefix}\" \"${prefix_binary}\" \"${prefix_integration}\" \"${prefix_consumer}\")"
    "overlap"
)
if(NOT EXISTS "${relocated_prefix}/preserve.txt")
    message(FATAL_ERROR "The overlapping source/destination guard modified its source tree.")
endif()

set(outside_root "${sandbox}/prefix-safety/outside")
set(outside_prefix "${outside_root}/relocated-prefix")
file(MAKE_DIRECTORY "${outside_prefix}")
file(WRITE "${outside_prefix}/preserve.txt" "outside sentinel")
_expect_prefix_guard_failure(
    "outside-package-prefix-is-rejected"
    "inputnext_remove_owned_package_prefix(\"${outside_prefix}\" \"${prefix_binary}\" \"${prefix_integration}\" \"${prefix_consumer}\")"
    "non-owned package prefix"
)
if(NOT EXISTS "${outside_prefix}/preserve.txt")
    message(FATAL_ERROR "An outside package prefix was modified by the failed cleanup guard.")
endif()

set(symlink_prefix "${prefix_consumer}/relocated-prefix")
inputnext_remove_owned_package_prefix(
    "${symlink_prefix}" "${prefix_binary}" "${prefix_integration}" "${prefix_consumer}"
)
file(CREATE_LINK "${outside_root}" "${symlink_prefix}" SYMBOLIC RESULT symlink_result)
if(symlink_result STREQUAL "0" AND IS_SYMLINK "${symlink_prefix}")
    _expect_prefix_guard_failure(
        "symbolic-package-prefix-is-rejected"
        "inputnext_remove_owned_package_prefix(\"${symlink_prefix}\" \"${prefix_binary}\" \"${prefix_integration}\" \"${prefix_consumer}\")"
        "resolved path escapes the validated consumer root|symbolic-link"
    )
    if(NOT EXISTS "${outside_prefix}/preserve.txt")
        message(FATAL_ERROR "A symbolic package prefix redirected cleanup into its target.")
    endif()
    file(REMOVE "${symlink_prefix}")
else()
    message(WARNING "Could not create a directory symlink for the cleanup regression: ${symlink_result}")
endif()

set(dangling_link "${prefix_consumer}/dangling-package-link")
set(dangling_target "${outside_root}/missing-package-target")
file(CREATE_LINK "${dangling_target}" "${dangling_link}" SYMBOLIC RESULT dangling_link_result)
if(dangling_link_result STREQUAL "0" AND IS_SYMLINK "${dangling_link}")
    _expect_prefix_guard_failure(
        "dangling-package-parent-fails-closed"
        "inputnext_remove_owned_package_prefix(\"${dangling_link}/relocated-prefix\" \"${prefix_binary}\" \"${prefix_integration}\" \"${prefix_consumer}\")"
        "dangling symbolic link"
    )
    if(NOT IS_SYMLINK "${dangling_link}" OR EXISTS "${dangling_target}" OR
       NOT EXISTS "${outside_prefix}/preserve.txt")
        message(FATAL_ERROR "A dangling-link cleanup refusal changed a link target or outside sentinel.")
    endif()
    file(REMOVE "${dangling_link}")
else()
    message(WARNING "Could not create a dangling directory symlink for the cleanup regression: ${dangling_link_result}")
endif()
