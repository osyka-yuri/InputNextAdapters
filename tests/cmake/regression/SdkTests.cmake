if(NOT DEFINED INPUTNEXTADAPTERS_SOURCE_DIR OR INPUTNEXTADAPTERS_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "SdkTests.cmake requires INPUTNEXTADAPTERS_SOURCE_DIR.")
endif()
if(NOT DEFINED INPUTNEXTADAPTERS_BINARY_DIR OR INPUTNEXTADAPTERS_BINARY_DIR STREQUAL "")
    message(FATAL_ERROR "SdkTests.cmake requires INPUTNEXTADAPTERS_BINARY_DIR.")
endif()
if(NOT DEFINED sandbox OR NOT IS_DIRECTORY "${sandbox}")
    message(FATAL_ERROR "SdkTests.cmake requires an existing validated sandbox directory.")
endif()

include("${INPUTNEXTADAPTERS_SOURCE_DIR}/adapters/skyrim_le/cmake/SkseSdkState.cmake")

message(STATUS "[RegressionTest] Checking SDK manifest integrity...")

set(sdk_fixture "${sandbox}/test_sdk_validation")
_skyrim_le_get_expected_subpaths(all_expected_subpaths)
foreach(subpath IN LISTS all_expected_subpaths)
    get_filename_component(fdir "${sdk_fixture}/${subpath}" DIRECTORY)
    file(MAKE_DIRECTORY "${fdir}")
    file(WRITE "${sdk_fixture}/${subpath}" "// file content for ${subpath}")
endforeach()

_skyrim_le_compute_manifest_digest("${sdk_fixture}" initial_digest)
if(initial_digest STREQUAL "")
    message(FATAL_ERROR "Regression: initial manifest digest was empty with all expected files present.")
endif()

set(test_fp "TEST_FINGERPRINT_12345")
file(WRITE "${sdk_fixture}/.prepared_state" "${test_fp}\n${initial_digest}\n")

_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid)
if(NOT is_valid)
    message(FATAL_ERROR "Regression: SDK tree validation failed on pristine fixture.")
endif()

# Deleting a required file invalidates the SDK manifest.
file(REMOVE "${sdk_fixture}/src/skse/skse/GameAPI.cpp")
_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid_after_delete)
if(is_valid_after_delete)
    message(FATAL_ERROR "Regression: SDK tree validation failed to detect deleted GameAPI.cpp.")
endif()

# Modifying a required file invalidates the SDK manifest.
file(WRITE "${sdk_fixture}/src/skse/skse/GameAPI.cpp" "// tampered content")
_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid_after_tamper)
if(is_valid_after_tamper)
    message(FATAL_ERROR "Regression: SDK tree validation failed to detect modified content in GameAPI.cpp.")
endif()

# Restoring original content restores manifest validity.
file(WRITE "${sdk_fixture}/src/skse/skse/GameAPI.cpp" "// file content for src/skse/skse/GameAPI.cpp")
_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid_restored)
if(NOT is_valid_restored)
    message(FATAL_ERROR "Regression: SDK tree validation failed after restoring GameAPI.cpp.")
endif()

set(common_types_header "${sdk_fixture}/src/common/ITypes.h")
set(extra_common_header "${sdk_fixture}/src/common/ExtraFixture.h")

# An unlisted source file changes the whole-tree digest.
file(WRITE "${extra_common_header}" "// additional common header")
_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid_after_addition)
if(is_valid_after_addition)
    message(FATAL_ERROR "Regression: adding an unlisted common header did not invalidate the SDK manifest.")
endif()
file(REMOVE "${extra_common_header}")
_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid_after_addition_removed)
if(NOT is_valid_after_addition_removed)
    message(FATAL_ERROR "Regression: removing an added common header did not restore manifest validity.")
endif()

# The required common header is covered by both the layout check and full-tree digest.
file(WRITE "${common_types_header}" "// tampered common types header")
_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid_after_common_tamper)
if(is_valid_after_common_tamper)
    message(FATAL_ERROR "Regression: tampering with common/ITypes.h did not invalidate the SDK manifest.")
endif()
file(WRITE "${common_types_header}" "// file content for src/common/ITypes.h")
_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid_after_common_restore)
if(NOT is_valid_after_common_restore)
    message(FATAL_ERROR "Regression: restoring common/ITypes.h did not restore manifest validity.")
endif()

