cmake_minimum_required(VERSION 4.4.4)
project(InputNextAdaptersSourceBundleFixture NONE)

include("${HELPER_SOURCE_DIR}/cmake/DependencySources.cmake")
inputnext_prepare_dependency_sources(skyrim-se
    DEPENDENCIES
        "fmt|https://example.invalid/fmt.git|0123456789abcdef0123456789abcdef01234567|dependencies/skyrim-se/fmt|fmt"
)

set(BUNDLE_SENTINEL "${SENTINEL}")
include(FetchContent)
set(FETCHCONTENT_FULLY_DISCONNECTED ON)
FetchContent_Declare(fmt
    GIT_REPOSITORY https://example.invalid/fmt.git
    GIT_TAG 0123456789abcdef0123456789abcdef01234567
)
FetchContent_MakeAvailable(fmt)

file(REAL_PATH "${SOURCE_ROOT}" _expected_source_root)
file(REAL_PATH "${fmt_SOURCE_DIR}" _actual_source_root)
if(NOT _actual_source_root STREQUAL _expected_source_root)
    message(FATAL_ERROR "The verified bundle did not supply the declared FetchContent source override.")
endif()
inputnext_write_dependency_source_receipt(skyrim-se
    DEPENDENCIES
        "fmt|https://example.invalid/fmt.git|0123456789abcdef0123456789abcdef01234567|${fmt_SOURCE_DIR}"
)
