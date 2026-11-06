include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PackageProbe.cmake")

# Keep third-party headers out of this adapter's /W4 /WX warning set.
function(skyrim_mark_vendor_headers_system target_name)
    if(NOT TARGET "${target_name}")
        return()
    endif()
    get_target_property(_skyrim_real_target "${target_name}" ALIASED_TARGET)
    if(_skyrim_real_target)
        set(target_name "${_skyrim_real_target}")
    endif()
    set_property(TARGET "${target_name}" PROPERTY SYSTEM TRUE)
    inputnext_scope_vendor_warning_suppressions("${target_name}")
endfunction()

foreach(_skyrim_vendor_target IN ITEMS CommonLibSSE::CommonLibSSE spdlog::spdlog fmt::fmt Microsoft::DirectXTK)
    skyrim_mark_vendor_headers_system("${_skyrim_vendor_target}")
endforeach()

add_library(skyrim_native_observation STATIC
    src/native/observation.cpp
    src/native/content_observation.cpp
)
target_include_directories(skyrim_native_observation
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(skyrim_native_observation PUBLIC skyrim_policy CommonLibSSE::CommonLibSSE)
target_precompile_headers(skyrim_native_observation PRIVATE <SKSE/Impl/PCH.h>)
skyrim_enable_checks(skyrim_native_observation PRODUCTION)

add_commonlibsse_plugin(InputNextSkyrim
    NAME "InputNext Skyrim"
    AUTHOR "InputNext"
    VERSION "${PROJECT_VERSION}"
    COMPATIBLE_RUNTIMES
        # Steam (x.x.x.0)
        "1.5.97.0"
        "1.6.1170.0"
        "1.6.1179.0"
        "1.7.104.0"
        # GOG (x.x.x.1)
        "1.6.659.1"
        "1.6.1170.1"
        "1.6.1179.1"
        # Epic Games Store (x.x.x.2)
        "1.6.678.2"
    SOURCES
        src/plugin.cpp
        src/native/owner.cpp
        src/native/owner/worker.cpp
        src/native/owner/tasks.cpp
        src/native/owner/subscriptions.cpp
        src/native/events.cpp
)
target_link_libraries(InputNextSkyrim PRIVATE skyrim_session skyrim_config skyrim_native_observation)
target_precompile_headers(InputNextSkyrim PRIVATE "src/native/plugin_pch.hpp")
set_target_properties(InputNextSkyrim PROPERTIES
    OUTPUT_NAME "InputNextSkyrim"
    PREFIX ""
    SUFFIX ".dll"
)
skyrim_enable_checks(InputNextSkyrim PRODUCTION)

if(INPUTNEXTADAPTERS_PRODUCTION
        AND NOT INPUTNEXTADAPTERS_ENABLE_ASAN
        AND NOT INPUTNEXTADAPTERS_ENABLE_UBSAN
        AND NOT INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE)
    install(FILES "$<TARGET_PDB_FILE:InputNextSkyrim>"
        DESTINATION symbols
        COMPONENT SkyrimSymbols
    )
endif()

install(TARGETS InputNextSkyrim
    RUNTIME DESTINATION "Data/SKSE/Plugins"
    COMPONENT SkyrimAdapter
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/InputNextSkyrim.ini"
    "${CMAKE_CURRENT_SOURCE_DIR}/InputNextSkyrim.reference.ini"
    DESTINATION "Data/SKSE/Plugins"
    COMPONENT SkyrimAdapter
)

if(INPUTNEXT_SKYRIM_BUILD_TESTS)
    inputnext_package_probe_argument(_skyrim_package_probe_plugin InputNextSkyrim)
    add_subdirectory(tests/cpp/component/native_observation)
    add_subdirectory(tests/cpp/component/native_actor)
    add_subdirectory(tests/cpp/integration/plugin_package)
endif()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/package-readme.md"
    DESTINATION "."
    RENAME "README.md"
    COMPONENT SkyrimAdapter
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES"
    DESTINATION "licenses"
    COMPONENT SkyrimAdapter
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/InputNext-MIT.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/CommonLibSSE-COPYING.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/CommonLibSSE-EXCEPTIONS.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/CommonLibSSE-MIT.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/fmt-LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/spdlog-LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/rapidcsv-LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/DirectXTK-LICENSE"
    DESTINATION "licenses"
    COMPONENT SkyrimAdapter
)
