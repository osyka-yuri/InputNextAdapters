include_guard(GLOBAL)

function(_skyrim_le_get_expected_subpaths out_subpaths)
    set(${out_subpaths}
        "src/skse/skse/PluginAPI.h"
        "src/common/IPrefix.h"
        "src/common/ITypes.h"
        "src/skse/skse/GameAPI.cpp"
        "src/skse/skse/GameBSExtraData.cpp"
        "src/skse/skse/GameData.cpp"
        "src/skse/skse/GameEvents.cpp"
        "src/skse/skse/GameForms.cpp"
        "src/skse/skse/GameMenus.cpp"
        "src/skse/skse/GameReferences.cpp"
        "src/skse/skse/GameRTTI.cpp"
        "src/skse/skse/GameSettings.cpp"
        "src/skse/skse/ScaleformAPI.cpp"
        "src/skse/skse/GameTypes.cpp"
        "src/skse/skse/GameTypes.h"
        "src/skse/skse/GameEvents.h"
        "src/skse/skse/NiTypes.h"
        "src/skse/skse/GameRTTI.h"
        "src/skse/skse/Utilities.h"
        "src/skse/skse/GameMenus.h"
        "src/skse/skse/PapyrusEvents.h"
        "src/skse/skse/GameAPI.h"
        "src/skse/skse/GameData.h"
        "src/skse/skse/GameFormComponents.h"
        "src/skse/skse/GameForms.h"
        "src/skse/skse/GameObjects.h"
        "src/skse/skse/GameReferences.h"
        "src/skse/skse/GameSettings.h"
        "src/skse/skse/GameThreads.h"
        "src/skse/skse/ScaleformMovie.h"
        "src/skse/skse/skse_version.h"
        PARENT_SCOPE
    )
endfunction()

function(_skyrim_le_compute_external_tree_hash override_root out_hash)
    set(${out_hash} "" PARENT_SCOPE)
    if(NOT override_root OR NOT EXISTS "${override_root}")
        return()
    endif()
    get_filename_component(_abs_root "${override_root}" ABSOLUTE)
    file(GLOB_RECURSE _ext_files LIST_DIRECTORIES FALSE "${_abs_root}/src/*")
    list(SORT _ext_files)
    set(_ext_entries "")
    foreach(_ext_file IN LISTS _ext_files)
        file(RELATIVE_PATH _rel_file "${_abs_root}" "${_ext_file}")
        file(SHA256 "${_ext_file}" _fhash)
        string(APPEND _ext_entries "${_rel_file}:${_fhash}\n")
    endforeach()
    if(EXISTS "${_abs_root}/skse_readme.txt")
        file(SHA256 "${_abs_root}/skse_readme.txt" _rhash)
        string(APPEND _ext_entries "skse_readme.txt:${_rhash}\n")
    endif()
    string(SHA256 _computed_hash "${_ext_entries}")
    set(${out_hash} "${_computed_hash}" PARENT_SCOPE)
endfunction()

function(_skyrim_le_compute_manifest_digest dir out_digest)
    set(${out_digest} "" PARENT_SCOPE)
    _skyrim_le_get_expected_subpaths(_subpaths)
    foreach(_sub IN LISTS _subpaths)
        set(_full_path "${dir}/${_sub}")
        if(NOT EXISTS "${_full_path}" OR IS_DIRECTORY "${_full_path}")
            return()
        endif()
    endforeach()

    file(GLOB_RECURSE _manifest_files
        RELATIVE "${dir}"
        LIST_DIRECTORIES FALSE
        "${dir}/src/*")
    list(SORT _manifest_files)
    set(_manifest_entries "")
    foreach(_manifest_file IN LISTS _manifest_files)
        file(TO_CMAKE_PATH "${_manifest_file}" _relative_path)
        file(SHA256 "${dir}/${_manifest_file}" _file_hash)
        string(APPEND _manifest_entries "${_relative_path}:${_file_hash}\n")
    endforeach()

    string(SHA256 _computed "${_manifest_entries}")
    set(${out_digest} "${_computed}" PARENT_SCOPE)
endfunction()

function(_skyrim_le_inspect_sdk_tree dir expected_fingerprint out_state)
    set(${out_state} "CORRUPTED" PARENT_SCOPE)
    if(NOT IS_DIRECTORY "${dir}")
        return()
    endif()
    set(_state_file "${dir}/.prepared_state")
    if(NOT EXISTS "${_state_file}")
        return()
    endif()
    file(STRINGS "${_state_file}" _state_lines)
    list(LENGTH _state_lines _lines_len)
    if(_lines_len LESS 2)
        return()
    endif()
    list(GET _state_lines 0 _recorded_fp)
    list(GET _state_lines 1 _recorded_digest)
    if(_recorded_digest STREQUAL "")
        return()
    endif()
    _skyrim_le_compute_manifest_digest("${dir}" _actual_digest)
    if(NOT _actual_digest STREQUAL _recorded_digest OR _actual_digest STREQUAL "")
        return()
    endif()
    if(_recorded_fp STREQUAL expected_fingerprint)
        set(${out_state} "CURRENT_VALID" PARENT_SCOPE)
    else()
        set(${out_state} "OUTDATED_VALID" PARENT_SCOPE)
    endif()
endfunction()