file(RENAME "${common_types_header}" "${common_types_header}.renamed")
_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid_after_common_rename)
if(is_valid_after_common_rename)
    message(FATAL_ERROR "Regression: renaming required common/ITypes.h did not invalidate the SDK manifest.")
endif()
file(RENAME "${common_types_header}.renamed" "${common_types_header}")
_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid_after_common_rename_restore)
if(NOT is_valid_after_common_rename_restore)
    message(FATAL_ERROR "Regression: restoring common/ITypes.h after rename did not restore manifest validity.")
endif()

file(REMOVE "${common_types_header}")
_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid_after_common_delete)
if(is_valid_after_common_delete)
    message(FATAL_ERROR "Regression: deleting required common/ITypes.h did not invalidate the SDK manifest.")
endif()
file(WRITE "${common_types_header}" "// file content for src/common/ITypes.h")
_skyrim_le_validate_sdk_tree("${sdk_fixture}" "${test_fp}" is_valid_after_common_delete_restore)
if(NOT is_valid_after_common_delete_restore)
    message(FATAL_ERROR "Regression: restoring common/ITypes.h after deletion did not restore manifest validity.")
endif()

message(STATUS "[RegressionTest] Checking external tree fingerprint sensitivity...")

set(ext_tree_1 "${sandbox}/ext_tree_1")
set(ext_tree_2 "${sandbox}/ext_tree_2")

# Create two trees with identical content but different relative paths
file(MAKE_DIRECTORY "${ext_tree_1}/src/dirA")
file(MAKE_DIRECTORY "${ext_tree_1}/src/dirB")
file(WRITE "${ext_tree_1}/src/dirA/file.cpp" "SAME CONTENT")
file(WRITE "${ext_tree_1}/src/dirB/other.cpp" "OTHER CONTENT")

file(MAKE_DIRECTORY "${ext_tree_2}/src/dirA")
file(MAKE_DIRECTORY "${ext_tree_2}/src/dirB")
file(WRITE "${ext_tree_2}/src/dirA/other.cpp" "OTHER CONTENT")
file(WRITE "${ext_tree_2}/src/dirB/file.cpp" "SAME CONTENT")

_skyrim_le_compute_external_tree_hash("${ext_tree_1}" hash_tree_1)
_skyrim_le_compute_external_tree_hash("${ext_tree_2}" hash_tree_2)

if(hash_tree_1 STREQUAL hash_tree_2)
    message(FATAL_ERROR "Regression: external tree hash failed to distinguish different relative paths.")
endif()

# Renaming a file inside an external tree must alter the hash.
_skyrim_le_compute_external_tree_hash("${ext_tree_1}" hash_before_rename)
file(RENAME "${ext_tree_1}/src/dirA/file.cpp" "${ext_tree_1}/src/dirA/renamed.cpp")
_skyrim_le_compute_external_tree_hash("${ext_tree_1}" hash_after_rename)

if(hash_before_rename STREQUAL hash_after_rename)
    message(FATAL_ERROR "Regression: renaming a file did not alter the external tree hash.")
endif()

message(STATUS "[RegressionTest] Checking SDK state recovery and activation...")

set(active_sdk "${sandbox}/test_state_active")
set(backup_sdk "${sandbox}/test_state_backup")
set(staging_sdk "${sandbox}/test_state_staging")

function(_populate_sdk_fixture dir fp)
    file(REMOVE_RECURSE "${dir}")
    foreach(subpath IN LISTS all_expected_subpaths)
        get_filename_component(fdir "${dir}/${subpath}" DIRECTORY)
        file(MAKE_DIRECTORY "${fdir}")
        file(WRITE "${dir}/${subpath}" "// fixture content: ${subpath}")
    endforeach()
    _skyrim_le_compute_manifest_digest("${dir}" d)
    file(WRITE "${dir}/.prepared_state" "${fp}\n${d}\n")
endfunction()

function(_compute_incomplete_sdk_fixture_digest dir out_digest)
    _skyrim_le_get_expected_subpaths(_incomplete_subpaths)
    list(REMOVE_ITEM _incomplete_subpaths "src/common/ITypes.h")
    set(_incomplete_entries "")
    foreach(_subpath IN LISTS _incomplete_subpaths)
        file(SHA256 "${dir}/${_subpath}" _incomplete_hash)
        string(APPEND _incomplete_entries "${_subpath}:${_incomplete_hash}\n")
    endforeach()
    string(SHA256 _incomplete_digest "${_incomplete_entries}")
    set(${out_digest} "${_incomplete_digest}" PARENT_SCOPE)
