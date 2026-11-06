include_guard(GLOBAL)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The Skyrim LE SKSE SDK target requires a 32-bit toolchain.")
endif()
if(CMAKE_GENERATOR_PLATFORM AND NOT CMAKE_GENERATOR_PLATFORM MATCHES "^(Win32|x86)$")
    message(FATAL_ERROR "The Skyrim LE SKSE SDK target requires the Win32/x86 platform.")
endif()
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID
   AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "X86")
    message(FATAL_ERROR "The Skyrim LE SKSE SDK target requires an x86 compiler target.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "The Skyrim LE SKSE SDK target requires an MSVC-compatible toolchain.")
endif()

set(_skyrim_le_skse_archive_url "https://skse.silverlock.org/beta/skse_1_07_03.7z")
set(_skyrim_le_skse_archive_sha256 "1825024212A20A6A197BBCD308073B497E516139AD48D60685D5B18AB4BBE76D")

set(INPUTNEXT_SKYRIM_LE_SKSE_SOURCE_DIR "" CACHE PATH
    "Optional extracted SKSE 1.7.3 tree for offline PRE; must match the pinned archive")
set(INPUTNEXT_SKYRIM_LE_SKSE_ARCHIVE "" CACHE FILEPATH
    "Optional local SKSE 1.7.3 archive; its pinned SHA-256 is always checked")

if(INPUTNEXTADAPTERS_RELEASE_BUILD AND INPUTNEXT_SKYRIM_LE_SKSE_SOURCE_DIR)
    message(FATAL_ERROR
        "Release-producing configures reject INPUTNEXT_SKYRIM_LE_SKSE_SOURCE_DIR; use the pinned archive input instead.")
endif()

if(INPUTNEXT_SKYRIM_LE_SKSE_SOURCE_DIR AND NOT INPUTNEXT_SKYRIM_LE_SKSE_ARCHIVE)
    message(FATAL_ERROR
        "INPUTNEXT_SKYRIM_LE_SKSE_SOURCE_DIR requires INPUTNEXT_SKYRIM_LE_SKSE_ARCHIVE so the extracted source can be provenance-checked.")
endif()

set(_skyrim_le_sdk_private_root "${CMAKE_CURRENT_BINARY_DIR}/_skyrim_le_skse_1_07_03_private")
set(_skyrim_le_sdk_archive "${INPUTNEXT_SKYRIM_LE_SKSE_ARCHIVE}")

if(_skyrim_le_sdk_archive)
    if(NOT EXISTS "${_skyrim_le_sdk_archive}")
        message(FATAL_ERROR "The configured local SKSE archive does not exist: ${_skyrim_le_sdk_archive}")
    endif()
else()
    set(_skyrim_le_sdk_archive "${_skyrim_le_sdk_private_root}/skse_1_07_03.7z")
    get_filename_component(_skyrim_le_sdk_archive_dir "${_skyrim_le_sdk_archive}" DIRECTORY)
    file(MAKE_DIRECTORY "${_skyrim_le_sdk_archive_dir}")
endif()

if(NOT EXISTS "${_skyrim_le_sdk_archive}")
    file(DOWNLOAD
        "${_skyrim_le_skse_archive_url}"
        "${_skyrim_le_sdk_archive}"
        EXPECTED_HASH "SHA256=${_skyrim_le_skse_archive_sha256}"
        TLS_VERIFY ON
        STATUS _skyrim_le_download_status
        LOG _skyrim_le_download_log
        SHOW_PROGRESS
    )
    list(GET _skyrim_le_download_status 0 _skyrim_le_download_code)
    if(NOT _skyrim_le_download_code EQUAL 0)
        message(FATAL_ERROR
            "Could not fetch the pinned SKSE 1.7.3 archive (${_skyrim_le_download_status}).\n${_skyrim_le_download_log}")
    endif()
endif()

file(SHA256 "${_skyrim_le_sdk_archive}" _skyrim_le_archive_hash)
string(TOLOWER "${_skyrim_le_skse_archive_sha256}" _skyrim_le_expected_archive_hash)
if(NOT _skyrim_le_archive_hash STREQUAL _skyrim_le_expected_archive_hash)
    message(FATAL_ERROR
        "SKSE 1.7.3 archive SHA-256 mismatch: expected ${_skyrim_le_expected_archive_hash}, got ${_skyrim_le_archive_hash}.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/SkseSdkState.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/SkseSdkPatches.cmake")

set(_skyrim_le_sdk_source_root "${_skyrim_le_sdk_private_root}/patched")
set(_skyrim_le_backup_root "${_skyrim_le_sdk_private_root}/backup_patched")
set(_skyrim_le_staging_root "${_skyrim_le_sdk_private_root}/staging")

file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" _skyrim_le_cmake_hash)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/SkseSdkState.cmake" _skyrim_le_state_cmake_hash)
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/SkseSdkPatches.cmake" _skyrim_le_patches_cmake_hash)

