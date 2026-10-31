cmake_minimum_required(VERSION 4.4.4)
project(InputNextAdaptersNativeDependenciesDeclarationFixture NONE)

if(NOT DEFINED HELPER_SOURCE_DIR)
    message(FATAL_ERROR "HELPER_SOURCE_DIR is required.")
endif()

include("${HELPER_SOURCE_DIR}/cmake/NativeDependencies.cmake")
inputnext_declare_native_dependency(skyrim-se fmt OVERRIDE_FIND_PACKAGE)
inputnext_declare_native_dependency(skyrim-se rapidcsv SOURCE_SUBDIR _none)
inputnext_declare_native_dependency(fallout4 commonlibf4 RECURSE_SUBMODULES)
