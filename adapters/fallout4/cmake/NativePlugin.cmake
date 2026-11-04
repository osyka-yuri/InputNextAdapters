if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR "The native Fallout 4 plugin requires the Microsoft C++ compiler; clang-cl is supported for adapter tests only.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PackageProbe.cmake")

set(COMMONLIB_PLUGIN_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(COMMONLIB_PLUGIN_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(COMMONLIB_PLUGIN_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(COMMONLIB_PLUGIN_NAME "InputNextFallout4")
set(COMMONLIB_PLUGIN_AUTHOR "InputNext")
set(COMMONLIB_PLUGIN_XSE_VERSION_MAJOR 0)
set(COMMONLIB_PLUGIN_XSE_VERSION_MINOR 7)
set(COMMONLIB_PLUGIN_XSE_VERSION_PATCH 9)
set(COMMONLIBF4_OPTION_SIG_SCANNING false)
set(COMMONLIBF4_OPTION_ADDRESS_LIBRARY true)
set(COMMONLIBF4_OPTION_NO_STRUCT_USE false)
set(COMMONLIBF4_OPTION_LAYOUT_DEPENDENT true)

set(_fallout4_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_fallout4_generated_dir}")
file(STRINGS
    "${fallout4_commonlibf4_SOURCE_DIR}/include/F4SE/Version.h"
    _fallout4_latest_runtime_declaration
    REGEX "RUNTIME_LATEST = RUNTIME_1_11_240;"
)
if(NOT _fallout4_latest_runtime_declaration)
    message(FATAL_ERROR
        "The pinned plugin metadata requires the checked Fallout 4 runtime 1.11.240. "
        "Review the version pin and compatibility declaration before changing it.")
endif()
configure_file(
    "${fallout4_commonlibf4_SOURCE_DIR}/res/commonlibf4-plugin.cpp.in"
    "${_fallout4_generated_dir}/commonlibf4-plugin.cpp"
)

add_library(InputNextFallout4 SHARED
    "${_fallout4_generated_dir}/commonlibf4-plugin.cpp"
    src/plugin.cpp
    src/native/dispatch.cpp
    src/native/owner.cpp
    src/native/owner_services.cpp
    src/native/owner_pipeline.cpp
    src/native/owner_mailbox.cpp
    src/native/observation_requests.cpp
    src/native/observation.cpp
    src/native/animation_subscription.cpp
    src/native/events.cpp
    src/native/radiation_source.cpp
    src/native/bindings.cpp
    src/native/led_bindings.cpp
    src/native/ui_capture.cpp
)
target_include_directories(InputNextFallout4 PRIVATE src)

target_link_libraries(InputNextFallout4 PRIVATE
    fallout4_config
    fallout4_session
    Fallout4::CommonLibF4
)
fallout4_enable_checks(InputNextFallout4 PRODUCTION)
target_precompile_headers(InputNextFallout4 PRIVATE <F4SE/Impl/PCH.h>)
set_target_properties(InputNextFallout4 PROPERTIES
    OUTPUT_NAME "InputNextFallout4"
    PREFIX ""
    SUFFIX ".dll"
)

if(INPUTNEXTADAPTERS_PRODUCTION
        AND NOT INPUTNEXTADAPTERS_ENABLE_ASAN
        AND NOT INPUTNEXTADAPTERS_ENABLE_UBSAN
        AND NOT INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE)
    install(FILES "$<TARGET_PDB_FILE:InputNextFallout4>"
        DESTINATION symbols
        COMPONENT Fallout4Symbols
    )
endif()

if(INPUTNEXT_FALLOUT4_BUILD_TESTS)
    inputnext_package_probe_argument(_fallout4_package_probe_plugin InputNextFallout4)
    add_subdirectory(tests/cpp/component/native_ui_capture)

    add_subdirectory(tests/cpp/component/native_dispatch)

    add_subdirectory(tests/cpp/component/native_owner)

    add_subdirectory(tests/cpp/unit/native_hit)
    add_subdirectory(tests/cpp/unit/native_led)
    add_subdirectory(tests/cpp/integration/plugin_package)
endif()

install(TARGETS InputNextFallout4
    RUNTIME DESTINATION "Data/F4SE/Plugins"
    COMPONENT Fallout4Adapter
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES"
    DESTINATION "licenses"
    COMPONENT Fallout4Adapter
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/package-readme.md"
    DESTINATION "."
    RENAME README.md
    COMPONENT Fallout4Adapter
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE"
    DESTINATION "licenses"
    RENAME InputNext-MIT.txt
    COMPONENT Fallout4Adapter
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/InputNextFallout4.ini"
    "${CMAKE_CURRENT_SOURCE_DIR}/InputNextFallout4.reference.ini"
    DESTINATION "Data/F4SE/Plugins"
    COMPONENT Fallout4Adapter
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/CommonLibF4-GPL-3.0.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/CommonLibF4-EXCEPTIONS.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/CommonLibShared-GPL-3.0.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/CommonLibShared-EXCEPTIONS.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/spdlog-MIT.txt"
    DESTINATION "licenses"
    COMPONENT Fallout4Adapter
)
