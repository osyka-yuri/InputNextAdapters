include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PackageProbe.cmake")

add_library(skyrim_le_actor_values STATIC
    src/native/actor_values.cpp
)
target_include_directories(skyrim_le_actor_values PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(skyrim_le_actor_values PUBLIC skyrim_le_sdk)
skyrim_enable_checks(skyrim_le_actor_values PRODUCTION)

add_library(skyrim_le_observation STATIC
    src/native/observation.cpp
)
target_include_directories(skyrim_le_observation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(skyrim_le_observation PUBLIC
    skyrim_session skyrim_policy skyrim_le_sdk skyrim_le_actor_values)
skyrim_enable_checks(skyrim_le_observation PRODUCTION)

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

target_link_libraries(InputNextSkyrimLE PRIVATE skyrim_le_observation skyrim_config skyrim_session skyrim_le_sdk)
set_target_properties(InputNextSkyrimLE PROPERTIES
    OUTPUT_NAME "InputNextSkyrimLE"
    PREFIX ""
    SUFFIX ".dll"
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
)
skyrim_enable_checks(InputNextSkyrimLE PRODUCTION)

if(INPUTNEXTADAPTERS_PRODUCTION
        AND NOT INPUTNEXTADAPTERS_ENABLE_ASAN
        AND NOT INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE)
    install(FILES "$<TARGET_PDB_FILE:InputNextSkyrimLE>"
        DESTINATION symbols
        COMPONENT SkyrimLESymbols
    )
endif()

install(TARGETS InputNextSkyrimLE
    RUNTIME DESTINATION "Data/SKSE/Plugins"
    COMPONENT SkyrimLEAdapter
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/InputNextSkyrimLE.ini"
    "${CMAKE_CURRENT_SOURCE_DIR}/InputNextSkyrimLE.reference.ini"
    DESTINATION "Data/SKSE/Plugins"
    COMPONENT SkyrimLEAdapter
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/package-readme.md"
    DESTINATION "."
    RENAME "README.md"
    COMPONENT SkyrimLEAdapter
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES"
    DESTINATION "licenses"
    COMPONENT SkyrimLEAdapter
)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/SKSE-source-terms.txt"
    DESTINATION "licenses"
    COMPONENT SkyrimLEAdapter
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE"
    DESTINATION "licenses"
    RENAME "InputNext-MIT.txt"
    COMPONENT SkyrimLEAdapter
)

if(INPUTNEXT_SKYRIM_LE_BUILD_TESTS)
    inputnext_package_probe_argument(_skyrim_le_package_probe_plugin InputNextSkyrimLE)
    add_subdirectory(tests/cpp/integration/query)
    add_subdirectory(tests/cpp/integration/load)
    add_subdirectory(tests/cpp/component/binding)
endif()
