include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/FoseSdk.cmake")

add_library(InputNextFallout3 SHARED
    src/InputNextFallout3.def
    src/plugin.cpp
)

target_include_directories(InputNextFallout3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(InputNextFallout3 PRIVATE fallout3_fose_sdk)
set_target_properties(InputNextFallout3 PROPERTIES
    OUTPUT_NAME "InputNextFallout3"
    PREFIX ""
    SUFFIX ".dll"
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
fallout3_enable_checks(InputNextFallout3 PRODUCTION)

get_filename_component(_fallout3_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(_fallout3_dumpbin NAMES dumpbin.exe dumpbin
    HINTS "${_fallout3_compiler_directory}" REQUIRED)

install(TARGETS InputNextFallout3
    RUNTIME DESTINATION "Data/FOSE/Plugins"
    COMPONENT Fallout3Adapter
)
install(FILES README.md "${INPUTNEXT_FALLOUT3_REPOSITORY_ROOT}/LICENSE"
    DESTINATION .
    COMPONENT Fallout3Adapter
)

set(_fallout3_package_root "${CMAKE_CURRENT_BINARY_DIR}/package")
set(_fallout3_package_archive
    "${_fallout3_package_root}/InputNextFallout3-${PROJECT_VERSION}-windows-x86.zip")
add_custom_target(fallout3_package
    COMMAND "${CMAKE_COMMAND}"
        "-Dinstall_build_dir=${CMAKE_BINARY_DIR}"
        "-Dpackage_archive=${_fallout3_package_archive}"
        "-Dimage=$<TARGET_FILE:InputNextFallout3>"
        "-Ddumpbin=${_fallout3_dumpbin}"
        "-Dconfiguration=$<CONFIG>"
        -P "${CMAKE_CURRENT_LIST_DIR}/MakePackage.cmake"
    DEPENDS InputNextFallout3
    COMMENT "Packing the Fallout 3 FOSE plugin"
    VERBATIM
)
