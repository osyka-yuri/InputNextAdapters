include_guard(GLOBAL)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The New Vegas xNVSE adapter requires an x86 toolchain.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "The New Vegas xNVSE adapter requires an MSVC-compatible toolchain.")
endif()

set(INPUTNEXT_FALLOUT_NEW_VEGAS_NVSE_ARCHIVE "" CACHE FILEPATH
    "Optional local xNVSE 6.4.9 source archive")
set(_fnv_nvse_archive_url "https://github.com/xNVSE/NVSE/archive/refs/tags/6.4.9.zip")
set(_fnv_nvse_archive_sha256 "54f497ea476fe8433c57d865161d0878286a16be9db5279c409988ebf201fd1c")
set(_fnv_nvse_private_root "${CMAKE_CURRENT_BINARY_DIR}/_fallout_new_vegas_nvse_6_4_9")
set(_fnv_nvse_archive "${INPUTNEXT_FALLOUT_NEW_VEGAS_NVSE_ARCHIVE}")

if(_fnv_nvse_archive AND NOT EXISTS "${_fnv_nvse_archive}")
    message(FATAL_ERROR "The configured xNVSE source archive does not exist: ${_fnv_nvse_archive}")
endif()

if(NOT _fnv_nvse_archive)
    set(_fnv_nvse_archive "${_fnv_nvse_private_root}/xNVSE-6.4.9.zip")
    get_filename_component(_fnv_nvse_archive_dir "${_fnv_nvse_archive}" DIRECTORY)
    file(MAKE_DIRECTORY "${_fnv_nvse_archive_dir}")
    if(NOT EXISTS "${_fnv_nvse_archive}")
        file(DOWNLOAD
            "${_fnv_nvse_archive_url}"
            "${_fnv_nvse_archive}"
            EXPECTED_HASH "SHA256=${_fnv_nvse_archive_sha256}"
            TLS_VERIFY ON
            STATUS _fnv_nvse_download_status
            LOG _fnv_nvse_download_log
        )
        list(GET _fnv_nvse_download_status 0 _fnv_nvse_download_code)
        if(NOT _fnv_nvse_download_code EQUAL 0)
            file(REMOVE "${_fnv_nvse_archive}")
            message(FATAL_ERROR
                "Could not download the pinned xNVSE 6.4.9 source archive.\n${_fnv_nvse_download_log}")
        endif()
    endif()
endif()

file(SHA256 "${_fnv_nvse_archive}" _fnv_nvse_actual_sha256)
if(NOT _fnv_nvse_actual_sha256 STREQUAL _fnv_nvse_archive_sha256)
    message(FATAL_ERROR
        "xNVSE 6.4.9 source SHA-256 mismatch: expected ${_fnv_nvse_archive_sha256}, got ${_fnv_nvse_actual_sha256}.")
endif()

set(_fnv_nvse_source_root "${_fnv_nvse_private_root}/source/NVSE-6.4.9")
set(_fnv_nvse_stamp "${_fnv_nvse_private_root}/source/.xnvse_6_4_9.stamp")
if(NOT EXISTS "${_fnv_nvse_source_root}/nvse/nvse/PluginAPI.h" OR NOT EXISTS "${_fnv_nvse_stamp}")
    file(REMOVE_RECURSE "${_fnv_nvse_private_root}/source")
    file(MAKE_DIRECTORY "${_fnv_nvse_private_root}/source")
    file(ARCHIVE_EXTRACT INPUT "${_fnv_nvse_archive}" DESTINATION "${_fnv_nvse_private_root}/source")
    if(NOT EXISTS "${_fnv_nvse_source_root}/nvse/nvse/PluginAPI.h")
        message(FATAL_ERROR "The xNVSE 6.4.9 source archive has an unexpected layout.")
    endif()
    file(WRITE "${_fnv_nvse_stamp}" "${_fnv_nvse_archive_sha256}\n")
endif()

add_library(fallout_new_vegas_sdk INTERFACE)
target_include_directories(fallout_new_vegas_sdk SYSTEM INTERFACE
    "${_fnv_nvse_source_root}"
    "${_fnv_nvse_source_root}/nvse"
)
target_compile_definitions(fallout_new_vegas_sdk INTERFACE
    RUNTIME=1
    RUNTIME_VERSION=0x040020D0
)
