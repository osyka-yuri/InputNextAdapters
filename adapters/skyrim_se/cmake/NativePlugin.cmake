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

if(INPUTNEXT_SKYRIM_BUILD_TESTS)
    add_subdirectory(tests/cpp/component/native_observation)
    add_subdirectory(tests/cpp/component/native_actor)
endif()
