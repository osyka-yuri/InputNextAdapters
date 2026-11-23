include_guard(GLOBAL)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The Fallout 3 FOSE SDK target requires a 32-bit toolchain.")
endif()
if(CMAKE_GENERATOR_PLATFORM AND NOT CMAKE_GENERATOR_PLATFORM MATCHES "^(Win32|x86)$")
    message(FATAL_ERROR "The Fallout 3 FOSE SDK target requires the Win32/x86 platform.")
endif()
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID AND NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "X86")
    message(FATAL_ERROR "The Fallout 3 FOSE SDK target requires an x86 compiler target.")
endif()

set(_fallout3_fose_archive_url "https://fose.silverlock.org/beta/fose_v1_3_beta2.7z")
set(_fallout3_fose_archive_sha256 "67be7d78fac301acac0491d34020ed2a0b98b9b06ce0b4f5ea26d9da21351e04")
set(_fallout3_fose_plugin_api_sha256 "6A56D64AF42AAAD7A1630BD35B82BE1538E36A635E5E94A21392B7C3920176FE")
set(_fallout3_fose_version_header_sha256 "759B91E18137FDA2475026C06E30CB42FDD92387E25C5DD07601DA1373659D9E")

set(INPUTNEXT_FALLOUT3_FOSE_ARCHIVE "" CACHE FILEPATH
    "Optional local official FOSE 1.3 beta2 source archive; its pinned SHA-256 is always checked")

set(_fallout3_fose_private_root "${CMAKE_CURRENT_BINARY_DIR}/_fallout3_fose_1_3_beta2_private")
set(_fallout3_fose_archive "${INPUTNEXT_FALLOUT3_FOSE_ARCHIVE}")

if(_fallout3_fose_archive)
    if(NOT EXISTS "${_fallout3_fose_archive}")
        message(FATAL_ERROR "The configured FOSE SDK archive does not exist: ${_fallout3_fose_archive}")
    endif()
    file(SHA256 "${_fallout3_fose_archive}" _fallout3_fose_actual_archive_sha256)
    if(NOT _fallout3_fose_actual_archive_sha256 STREQUAL _fallout3_fose_archive_sha256)
        message(FATAL_ERROR
            "FOSE 1.3 beta2 archive SHA-256 mismatch: expected ${_fallout3_fose_archive_sha256}, got ${_fallout3_fose_actual_archive_sha256}.")
    endif()
else()
    set(_fallout3_fose_archive "${_fallout3_fose_private_root}/fose_v1_3_beta2.7z")
    file(MAKE_DIRECTORY "${_fallout3_fose_private_root}")

    if(EXISTS "${_fallout3_fose_archive}")
        file(SHA256 "${_fallout3_fose_archive}" _fallout3_fose_cached_archive_sha256)
        if(NOT _fallout3_fose_cached_archive_sha256 STREQUAL _fallout3_fose_archive_sha256)
            file(REMOVE "${_fallout3_fose_archive}")
            message(STATUS "Removed an incomplete or invalid cached FOSE SDK archive.")
        endif()
    endif()

    if(NOT EXISTS "${_fallout3_fose_archive}")
        set(_fallout3_fose_download_staging "${_fallout3_fose_archive}.download")
        file(REMOVE "${_fallout3_fose_download_staging}")
        file(DOWNLOAD
            "${_fallout3_fose_archive_url}"
            "${_fallout3_fose_download_staging}"
            TLS_VERIFY ON
            STATUS _fallout3_fose_download_status
            LOG _fallout3_fose_download_log
        )
        list(GET _fallout3_fose_download_status 0 _fallout3_fose_download_code)
        if(NOT _fallout3_fose_download_code EQUAL 0)
            file(REMOVE "${_fallout3_fose_download_staging}")
            message(FATAL_ERROR
                "Could not fetch the pinned official FOSE SDK archive (${_fallout3_fose_download_status}).\n${_fallout3_fose_download_log}")
        endif()

        file(SHA256 "${_fallout3_fose_download_staging}" _fallout3_fose_download_sha256)
        if(NOT _fallout3_fose_download_sha256 STREQUAL _fallout3_fose_archive_sha256)
            file(REMOVE "${_fallout3_fose_download_staging}")
            message(FATAL_ERROR
                "FOSE 1.3 beta2 archive SHA-256 mismatch: expected ${_fallout3_fose_archive_sha256}, got ${_fallout3_fose_download_sha256}.")
        endif()

        file(RENAME "${_fallout3_fose_download_staging}" "${_fallout3_fose_archive}" RESULT _fallout3_fose_cache_result)
        if(NOT _fallout3_fose_cache_result STREQUAL "0")
            file(REMOVE "${_fallout3_fose_download_staging}")
            message(FATAL_ERROR "Could not cache the verified FOSE SDK archive: ${_fallout3_fose_cache_result}")
        endif()
    endif()
