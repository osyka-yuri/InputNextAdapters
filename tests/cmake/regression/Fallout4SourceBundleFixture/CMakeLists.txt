cmake_minimum_required(VERSION 4.4.4)
project(InputNextAdaptersFallout4SourceBundleFixture NONE)

include("${HELPER_SOURCE_DIR}/cmake/DependencySources.cmake")
inputnext_prepare_dependency_sources(fallout4
    DEPENDENCIES
        "commonlibf4|https://github.com/libxse/commonlibf4.git|7c8c6f8a349ac85abf6fc8a7e5a71732d9339954|dependencies/fallout4/commonlibf4|fallout4_commonlibf4"
        "spdlog|https://github.com/gabime/spdlog.git|79524ddd08a4ec981b7fea76afd08ee05f83755d|dependencies/fallout4/spdlog|spdlog"
    PATCHES
        "commonlibf4|inputnext-commonlibf4-animation-manager-destructor-v1|include/RE/B/BSAnimationGraphManager.h|5c483703ba54a10a7ec051109072acfd866ea24376dad3a59ad8f93729a3facb,59977a1acf8400caab11ee071e617c7da118da3c7ba6c50cfc84173134265cba"
    SUBMODULES
        "commonlibf4|commonlib-shared|lib/commonlib-shared|https://github.com/libxse/commonlib-shared.git|29fbdb0e2dc548c9ab22f6964981d75090dc9094"
)

include(FetchContent)
set(FETCHCONTENT_FULLY_DISCONNECTED ON)
FetchContent_Declare(fallout4_commonlibf4 GIT_REPOSITORY https://example.invalid/commonlibf4.git)
FetchContent_Declare(spdlog GIT_REPOSITORY https://example.invalid/spdlog.git)
FetchContent_MakeAvailable(fallout4_commonlibf4 spdlog)
