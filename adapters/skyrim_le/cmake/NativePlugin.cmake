add_library(InputNextSkyrimLE SHARED
    src/InputNextSkyrimLE.def
    src/native/diagnostics.cpp
    src/native/owner.cpp
    src/native/owner/worker.cpp
    src/native/owner/tasks.cpp
    src/native/owner/subscriptions.cpp
    src/native/owner/reporting.cpp
    src/native/events.cpp
    src/plugin.cpp
)
target_include_directories(InputNextSkyrimLE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(InputNextSkyrimLE PRIVATE
    skyrim_le_observation skyrim_config skyrim_session skyrim_le_sdk)
set_target_properties(InputNextSkyrimLE PROPERTIES
    OUTPUT_NAME "InputNextSkyrimLE"
    PREFIX ""
    SUFFIX ".dll"
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
)
skyrim_enable_checks(InputNextSkyrimLE PRODUCTION)

if(INPUTNEXT_SKYRIM_LE_BUILD_TESTS)
    add_subdirectory(tests/cpp/unit/diagnostics)
    set(_skyrim_le_package_probe_plugin "$<TARGET_FILE:InputNextSkyrimLE>")
    add_subdirectory(tests/cpp/integration/query)
    add_subdirectory(tests/cpp/integration/load)
endif()
