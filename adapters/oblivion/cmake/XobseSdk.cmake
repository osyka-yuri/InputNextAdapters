include_guard(GLOBAL)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The Oblivion xOBSE SDK target requires a 32-bit toolchain.")
endif()
if(CMAKE_GENERATOR_PLATFORM AND NOT CMAKE_GENERATOR_PLATFORM MATCHES "^(Win32|x86)$")
    message(FATAL_ERROR "The Oblivion xOBSE SDK target requires the Win32/x86 platform.")
endif()
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "X86")
    message(FATAL_ERROR "The Oblivion xOBSE SDK target requires an x86 compiler target.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "The Oblivion xOBSE SDK target requires an MSVC-compatible toolchain.")
endif()

set(_oblivion_xobse_archive_url "https://github.com/llde/xOBSE/archive/refs/tags/22.15.zip")
set(_oblivion_xobse_archive_sha256 "771E45E8E00EE6FEE7E2C5AAC90B74ED3DCC034226300C14D96937BCEE1902EB")
set(_oblivion_xobse_plugin_api_sha256 "8ED42E83DC6905445D4A416ED7490095940B2EADEE3AFE8A016C1467A61D949A")

set(INPUTNEXT_OBLIVION_XOBSE_SOURCE_DIR "" CACHE PATH
    "Optional extracted xOBSE 22.15 tree; must match the pinned archive")
set(INPUTNEXT_OBLIVION_XOBSE_ARCHIVE "" CACHE FILEPATH
    "Optional local xOBSE 22.15 archive; its pinned SHA-256 is always checked")

if(NOT INPUTNEXT_OBLIVION_XOBSE_ARCHIVE AND DEFINED ENV{USERPROFILE})
    set(_candidate_local_archive "$ENV{USERPROFILE}/Downloads/xOBSE-22.15-src.zip")
    if(EXISTS "${_candidate_local_archive}")
        set(INPUTNEXT_OBLIVION_XOBSE_ARCHIVE "${_candidate_local_archive}" CACHE FILEPATH
            "Optional local xOBSE 22.15 archive; its pinned SHA-256 is always checked" FORCE)
    endif()
endif()

if((INPUTNEXTADAPTERS_RELEASE_BUILD OR INPUTNEXTADAPTERS_PRODUCTION) AND INPUTNEXT_OBLIVION_XOBSE_SOURCE_DIR)
    message(FATAL_ERROR
        "Release-producing and production configures reject INPUTNEXT_OBLIVION_XOBSE_SOURCE_DIR; use the pinned archive input instead.")
endif()

set(_oblivion_sdk_private_root "${CMAKE_CURRENT_BINARY_DIR}/_oblivion_xobse_22_15_private")
set(_oblivion_sdk_archive "${INPUTNEXT_OBLIVION_XOBSE_ARCHIVE}")

if(_oblivion_sdk_archive)
    if(NOT EXISTS "${_oblivion_sdk_archive}")
        message(FATAL_ERROR "The configured local xOBSE archive does not exist: ${_oblivion_sdk_archive}")
    endif()
else()
    set(_oblivion_sdk_archive "${_oblivion_sdk_private_root}/xOBSE-22.15.zip")
    get_filename_component(_oblivion_sdk_archive_dir "${_oblivion_sdk_archive}" DIRECTORY)
    file(MAKE_DIRECTORY "${_oblivion_sdk_archive_dir}")
endif()

if(NOT EXISTS "${_oblivion_sdk_archive}")
    file(DOWNLOAD
        "${_oblivion_xobse_archive_url}"
        "${_oblivion_sdk_archive}"
        EXPECTED_HASH "SHA256=${_oblivion_xobse_archive_sha256}"
        TLS_VERIFY ON
        STATUS _oblivion_download_status
        LOG _oblivion_download_log
        SHOW_PROGRESS
    )
    list(GET _oblivion_download_status 0 _oblivion_download_code)
    if(NOT _oblivion_download_code EQUAL 0)
        message(FATAL_ERROR
            "Could not fetch the pinned xOBSE 22.15 archive (${_oblivion_download_status}).\n${_oblivion_download_log}")
    endif()
endif()

file(SHA256 "${_oblivion_sdk_archive}" _oblivion_archive_hash)
string(TOLOWER "${_oblivion_xobse_archive_sha256}" _oblivion_expected_archive_hash)
if(NOT _oblivion_archive_hash STREQUAL _oblivion_expected_archive_hash)
    message(FATAL_ERROR
        "xOBSE 22.15 archive SHA-256 mismatch: expected ${_oblivion_expected_archive_hash}, got ${_oblivion_archive_hash}.")
endif()

set(_oblivion_sdk_extracted_root "${_oblivion_sdk_private_root}/extracted")
set(_oblivion_sdk_source_root "${_oblivion_sdk_extracted_root}/xOBSE-22.15")
set(_oblivion_sdk_extracted_stamp "${_oblivion_sdk_extracted_root}/.xobse_${_oblivion_expected_archive_hash}.stamp")

if(INPUTNEXT_OBLIVION_XOBSE_SOURCE_DIR)
    get_filename_component(_oblivion_sdk_source_root "${INPUTNEXT_OBLIVION_XOBSE_SOURCE_DIR}" ABSOLUTE)
elseif(NOT EXISTS "${_oblivion_sdk_source_root}/obse/obse/PluginAPI.h"
       OR NOT EXISTS "${_oblivion_sdk_extracted_stamp}")
    file(REMOVE_RECURSE "${_oblivion_sdk_extracted_root}")
    file(MAKE_DIRECTORY "${_oblivion_sdk_extracted_root}")
    file(ARCHIVE_EXTRACT
        INPUT "${_oblivion_sdk_archive}"
        DESTINATION "${_oblivion_sdk_extracted_root}"
    )
    file(WRITE "${_oblivion_sdk_extracted_stamp}" "${_oblivion_expected_archive_hash}\n")
endif()

if(NOT EXISTS "${_oblivion_sdk_source_root}/obse/obse/PluginAPI.h"
   OR NOT EXISTS "${_oblivion_sdk_source_root}/common/IPrefix.h")
    message(FATAL_ERROR "The extracted xOBSE tree has an unexpected source layout at '${_oblivion_sdk_source_root}'.")
endif()

file(SHA256 "${_oblivion_sdk_source_root}/obse/obse/PluginAPI.h" _oblivion_actual_plugin_api_hash)
string(TOLOWER "${_oblivion_xobse_plugin_api_sha256}" _oblivion_expected_plugin_api_hash)
if(NOT _oblivion_actual_plugin_api_hash STREQUAL _oblivion_expected_plugin_api_hash)
    message(FATAL_ERROR
        "xOBSE PluginAPI.h SHA-256 mismatch: expected ${_oblivion_expected_plugin_api_hash}, got ${_oblivion_actual_plugin_api_hash}. SDK tree integrity compromised.")
endif()

add_library(oblivion_sdk INTERFACE)
target_compile_definitions(oblivion_sdk INTERFACE
    WIN32
    _WINDOWS
    OBLIVION
    OBLIVION_VERSION=0x010201A0
    NOMINMAX
)

target_include_directories(oblivion_sdk
    SYSTEM INTERFACE
        "${_oblivion_sdk_source_root}"
        "${_oblivion_sdk_source_root}/obse"
        "${_oblivion_sdk_source_root}/obse/obse"
        "${_oblivion_sdk_source_root}/common"
)

target_compile_features(oblivion_sdk INTERFACE cxx_std_23)