endif()

set(_fallout3_fose_extracted_root "${_fallout3_fose_private_root}/extracted")
set(_fallout3_fose_source_root "${_fallout3_fose_extracted_root}/src")
set(_fallout3_fose_extracted_stamp
    "${_fallout3_fose_extracted_root}/.fose_${_fallout3_fose_archive_sha256}.stamp")

if(NOT EXISTS "${_fallout3_fose_source_root}/fose/fose/PluginAPI.h"
        OR NOT EXISTS "${_fallout3_fose_extracted_stamp}")
    file(REMOVE_RECURSE "${_fallout3_fose_extracted_root}")
    file(MAKE_DIRECTORY "${_fallout3_fose_extracted_root}")
    file(ARCHIVE_EXTRACT
        INPUT "${_fallout3_fose_archive}"
        DESTINATION "${_fallout3_fose_extracted_root}"
    )
    file(WRITE "${_fallout3_fose_extracted_stamp}" "${_fallout3_fose_archive_sha256}\n")
endif()

set(_fallout3_fose_plugin_api "${_fallout3_fose_source_root}/fose/fose/PluginAPI.h")
set(_fallout3_fose_version_header "${_fallout3_fose_source_root}/fose/fose_common/fose_version.h")
if(NOT EXISTS "${_fallout3_fose_plugin_api}" OR NOT EXISTS "${_fallout3_fose_version_header}")
    message(FATAL_ERROR "The pinned FOSE 1.3 beta2 archive has an unexpected source layout.")
endif()

file(SHA256 "${_fallout3_fose_plugin_api}" _fallout3_fose_actual_plugin_api_sha256)
string(TOLOWER "${_fallout3_fose_plugin_api_sha256}" _fallout3_fose_expected_plugin_api_sha256)
if(NOT _fallout3_fose_actual_plugin_api_sha256 STREQUAL _fallout3_fose_expected_plugin_api_sha256)
    message(FATAL_ERROR
        "FOSE PluginAPI.h SHA-256 mismatch: expected ${_fallout3_fose_expected_plugin_api_sha256}, got ${_fallout3_fose_actual_plugin_api_sha256}.")
endif()

file(SHA256 "${_fallout3_fose_version_header}" _fallout3_fose_actual_version_header_sha256)
string(TOLOWER "${_fallout3_fose_version_header_sha256}" _fallout3_fose_expected_version_header_sha256)
if(NOT _fallout3_fose_actual_version_header_sha256 STREQUAL _fallout3_fose_expected_version_header_sha256)
    message(FATAL_ERROR
        "FOSE fose_version.h SHA-256 mismatch: expected ${_fallout3_fose_expected_version_header_sha256}, got ${_fallout3_fose_actual_version_header_sha256}.")
endif()

add_library(fallout3_fose_sdk INTERFACE)
target_include_directories(fallout3_fose_sdk SYSTEM INTERFACE
    "${_fallout3_fose_source_root}"
    "${_fallout3_fose_source_root}/fose"
)
target_compile_features(fallout3_fose_sdk INTERFACE cxx_std_23)
target_compile_definitions(fallout3_fose_sdk INTERFACE
    INPUTNEXT_FALLOUT3_RUNTIME_VERSION=0x01070030
    INPUTNEXT_FALLOUT3_FOSE_VERSION=0x01030020
    NOMINMAX
)