endfunction()

# Reject manifests that cover only a subset of the SDK tree.
set(incomplete_active_sdk "${sandbox}/test_incomplete_active")
set(incomplete_backup_sdk "${sandbox}/test_incomplete_backup")
set(incomplete_staging_sdk "${sandbox}/test_incomplete_staging")
set(replacement_fp "REPLACEMENT_FINGERPRINT")
_populate_sdk_fixture("${incomplete_active_sdk}" "${test_fp}")
_compute_incomplete_sdk_fixture_digest("${incomplete_active_sdk}" incomplete_digest)
file(WRITE "${incomplete_active_sdk}/.prepared_state" "${test_fp}\n${incomplete_digest}\n")
_skyrim_le_inspect_sdk_tree("${incomplete_active_sdk}" "${test_fp}" incomplete_active_state)
if(NOT incomplete_active_state STREQUAL "CORRUPTED")
    message(FATAL_ERROR "Regression: incomplete manifest was accepted (${incomplete_active_state}).")
endif()
_skyrim_le_recover_sdk_state("${incomplete_active_sdk}" "${incomplete_backup_sdk}" "${test_fp}" incomplete_needs_prepare)
if(NOT incomplete_needs_prepare OR NOT EXISTS "${incomplete_active_sdk}")
    message(FATAL_ERROR "Regression: active SDK with an incomplete manifest did not request staging recovery while remaining intact.")
endif()

# Restore a valid backup when the active manifest is incomplete.
_populate_sdk_fixture("${incomplete_backup_sdk}" "${test_fp}")
_skyrim_le_recover_sdk_state("${incomplete_active_sdk}" "${incomplete_backup_sdk}" "${test_fp}" incomplete_backup_needs_prepare)
if(incomplete_backup_needs_prepare)
    message(FATAL_ERROR "Regression: recovery did not restore a valid full-tree backup over an SDK with an incomplete manifest.")
endif()
_skyrim_le_validate_sdk_tree("${incomplete_active_sdk}" "${test_fp}" incomplete_backup_restored)
if(NOT incomplete_backup_restored OR EXISTS "${incomplete_backup_sdk}")
    message(FATAL_ERROR "Regression: backup recovery left an invalid active SDK or retained the backup.")
endif()

# Keep a backup with an incomplete manifest until a valid staging tree is ready to replace it.
file(REMOVE_RECURSE "${incomplete_active_sdk}")
file(MAKE_DIRECTORY "${incomplete_active_sdk}")
file(WRITE "${incomplete_active_sdk}/broken.txt" "corrupt active")
_populate_sdk_fixture("${incomplete_backup_sdk}" "${test_fp}")
_compute_incomplete_sdk_fixture_digest("${incomplete_backup_sdk}" incomplete_backup_digest)
file(WRITE "${incomplete_backup_sdk}/.prepared_state" "${test_fp}\n${incomplete_backup_digest}\n")
_skyrim_le_recover_sdk_state("${incomplete_active_sdk}" "${incomplete_backup_sdk}" "${replacement_fp}" incomplete_staging_needed)
if(NOT incomplete_staging_needed OR NOT EXISTS "${incomplete_backup_sdk}")
    message(FATAL_ERROR "Regression: backup with an incomplete manifest was removed before valid staging was available.")
endif()
_populate_sdk_fixture("${incomplete_staging_sdk}" "${replacement_fp}")
_skyrim_le_activate_staging("${incomplete_staging_sdk}" "${incomplete_active_sdk}" "${incomplete_backup_sdk}" "${replacement_fp}")
_skyrim_le_validate_sdk_tree("${incomplete_active_sdk}" "${replacement_fp}" incomplete_staging_active)
if(NOT incomplete_staging_active OR EXISTS "${incomplete_backup_sdk}")
    message(FATAL_ERROR "Regression: staging activation left an invalid active SDK or retained the backup.")
endif()

# Preserve a valid active SDK and discard a corrupted backup.
_populate_sdk_fixture("${active_sdk}" "${test_fp}")
file(REMOVE_RECURSE "${backup_sdk}")
file(MAKE_DIRECTORY "${backup_sdk}")
file(WRITE "${backup_sdk}/junk.txt" "corrupted backup")

_skyrim_le_recover_sdk_state("${active_sdk}" "${backup_sdk}" "${test_fp}" valid_active_needs_prepare)
if(valid_active_needs_prepare)
    message(FATAL_ERROR "Regression: valid active SDK requested preparation unexpectedly.")