_skyrim_le_compute_external_tree_hash("${INPUTNEXT_SKYRIM_LE_SKSE_SOURCE_DIR}" _skyrim_le_external_tree_hash)

string(SHA256 _skyrim_le_fingerprint
    "${_skyrim_le_archive_hash}:${_skyrim_le_cmake_hash}:${_skyrim_le_state_cmake_hash}:${_skyrim_le_patches_cmake_hash}:${INPUTNEXT_SKYRIM_LE_SKSE_SOURCE_DIR}:${_skyrim_le_external_tree_hash}"
)

_skyrim_le_recover_sdk_state(
    "${_skyrim_le_sdk_source_root}"
    "${_skyrim_le_backup_root}"
    "${_skyrim_le_fingerprint}"
    _skyrim_le_needs_prepare
)

if(_skyrim_le_needs_prepare)
    set(_skyrim_le_staging_root "${_skyrim_le_sdk_private_root}/staging")
    set(_skyrim_le_staging_verified "${_skyrim_le_staging_root}/verified-archive")
    set(_skyrim_le_staging_patched "${_skyrim_le_staging_root}/patched")

    file(REMOVE_RECURSE "${_skyrim_le_staging_root}")
    file(MAKE_DIRECTORY "${_skyrim_le_staging_verified}")
    file(ARCHIVE_EXTRACT
        INPUT "${_skyrim_le_sdk_archive}"
        DESTINATION "${_skyrim_le_staging_verified}"
    )
    set(_skyrim_le_verified_source
        "${_skyrim_le_staging_verified}/skse_1_07_03")
    if(NOT EXISTS "${_skyrim_le_verified_source}/src/skse/skse/PluginAPI.h"
       OR NOT EXISTS "${_skyrim_le_verified_source}/src/common/IPrefix.h"
       OR NOT EXISTS "${_skyrim_le_verified_source}/skse_readme.txt")
        message(FATAL_ERROR "The pinned SKSE archive has an unexpected source layout.")
    endif()

    if(INPUTNEXT_SKYRIM_LE_SKSE_SOURCE_DIR)
        get_filename_component(_skyrim_le_override_source "${INPUTNEXT_SKYRIM_LE_SKSE_SOURCE_DIR}" ABSOLUTE)
        if(NOT EXISTS "${_skyrim_le_override_source}/src/skse/skse/PluginAPI.h"
           OR NOT EXISTS "${_skyrim_le_override_source}/src/common/IPrefix.h"
           OR NOT EXISTS "${_skyrim_le_override_source}/skse_readme.txt")
            message(FATAL_ERROR "INPUTNEXT_SKYRIM_LE_SKSE_SOURCE_DIR is not an extracted SKSE 1.7.3 root.")
        endif()

        file(GLOB_RECURSE _skyrim_le_verified_files
            RELATIVE "${_skyrim_le_verified_source}/src"
            LIST_DIRECTORIES FALSE
            "${_skyrim_le_verified_source}/src/*")
        file(GLOB_RECURSE _skyrim_le_override_files
            RELATIVE "${_skyrim_le_override_source}/src"
            LIST_DIRECTORIES FALSE
            "${_skyrim_le_override_source}/src/*")
        list(SORT _skyrim_le_verified_files)
        list(SORT _skyrim_le_override_files)
        if(NOT _skyrim_le_verified_files STREQUAL _skyrim_le_override_files)
            message(FATAL_ERROR "The extracted SKSE source override has a different file set from the pinned archive.")
        endif()
        foreach(_skyrim_le_relative_file IN LISTS _skyrim_le_verified_files)
            file(SHA256
                "${_skyrim_le_verified_source}/src/${_skyrim_le_relative_file}"
                _skyrim_le_verified_file_hash)
            file(SHA256
                "${_skyrim_le_override_source}/src/${_skyrim_le_relative_file}"
                _skyrim_le_override_file_hash)
            if(NOT _skyrim_le_verified_file_hash STREQUAL _skyrim_le_override_file_hash)
                message(FATAL_ERROR
                    "The extracted SKSE source override differs from the pinned archive at ${_skyrim_le_relative_file}.")
            endif()
        endforeach()
        file(SHA256 "${_skyrim_le_verified_source}/skse_readme.txt" _skyrim_le_readme_hash)
        file(SHA256 "${_skyrim_le_override_source}/skse_readme.txt" _skyrim_le_override_readme_hash)
        if(NOT _skyrim_le_readme_hash STREQUAL _skyrim_le_override_readme_hash)
            message(FATAL_ERROR "The extracted SKSE readme differs from the pinned archive.")
        endif()
    endif()

    file(MAKE_DIRECTORY "${_skyrim_le_staging_patched}")
    file(COPY "${_skyrim_le_verified_source}/src" DESTINATION "${_skyrim_le_staging_patched}")

    set(_skyrim_le_final_source_root "${_skyrim_le_sdk_source_root}")
    set(_skyrim_le_sdk_source_root "${_skyrim_le_staging_patched}")

    _skyrim_le_apply_compatibility_patches("${_skyrim_le_staging_patched}")

    _skyrim_le_compute_manifest_digest("${_skyrim_le_staging_patched}" _staging_digest)
    file(WRITE "${_skyrim_le_staging_patched}/.prepared_state" "${_skyrim_le_fingerprint}\n${_staging_digest}\n")

    _skyrim_le_activate_staging(
        "${_skyrim_le_staging_patched}"
        "${_skyrim_le_final_source_root}"
        "${_skyrim_le_backup_root}"
        "${_skyrim_le_fingerprint}"
    )

    file(REMOVE_RECURSE "${_skyrim_le_staging_root}")
    set(_skyrim_le_sdk_source_root "${_skyrim_le_final_source_root}")
