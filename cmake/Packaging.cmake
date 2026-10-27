function(inputnext_install_package)
    include(CMakePackageConfigHelpers)

    set(INPUTNEXTADAPTERS_PACKAGE_REQUIRES_CLANGCL_ASAN_RUNTIME OFF)
    if(INPUTNEXTADAPTERS_ENABLE_ASAN AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(INPUTNEXTADAPTERS_PACKAGE_REQUIRES_CLANGCL_ASAN_RUNTIME ON)
    endif()
    set(INPUTNEXTADAPTERS_PACKAGE_REQUIRES_CLANGCL_UBSAN_RUNTIME OFF)
    if(INPUTNEXTADAPTERS_ENABLE_UBSAN)
        set(INPUTNEXTADAPTERS_PACKAGE_REQUIRES_CLANGCL_UBSAN_RUNTIME ON)
    endif()

    set(package_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/InputNextAdapters")
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/package")
    file(MAKE_DIRECTORY "${generated_dir}")

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InputNextAdaptersConfig.cmake.in"
        "${generated_dir}/InputNextAdaptersConfig.cmake"
        INSTALL_DESTINATION "${package_cmake_dir}"
    )
    write_basic_package_version_file(
        "${generated_dir}/InputNextAdaptersConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )

    install(TARGETS ${ARGN}
        EXPORT InputNextAdaptersTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        COMPONENT InputNextAdaptersBinary
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        COMPONENT InputNextAdaptersBinary
        FILES_MATCHING PATTERN "*.hpp"
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inputnext/include/input_next_effects.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inputnext/include/input_next_core.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/inputnext_adapters/sdk"
        COMPONENT InputNextAdaptersBinary
    )
    install(EXPORT InputNextAdaptersTargets
        FILE InputNextAdaptersTargets.cmake
        NAMESPACE InputNextAdapters::
        DESTINATION "${package_cmake_dir}"
        COMPONENT InputNextAdaptersBinary
    )
    install(FILES
        "${generated_dir}/InputNextAdaptersConfig.cmake"
        "${generated_dir}/InputNextAdaptersConfigVersion.cmake"
        DESTINATION "${package_cmake_dir}"
        COMPONENT InputNextAdaptersBinary
    )
    if(INPUTNEXTADAPTERS_PACKAGE_REQUIRES_CLANGCL_ASAN_RUNTIME)
        install(FILES "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ClangClAsan.cmake"
            DESTINATION "${package_cmake_dir}"
            COMPONENT InputNextAdaptersBinary
        )
    endif()
    if(INPUTNEXTADAPTERS_PACKAGE_REQUIRES_CLANGCL_UBSAN_RUNTIME)
        install(FILES "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ClangClUbsan.cmake"
            DESTINATION "${package_cmake_dir}"
            COMPONENT InputNextAdaptersBinary
        )
    endif()

    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/InputNextAdapters"
        COMPONENT InputNextAdaptersBinary
    )
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inputnext/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/InputNextAdapters/inputnext"
        COMPONENT InputNextAdaptersBinary
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inputnext/manifest.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inputnext/UPSTREAM.md"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/InputNextAdapters/inputnext"
        COMPONENT InputNextAdaptersBinary
    )

    if(INPUTNEXTADAPTERS_PRODUCTION AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/symbols/$<CONFIG>/InputNextAdapters-runtime-client.pdb"
            "${CMAKE_CURRENT_BINARY_DIR}/symbols/$<CONFIG>/InputNextAdapters-effects.pdb"
            DESTINATION symbols
            COMPONENT InputNextAdaptersSymbols
            CONFIGURATIONS Release
        )
    endif()
endfunction()
