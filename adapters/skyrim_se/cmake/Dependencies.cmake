include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/DependencySources.cmake")

# Keep these source pins in sync with THIRD_PARTY_NOTICES.
# The plugin is opt-in in the adapter's top-level CMake file; selecting it always builds these pins.
set(BUILD_TESTS OFF)
set(ENABLE_SKYRIM_SE ON)
set(ENABLE_SKYRIM_AE ON)
set(ENABLE_SKYRIM_VR OFF)
set(SKSE_SUPPORT_PATCH_SAFETY OFF)
set(COMMONLIB_ENABLE_IPO OFF)
set(FMT_INSTALL ON)
set(SPDLOG_FMT_EXTERNAL ON)
set(SPDLOG_INSTALL ON)
set(BUILD_TOOLS OFF)
set(BUILD_XAUDIO_WIN10 OFF)
set(BUILD_XAUDIO_WIN8 OFF)
set(BUILD_XAUDIO_REDIST OFF)

inputnext_declare_native_dependency(skyrim-se fmt OVERRIDE_FIND_PACKAGE)
inputnext_declare_native_dependency(skyrim-se spdlog OVERRIDE_FIND_PACKAGE)
inputnext_declare_native_dependency(skyrim-se rapidcsv SOURCE_SUBDIR _none)
inputnext_declare_native_dependency(skyrim-se directxtk OVERRIDE_FIND_PACKAGE)
inputnext_declare_native_dependency(skyrim-se commonlibsse)

inputnext_native_dependency_metadata(skyrim-se fmt _fmt_dependency)
inputnext_native_dependency_metadata(skyrim-se spdlog _spdlog_dependency)
inputnext_native_dependency_metadata(skyrim-se rapidcsv _rapidcsv_dependency)
inputnext_native_dependency_metadata(skyrim-se directxtk _directxtk_dependency)
inputnext_native_dependency_metadata(skyrim-se commonlibsse _commonlibsse_dependency)

inputnext_prepare_dependency_sources(skyrim-se)

FetchContent_MakeAvailable(
    "${_fmt_dependency_FETCHCONTENT}"
    "${_spdlog_dependency_FETCHCONTENT}"
    "${_rapidcsv_dependency_FETCHCONTENT}"
    "${_directxtk_dependency_FETCHCONTENT}"
)
if(NOT TARGET Microsoft::DirectXTK)
    add_library(Microsoft::DirectXTK ALIAS DirectXTK)
endif()
set(RAPIDCSV_INCLUDE_DIRS "${rapidcsv_SOURCE_DIR}/src")
FetchContent_MakeAvailable("${_commonlibsse_dependency_FETCHCONTENT}")

inputnext_write_dependency_source_receipt(skyrim-se)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    get_target_property(_skyrim_commonlib_interface_options CommonLibSSE INTERFACE_COMPILE_OPTIONS)
    list(REMOVE_ITEM _skyrim_commonlib_interface_options "/Zc:preprocessor")
    set_property(TARGET CommonLibSSE PROPERTY INTERFACE_COMPILE_OPTIONS
        "${_skyrim_commonlib_interface_options}"
    )
endif()

if(INPUTNEXT_SKYRIM_ENABLE_ASAN)
    foreach(_skyrim_source_target IN ITEMS fmt spdlog DirectXTK CommonLibSSE)
        inputnext_enable_asan(${_skyrim_source_target})
    endforeach()
endif()

# CommonLibSSE's source-build branch does not include this helper for consumers.
# Its own live-consumer example includes the helper from the fetched source tree.
get_target_property(_skyrim_commonlib_source_dir CommonLibSSE SOURCE_DIR)
include("${_skyrim_commonlib_source_dir}/cmake/CommonLibSSE.cmake")
