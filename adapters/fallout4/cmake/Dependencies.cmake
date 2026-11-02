include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/DependencySources.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CommonLibF4Patches.cmake")

set(SPDLOG_BUILD_SHARED OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_INSTALL ON)
set(SPDLOG_USE_STD_FORMAT ON)
set(SPDLOG_FMT_EXTERNAL OFF)
set(SPDLOG_FMT_EXTERNAL_HO OFF)
set(SPDLOG_WCHAR_SUPPORT ON)

inputnext_declare_native_dependency(fallout4 commonlibf4 RECURSE_SUBMODULES)
inputnext_declare_native_dependency(fallout4 spdlog OVERRIDE_FIND_PACKAGE)
inputnext_native_dependency_metadata(fallout4 commonlibf4 _fallout4_commonlibf4)
inputnext_native_dependency_metadata(fallout4 spdlog _fallout4_spdlog)

inputnext_prepare_dependency_sources(fallout4)

FetchContent_MakeAvailable(
    "${_fallout4_commonlibf4_FETCHCONTENT}"
    "${_fallout4_spdlog_FETCHCONTENT}"
)

fallout4_patch_commonlibf4("${fallout4_commonlibf4_SOURCE_DIR}")

if(NOT EXISTS "${fallout4_commonlibf4_SOURCE_DIR}/lib/commonlib-shared/CMakeLists.txt")
    message(FATAL_ERROR "The pinned CommonLibF4 shared-library submodule was not populated.")
endif()

if(NOT INPUTNEXTADAPTERS_SOURCE_BUNDLE_VALIDATED)
    foreach(_submodule_spec IN LISTS _fallout4_commonlibf4_SUBMODULE_SPECS)
        string(REPLACE "|" ";" _submodule_parts "${_submodule_spec}")
        list(GET _submodule_parts 1 _submodule_name)
        list(GET _submodule_parts 2 _submodule_path)
        list(GET _submodule_parts 4 _submodule_revision)
        inputnext_require_source_git_revision(
            "${fallout4_commonlibf4_SOURCE_DIR}/${_submodule_path}"
            "${_submodule_revision}"
            "CommonLibF4's ${_submodule_name} submodule"
        )
    endforeach()
endif()

inputnext_write_dependency_source_receipt(fallout4)

add_subdirectory(
    "${fallout4_commonlibf4_SOURCE_DIR}/lib/commonlib-shared"
    "${fallout4_commonlibf4_BINARY_DIR}/commonlib-shared"
)

file(GLOB_RECURSE _fallout4_commonlibf4_sources CONFIGURE_DEPENDS
    "${fallout4_commonlibf4_SOURCE_DIR}/src/*.cpp"
)
if(NOT _fallout4_commonlibf4_sources)
    message(FATAL_ERROR "No CommonLibF4 C++ sources were found at the pinned revision.")
endif()

add_library(fallout4_commonlibf4 STATIC ${_fallout4_commonlibf4_sources})
add_library(Fallout4::CommonLibF4 ALIAS fallout4_commonlibf4)
target_compile_features(fallout4_commonlibf4 PUBLIC cxx_std_23)
target_include_directories(fallout4_commonlibf4 PUBLIC
    "$<BUILD_INTERFACE:${fallout4_commonlibf4_SOURCE_DIR}/include>"
)
target_link_libraries(fallout4_commonlibf4 PUBLIC commonlib-shared::commonlib-shared)
target_precompile_headers(fallout4_commonlibf4 PRIVATE
    "${fallout4_commonlibf4_SOURCE_DIR}/include/F4SE/Impl/PCH.h"
)
set_target_properties(fallout4_commonlibf4 PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    SYSTEM TRUE
)

foreach(_fallout4_vendor_target IN ITEMS commonlib-shared spdlog::spdlog)
    if(TARGET "${_fallout4_vendor_target}")
        get_target_property(_fallout4_real_target "${_fallout4_vendor_target}" ALIASED_TARGET)
        if(_fallout4_real_target)
            set_property(TARGET "${_fallout4_real_target}" PROPERTY SYSTEM TRUE)
        else()
            set_property(TARGET "${_fallout4_vendor_target}" PROPERTY SYSTEM TRUE)
            set(_fallout4_real_target "${_fallout4_vendor_target}")
        endif()
        inputnext_scope_vendor_warning_suppressions("${_fallout4_real_target}")
    endif()
endforeach()