endif()
if(EXISTS "${backup_sdk}")
    message(FATAL_ERROR "Regression: corrupted backup was not removed when active SDK was valid.")
endif()
_skyrim_le_validate_sdk_tree("${active_sdk}" "${test_fp}" valid_active_valid)
if(NOT valid_active_valid)
    message(FATAL_ERROR "Regression: valid active SDK became invalid after state recovery.")
endif()

# Restore a corrupted active SDK from a valid backup.
file(REMOVE_RECURSE "${active_sdk}")
file(MAKE_DIRECTORY "${active_sdk}")
file(WRITE "${active_sdk}/broken.txt" "incomplete")
_populate_sdk_fixture("${backup_sdk}" "${test_fp}")

_skyrim_le_recover_sdk_state("${active_sdk}" "${backup_sdk}" "${test_fp}" corrupted_active_needs_prepare)
if(corrupted_active_needs_prepare)
    message(FATAL_ERROR "Regression: valid backup was not restored without requesting preparation.")
endif()
_skyrim_le_validate_sdk_tree("${active_sdk}" "${test_fp}" restored_active_valid)
if(NOT restored_active_valid)
    message(FATAL_ERROR "Regression: active SDK was not restored cleanly from valid backup.")
endif()

# Request staging preparation when both active SDK and backup are corrupted.
file(REMOVE_RECURSE "${active_sdk}" "${backup_sdk}")
file(MAKE_DIRECTORY "${active_sdk}")
file(WRITE "${active_sdk}/broken.txt" "incomplete active")
file(MAKE_DIRECTORY "${backup_sdk}")
file(WRITE "${backup_sdk}/broken.txt" "incomplete backup")

_skyrim_le_recover_sdk_state("${active_sdk}" "${backup_sdk}" "${test_fp}" both_corrupted_needs_prepare)
if(NOT both_corrupted_needs_prepare)
    message(FATAL_ERROR "Regression: corrupted active SDK and backup did not request preparation.")
endif()

# Preserve an outdated active SDK until replacement staging is activated.
set(old_fp "OLD_FINGERPRINT_11111")
set(new_fp "NEW_FINGERPRINT_22222")
_populate_sdk_fixture("${active_sdk}" "${old_fp}")
file(REMOVE_RECURSE "${backup_sdk}")

_skyrim_le_recover_sdk_state("${active_sdk}" "${backup_sdk}" "${new_fp}" outdated_active_needs_prepare)
if(NOT outdated_active_needs_prepare)
    message(FATAL_ERROR "Regression: outdated active SDK did not request preparation.")
endif()
_skyrim_le_validate_sdk_tree("${active_sdk}" "${old_fp}" outdated_active_preserved)
if(NOT outdated_active_preserved)
    message(FATAL_ERROR "Regression: outdated active SDK was damaged before replacement activation.")
endif()

_populate_sdk_fixture("${staging_sdk}" "${new_fp}")
_skyrim_le_activate_staging("${staging_sdk}" "${active_sdk}" "${backup_sdk}" "${new_fp}")
_skyrim_le_validate_sdk_tree("${active_sdk}" "${new_fp}" replacement_active_valid)
if(NOT replacement_active_valid)
    message(FATAL_ERROR "Regression: active SDK was not updated after replacement staging activation.")
endif()

# Reject corrupted staging without modifying the active SDK.
_populate_sdk_fixture("${active_sdk}" "${new_fp}")
file(REMOVE_RECURSE "${staging_sdk}")
file(MAKE_DIRECTORY "${staging_sdk}")
file(WRITE "${staging_sdk}/corrupted_staging.txt" "bad")