function(_skyrim_le_validate_sdk_tree dir expected_fingerprint out_valid)
    _skyrim_le_inspect_sdk_tree("${dir}" "${expected_fingerprint}" _state)
    if(_state STREQUAL "CURRENT_VALID")
        set(${out_valid} TRUE PARENT_SCOPE)
    else()
        set(${out_valid} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(_skyrim_le_recover_sdk_state active_dir backup_dir expected_fingerprint out_needs_prepare)
    _skyrim_le_inspect_sdk_tree("${active_dir}" "${expected_fingerprint}" _active_state)
    _skyrim_le_inspect_sdk_tree("${backup_dir}" "${expected_fingerprint}" _backup_state)

    if(_active_state STREQUAL "CURRENT_VALID")
        if(EXISTS "${backup_dir}")
            file(REMOVE_RECURSE "${backup_dir}")
        endif()
        set(${out_needs_prepare} FALSE PARENT_SCOPE)
        return()
    endif()

    if(_backup_state STREQUAL "CURRENT_VALID")
        if(EXISTS "${active_dir}")
            file(REMOVE_RECURSE "${active_dir}")
        endif()
        file(RENAME "${backup_dir}" "${active_dir}" RESULT _restore_res)
        if(NOT _restore_res EQUAL 0)
            message(FATAL_ERROR
                "Failed to restore valid backup to active SDK (${_restore_res}). Valid backup preserved at ${backup_dir}.")
        endif()
        set(${out_needs_prepare} FALSE PARENT_SCOPE)
        return()
    endif()

    # Preserve an OUTDATED_VALID backup during staging preparation.
    set(${out_needs_prepare} TRUE PARENT_SCOPE)
endfunction()

function(_skyrim_le_activate_staging staging_dir active_dir backup_dir expected_fingerprint)
    _skyrim_le_inspect_sdk_tree("${staging_dir}" "${expected_fingerprint}" _staging_state)
    if(NOT _staging_state STREQUAL "CURRENT_VALID")
        message(FATAL_ERROR "Staging SDK verification failed. Refusing to activate. Existing active SDK preserved.")
    endif()

    _skyrim_le_inspect_sdk_tree("${active_dir}" "${expected_fingerprint}" _active_state)
    _skyrim_le_inspect_sdk_tree("${backup_dir}" "${expected_fingerprint}" _backup_state)

    if(EXISTS "${active_dir}")
        if(_active_state STREQUAL "CORRUPTED")
            # Corrupted active files are discarded rather than rotated into backup.
            file(REMOVE_RECURSE "${active_dir}")
        else()
            # Rotate valid active tree into backup before activation.
            if(EXISTS "${backup_dir}")
                file(REMOVE_RECURSE "${backup_dir}")
            endif()
            file(RENAME "${active_dir}" "${backup_dir}" RESULT _backup_res)
            if(NOT _backup_res EQUAL 0)
                message(FATAL_ERROR "Failed to create backup of existing SDK tree (${_backup_res}). Previous SDK preserved at ${active_dir}.")
            endif()
        endif()
    endif()

    file(RENAME "${staging_dir}" "${active_dir}" RESULT _activate_res)
    if(NOT _activate_res EQUAL 0)
        if(EXISTS "${backup_dir}")
            if(EXISTS "${active_dir}")
                file(REMOVE_RECURSE "${active_dir}")
            endif()
            file(RENAME "${backup_dir}" "${active_dir}" RESULT _rollback_res)
            if(NOT _rollback_res EQUAL 0)
                message(FATAL_ERROR
                    "Failed to activate newly patched SDK (${_activate_res}), AND rollback rename failed (${_rollback_res}). Valid backup preserved at ${backup_dir}.")
            else()
                message(FATAL_ERROR
                    "Failed to activate newly patched SDK (${_activate_res}). Successfully rolled back to previous SDK.")
            endif()
        else()
            message(FATAL_ERROR "Failed to activate newly patched SDK (${_activate_res}). No backup directory existed.")
        endif()
    endif()

    _skyrim_le_inspect_sdk_tree("${active_dir}" "${expected_fingerprint}" _new_active_state)
    if(NOT _new_active_state STREQUAL "CURRENT_VALID")
        if(EXISTS "${backup_dir}")
            if(EXISTS "${active_dir}")
                file(REMOVE_RECURSE "${active_dir}")
            endif()
            file(RENAME "${backup_dir}" "${active_dir}" RESULT _rollback_res)
            if(NOT _rollback_res EQUAL 0)
                message(FATAL_ERROR
                    "Activated SDK failed post-activation validation check (${_new_active_state}), AND rollback rename failed (${_rollback_res}). Valid backup preserved at ${backup_dir}.")
            else()
                _skyrim_le_inspect_sdk_tree("${active_dir}" "${expected_fingerprint}" _restored_state)
                if(_restored_state STREQUAL "CURRENT_VALID" OR _restored_state STREQUAL "OUTDATED_VALID")
                    message(FATAL_ERROR
                        "Activated SDK failed post-activation validation check (${_new_active_state}). Successfully rolled back to verified previous SDK.")
                else()
                    message(FATAL_ERROR
                        "Activated SDK failed post-activation validation check (${_new_active_state}). Rolled back from backup, but restored SDK failed validation (${_restored_state}).")
                endif()
            endif()
        else()
            if(EXISTS "${active_dir}")
                file(REMOVE_RECURSE "${active_dir}")
            endif()
            message(FATAL_ERROR
                "Activated SDK failed post-activation validation check (${_new_active_state}); no backup directory existed.")
        endif()
    endif()

    if(EXISTS "${backup_dir}")
        file(REMOVE_RECURSE "${backup_dir}")
    endif()
endfunction()