endif()

set(_skyrim_le_sdk_sources
    "${_skyrim_le_sdk_source_root}/src/skse/skse/GameAPI.cpp"
    "${_skyrim_le_sdk_source_root}/src/skse/skse/GameBSExtraData.cpp"
    "${_skyrim_le_sdk_source_root}/src/skse/skse/GameData.cpp"
    "${_skyrim_le_sdk_source_root}/src/skse/skse/GameEvents.cpp"
    "${_skyrim_le_sdk_source_root}/src/skse/skse/GameForms.cpp"
    "${_skyrim_le_sdk_source_root}/src/skse/skse/GameMenus.cpp"
    "${_skyrim_le_sdk_source_root}/src/skse/skse/GameReferences.cpp"
    "${_skyrim_le_sdk_source_root}/src/skse/skse/GameRTTI.cpp"
    "${_skyrim_le_sdk_source_root}/src/skse/skse/GameSettings.cpp"
    "${_skyrim_le_sdk_source_root}/src/skse/skse/ScaleformAPI.cpp"
    "${_skyrim_le_sdk_source_root}/src/skse/skse/GameTypes.cpp")

add_library(skyrim_le_sdk STATIC ${_skyrim_le_sdk_sources})
target_compile_definitions(skyrim_le_sdk PRIVATE NOMINMAX)
target_include_directories(skyrim_le_sdk
    SYSTEM PUBLIC
        "${_skyrim_le_sdk_source_root}/src"
        "${_skyrim_le_sdk_source_root}/src/skse")
target_compile_features(skyrim_le_sdk PUBLIC cxx_std_23)
set_target_properties(skyrim_le_sdk PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES)
target_compile_options(skyrim_le_sdk PRIVATE
    /permissive-
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
    /W3
    /EHsc
    "/FI${_skyrim_le_sdk_source_root}/src/common/IPrefix.h")