set(invalid_staging_script "${sandbox}/test_invalid_staging.cmake")
file(WRITE "${invalid_staging_script}" "
include(\"${INPUTNEXTADAPTERS_SOURCE_DIR}/adapters/skyrim_le/cmake/SkseSdkState.cmake\")
_skyrim_le_activate_staging(\"${staging_sdk}\" \"${active_sdk}\" \"${backup_sdk}\" \"${new_fp}\")
")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -P "${invalid_staging_script}"
    RESULT_VARIABLE invalid_staging_result
    OUTPUT_QUIET
    ERROR_QUIET
)
if(invalid_staging_result EQUAL 0)
    message(FATAL_ERROR "Regression: staging activation succeeded unexpectedly on corrupted staging.")
endif()
_skyrim_le_validate_sdk_tree("${active_sdk}" "${new_fp}" active_preserved_after_rejection)
if(NOT active_preserved_after_rejection)
    message(FATAL_ERROR "Regression: active SDK was damaged when staging activation failed.")
endif()

# Preserve a valid backup across staging preparation and activation failure until replacement succeeds.
file(REMOVE_RECURSE "${active_sdk}" "${backup_sdk}")
file(MAKE_DIRECTORY "${active_sdk}")
file(WRITE "${active_sdk}/corrupted_active.txt" "garbage active")
_populate_sdk_fixture("${backup_sdk}" "${old_fp}")

_skyrim_le_recover_sdk_state("${active_sdk}" "${backup_sdk}" "${new_fp}" outdated_backup_needs_prepare)
if(NOT outdated_backup_needs_prepare)
    message(FATAL_ERROR "Regression: outdated backup with new fingerprint did not request preparation.")
endif()
_skyrim_le_inspect_sdk_tree("${backup_sdk}" "${old_fp}" backup_state_before_activation)
if(NOT backup_state_before_activation STREQUAL "CURRENT_VALID")
    message(FATAL_ERROR "Regression: valid backup was damaged during state recovery.")
endif()

file(REMOVE_RECURSE "${staging_sdk}")
file(MAKE_DIRECTORY "${staging_sdk}")
file(WRITE "${staging_sdk}/bad.txt" "incomplete staging")

set(backup_preservation_failure_script "${sandbox}/test_backup_preservation_failure.cmake")
file(WRITE "${backup_preservation_failure_script}" "
include(\"${INPUTNEXTADAPTERS_SOURCE_DIR}/adapters/skyrim_le/cmake/SkseSdkState.cmake\")
_skyrim_le_activate_staging(\"${staging_sdk}\" \"${active_sdk}\" \"${backup_sdk}\" \"${new_fp}\")
")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -P "${backup_preservation_failure_script}"
    RESULT_VARIABLE failed_activation_result
    OUTPUT_QUIET
    ERROR_QUIET
)
if(failed_activation_result EQUAL 0)
    message(FATAL_ERROR "Regression: staging activation succeeded unexpectedly on corrupted staging.")
endif()

# Outdated valid backup remains intact after activation failure.
_skyrim_le_inspect_sdk_tree("${backup_sdk}" "${old_fp}" backup_state_after_failed_activation)
if(NOT backup_state_after_failed_activation STREQUAL "CURRENT_VALID")
    message(FATAL_ERROR "Regression: valid backup was removed when staging activation failed.")
endif()

# Staging activation with new fingerprint.
_populate_sdk_fixture("${staging_sdk}" "${new_fp}")
_skyrim_le_activate_staging("${staging_sdk}" "${active_sdk}" "${backup_sdk}" "${new_fp}")
_skyrim_le_validate_sdk_tree("${active_sdk}" "${new_fp}" active_updated_after_recovery)
if(NOT active_updated_after_recovery)
    message(FATAL_ERROR "Regression: active SDK was not updated after successful staging activation.")
endif()

# Roll back to the verified backup when post-activation integrity check fails.
set(rollback_active_dir "${sandbox}/rollback_active")
set(rollback_backup_dir "${sandbox}/rollback_backup")
set(rollback_staging_dir "${sandbox}/rollback_staging")
_populate_sdk_fixture("${rollback_active_dir}" "${old_fp}")
_populate_sdk_fixture("${rollback_staging_dir}" "${new_fp}")
file(REMOVE_RECURSE "${rollback_backup_dir}")
_skyrim_le_compute_manifest_digest("${rollback_active_dir}" expected_rollback_digest)

set(post_activation_rollback_script "${sandbox}/test_post_activation_rollback.cmake")
set(SDK_ROLLBACK_SOURCE_DIR "${INPUTNEXTADAPTERS_SOURCE_DIR}")
set(SDK_ROLLBACK_ACTIVE_DIR "${rollback_active_dir}")
set(SDK_ROLLBACK_BACKUP_DIR "${rollback_backup_dir}")
set(SDK_ROLLBACK_STAGING_DIR "${rollback_staging_dir}")
set(SDK_ROLLBACK_NEW_FINGERPRINT "${new_fp}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/SdkPostActivationRollback.cmake.in"
    "${post_activation_rollback_script}"
    @ONLY
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -P "${post_activation_rollback_script}"
    RESULT_VARIABLE rollback_result
    OUTPUT_VARIABLE rollback_stdout
    ERROR_VARIABLE rollback_stderr
)
if(rollback_result EQUAL 0)
    message(FATAL_ERROR "Regression: staging activation succeeded unexpectedly despite post-activation integrity failure.")
endif()

if(NOT rollback_stderr MATCHES "Activated SDK failed post-activation validation check" OR
   NOT rollback_stderr MATCHES "Successfully rolled back to verified previous SDK")
    message(FATAL_ERROR "Regression: post-activation rollback error diagnostics did not contain expected messages. Output was:\n${rollback_stderr}")
endif()

if(NOT EXISTS "${rollback_active_dir}")
    message(FATAL_ERROR "Regression: active directory does not exist after rollback.")
endif()
if(EXISTS "${rollback_backup_dir}")
    message(FATAL_ERROR "Regression: backup directory still exists after being restored to active.")
endif()
if(EXISTS "${rollback_staging_dir}")
    message(FATAL_ERROR "Regression: staging directory still exists after activation failure.")
endif()

_skyrim_le_inspect_sdk_tree("${rollback_active_dir}" "${old_fp}" rollback_state_old_fingerprint)
if(NOT rollback_state_old_fingerprint STREQUAL "CURRENT_VALID")
    message(FATAL_ERROR "Regression: restored active SDK is not CURRENT_VALID for previous fingerprint (${rollback_state_old_fingerprint}).")
endif()

_skyrim_le_inspect_sdk_tree("${rollback_active_dir}" "${new_fp}" rollback_state_new_fingerprint)
if(NOT rollback_state_new_fingerprint STREQUAL "OUTDATED_VALID")
    message(FATAL_ERROR "Regression: restored active SDK should be OUTDATED_VALID for new fingerprint (${rollback_state_new_fingerprint}).")
endif()

_skyrim_le_validate_sdk_tree("${rollback_active_dir}" "${old_fp}" rollback_tree_valid_for_old)
if(NOT rollback_tree_valid_for_old)
    message(FATAL_ERROR "Regression: validation returned false for restored active SDK with previous fingerprint.")
endif()

_skyrim_le_validate_sdk_tree("${rollback_active_dir}" "${new_fp}" rollback_tree_valid_for_new)
if(rollback_tree_valid_for_new)
    message(FATAL_ERROR "Regression: restored active SDK was unexpectedly accepted for unprepared new fingerprint.")
endif()

_skyrim_le_compute_manifest_digest("${rollback_active_dir}" rollback_manifest_digest)
if(NOT rollback_manifest_digest STREQUAL expected_rollback_digest)
    message(FATAL_ERROR "Regression: restored manifest digest (${rollback_manifest_digest}) does not match expected digest (${expected_rollback_digest}).")
endif()

# Fail cleanly on missing SKSE archive without creating destination directories.
set(missing_archive_sandbox "${sandbox}/missing_archive")
set(non_existent_archive_dir "${missing_archive_sandbox}/user_non_existent_dir")
set(non_existent_archive_file "${non_existent_archive_dir}/missing_skse.7z")

set(missing_archive_script "${missing_archive_sandbox}/test_missing_archive.cmake")
file(MAKE_DIRECTORY "${missing_archive_sandbox}")
file(WRITE "${missing_archive_script}" "
set(CMAKE_CURRENT_BINARY_DIR \"${missing_archive_sandbox}\")
set(CMAKE_SIZEOF_VOID_P 4)
set(MSVC 1)
set(INPUTNEXT_SKYRIM_LE_SKSE_ARCHIVE \"${non_existent_archive_file}\" CACHE FILEPATH \"\" FORCE)
include(\"${INPUTNEXTADAPTERS_SOURCE_DIR}/adapters/skyrim_le/cmake/SkseSdk.cmake\")
")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -P "${missing_archive_script}"
    RESULT_VARIABLE missing_archive_result
    OUTPUT_VARIABLE missing_archive_stdout
    ERROR_VARIABLE missing_archive_stderr
)

if(missing_archive_result EQUAL 0)
    message(FATAL_ERROR "Regression: SkseSdk configuration succeeded unexpectedly when SKSE archive was missing.")
endif()

if(NOT missing_archive_stderr MATCHES "The configured local SKSE archive does not exist")
    message(FATAL_ERROR "Regression: missing archive diagnostic did not match expected message. Output was:\n${missing_archive_stderr}")
endif()

if(EXISTS "${non_existent_archive_dir}")
    message(FATAL_ERROR "Regression: destination directory was created before verifying archive existence.")
endif()
