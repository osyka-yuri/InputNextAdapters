cmake_minimum_required(VERSION 4.4.4)
project(InputNextFalloutNewVegas VERSION 0.1.0 LANGUAGES CXX)

option(INPUTNEXT_FALLOUT_NEW_VEGAS_BUILD_TESTS "Build the New Vegas adapter tests" "${PROJECT_IS_TOP_LEVEL}")

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The New Vegas adapter requires an MSVC-compatible Windows x86 toolchain.")
endif()

# Keep the game plugin and every linked object independent of a separately installed MSVC CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProductionOptions.cmake")
inputnext_prepare_production_configuration()

if(INPUTNEXT_FALLOUT_NEW_VEGAS_BUILD_TESTS)
    set(CMAKE_TEST_BUILD_DEPENDS ON)
    enable_testing()
endif()

if(INPUTNEXTADAPTERS_RUNTIME_DIRECTORY)
    set(INPUTNEXTADAPTERS_BUILD_TESTS OFF)
    if(NOT TARGET InputNextAdapters::runtime_client)
        add_subdirectory(../.. inputnext_adapters EXCLUDE_FROM_ALL)
    endif()
    enable_testing()
    add_subdirectory("../../tests/cpp/integration/runtime_package" runtime_package)
    return()
endif()

if(NOT TARGET fallout_gamebryo_policy)
    set(INPUTNEXT_FALLOUT_GAMEBRYO_BUILD_TESTS "${INPUTNEXT_FALLOUT_NEW_VEGAS_BUILD_TESTS}")
    add_subdirectory(../fallout_gamebryo fallout_gamebryo)
endif()
include(cmake/NvseSdk.cmake)

add_library(fallout_new_vegas_capture STATIC
    src/event_reducer.cpp
    src/native_combat.cpp
)
target_include_directories(fallout_new_vegas_capture PUBLIC src)
target_link_libraries(fallout_new_vegas_capture PUBLIC fallout_gamebryo_policy PRIVATE fallout_new_vegas_sdk)
inputnext_configure_target(fallout_new_vegas_capture PRODUCTION)

add_library(InputNextFalloutNewVegas SHARED
    src/diagnostics.cpp
    src/dry_fire_hook.cpp
    src/native_events.cpp
    src/native_game.cpp
    src/native_state.cpp
    src/plugin.cpp
    src/sdk_bindings.cpp
    src/stealth_detection.cpp
    src/InputNextFalloutNewVegas.def
)
target_link_libraries(InputNextFalloutNewVegas PRIVATE
    fallout_gamebryo_session fallout_new_vegas_capture fallout_new_vegas_sdk)
set_target_properties(InputNextFalloutNewVegas PROPERTIES PREFIX "" WINDOWS_EXPORT_ALL_SYMBOLS OFF)
inputnext_configure_target(InputNextFalloutNewVegas PRODUCTION)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "InputNextFalloutNewVegas-${PROJECT_VERSION}-windows-x86")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_COMPONENTS_ALL FalloutNewVegasAdapter)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
if(CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CPACK_BUILD_CONFIG "${CMAKE_BUILD_TYPE}")
endif()

get_filename_component(_fnv_compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
find_program(_fnv_dumpbin NAMES dumpbin.exe dumpbin
    HINTS "${_fnv_compiler_directory}" REQUIRED)
set(CPACK_POST_BUILD_SCRIPTS "${CMAKE_CURRENT_BINARY_DIR}/VerifyNewVegasPackage.cmake")
configure_file(cmake/VerifyPackage.cmake.in "${CPACK_POST_BUILD_SCRIPTS}" @ONLY)
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/NewVegasPackageImage-$<CONFIG>.cmake"
    CONTENT "set(image [==[$<TARGET_FILE:InputNextFalloutNewVegas>]==])\n"
)

if(INPUTNEXT_FALLOUT_NEW_VEGAS_BUILD_TESTS)
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/InputNextCatch2.cmake")
    add_executable(fallout_new_vegas_native_tests
        src/stealth_detection.cpp
        tests/configuration_tests.cpp
        tests/native_combat_tests.cpp
        tests/native_event_reducer_tests.cpp
        tests/native_observation_tests.cpp
        tests/stealth_detection_tests.cpp
    )
    target_compile_definitions(fallout_new_vegas_native_tests PRIVATE
        "INPUTNEXT_FNV_INI_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
    target_include_directories(fallout_new_vegas_native_tests PRIVATE "${InputNextAdapters_SOURCE_DIR}/tests")
    target_link_libraries(fallout_new_vegas_native_tests PRIVATE
        fallout_new_vegas_capture fallout_new_vegas_sdk fallout_gamebryo_session Catch2::Catch2WithMain)
    inputnext_configure_target(fallout_new_vegas_native_tests)
    catch_discover_tests(fallout_new_vegas_native_tests ADD_TAGS_AS_LABELS PROPERTIES TIMEOUT 30)

    set(fallout_new_vegas_fixture "${CMAKE_CURRENT_BINARY_DIR}/loader-fixture")
    set(fallout_new_vegas_test_sources "${InputNextAdapters_SOURCE_DIR}/tests")
    add_library(fallout_new_vegas_test_runtime MODULE
        "${fallout_new_vegas_test_sources}/fake_runtime.cpp"
        "${fallout_new_vegas_test_sources}/fake_runtime.def"
    )
    set_target_properties(fallout_new_vegas_test_runtime PROPERTIES
        PREFIX ""
        OUTPUT_NAME inputnext_runtime
        LIBRARY_OUTPUT_DIRECTORY "${fallout_new_vegas_fixture}/runtime"
        RUNTIME_OUTPUT_DIRECTORY "${fallout_new_vegas_fixture}/runtime"
    )
    target_compile_definitions(fallout_new_vegas_test_runtime PRIVATE FAKE_RUNTIME_KIND=0 TEST_FIXTURE_ID=1)
    target_include_directories(fallout_new_vegas_test_runtime PRIVATE
        "${InputNextAdapters_SOURCE_DIR}/third_party/inputnext/include"
    )
    inputnext_configure_target(fallout_new_vegas_test_runtime)
    add_executable(fallout_new_vegas_load_tests tests/plugin_load_tests.cpp)
    target_include_directories(fallout_new_vegas_load_tests PRIVATE src "${fallout_new_vegas_test_sources}")
    target_link_libraries(fallout_new_vegas_load_tests PRIVATE fallout_new_vegas_sdk)
    inputnext_configure_target(fallout_new_vegas_load_tests)
    add_dependencies(fallout_new_vegas_load_tests InputNextFalloutNewVegas fallout_new_vegas_test_runtime)
    foreach(mode IN ITEMS valid-runtime missing-runtime partial-events partial-terminal-events
        missing-event-manager
        invalid-ini unreadable-ini invalid-audio-bank unreadable-audio-bank wrong-game-audio-bank disabled-audio-bank)
        add_test(NAME "fallout_new_vegas.load.${mode}" COMMAND fallout_new_vegas_load_tests
            "$<TARGET_FILE:InputNextFalloutNewVegas>" "$<TARGET_FILE:fallout_new_vegas_test_runtime>"
            "${fallout_new_vegas_fixture}/${mode}" "${mode}"
            "${InputNextAdapters_SOURCE_DIR}/resources/audio-haptics/skyrim-le.audiohaptics")
        set_tests_properties("fallout_new_vegas.load.${mode}" PROPERTIES
            TIMEOUT 15 LABELS "fallout-new-vegas;load")
    endforeach()

    set(INPUTNEXTADAPTERS_PACKAGE_PLUGIN "" CACHE FILEPATH "Extracted plugin DLL to probe")
    set(_fnv_package_plugin "$<TARGET_FILE:InputNextFalloutNewVegas>")
    if(INPUTNEXTADAPTERS_PACKAGE_PLUGIN)
        set(_fnv_package_plugin "${INPUTNEXTADAPTERS_PACKAGE_PLUGIN}")
    endif()
    add_test(NAME fallout_new_vegas_plugin_package COMMAND fallout_new_vegas_load_tests
        "${_fnv_package_plugin}" "$<TARGET_FILE:fallout_new_vegas_test_runtime>"
        "${fallout_new_vegas_fixture}/extracted-package" valid-runtime
        "${InputNextAdapters_SOURCE_DIR}/resources/audio-haptics/skyrim-le.audiohaptics")
    set_tests_properties(fallout_new_vegas_plugin_package PROPERTIES
        TIMEOUT 15 LABELS "fallout-new-vegas;load;package")

    add_test(NAME fallout_new_vegas.verify_x86_image
        COMMAND "${CMAKE_COMMAND}"
            "-Dimage=$<TARGET_FILE:InputNextFalloutNewVegas>"
            "-Dproduct_name=Fallout New Vegas plugin"
            -P "${PROJECT_SOURCE_DIR}/../../cmake/VerifyPe32Image.cmake"
    )
    set_tests_properties(fallout_new_vegas.verify_x86_image PROPERTIES
        LABELS "fallout-new-vegas;native;image")

    add_test(NAME fallout_new_vegas.verify_exports
        COMMAND "${CMAKE_COMMAND}"
            "-Dimage=$<TARGET_FILE:InputNextFalloutNewVegas>"
            "-Ddumpbin=${_fnv_dumpbin}"
            "-Dexpected_exports=NVSEPlugin_Load,NVSEPlugin_Query"
            "-Dproduct_name=Fallout New Vegas plugin"
            -P "${PROJECT_SOURCE_DIR}/../../cmake/VerifyWindowsDll.cmake"
    )
    set_tests_properties(fallout_new_vegas.verify_exports PROPERTIES
        LABELS "fallout-new-vegas;native;exports;dependencies")

    add_test(NAME fallout_new_vegas.verify_package
        COMMAND "${CMAKE_COMMAND}"
            "-Dcpack=${CMAKE_CPACK_COMMAND}"
            "-Dcpack_config=${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake"
            "-Dpackage_directory=${CMAKE_CURRENT_BINARY_DIR}/package-audit"
            "-Dpackage_filename=${CPACK_PACKAGE_FILE_NAME}.zip"
            "-Dpackage_configuration=$<CONFIG>"
            -P "${PROJECT_SOURCE_DIR}/../../cmake/RunCpackPackageCheck.cmake"
    )
    set_tests_properties(fallout_new_vegas.verify_package PROPERTIES
        TIMEOUT 60 LABELS "fallout-new-vegas;native;package")
endif()

if(INPUTNEXTADAPTERS_PRODUCTION)
    install(FILES "$<TARGET_PDB_FILE:InputNextFalloutNewVegas>"
        DESTINATION symbols COMPONENT FalloutNewVegasSymbols)
endif()

install(TARGETS InputNextFalloutNewVegas RUNTIME DESTINATION Data/NVSE/Plugins COMPONENT FalloutNewVegasAdapter)
install(FILES InputNextFalloutNewVegas.ini InputNextFalloutNewVegas.reference.ini
    DESTINATION Data/NVSE/Plugins COMPONENT FalloutNewVegasAdapter
)
install(FILES README.md "${InputNextAdapters_SOURCE_DIR}/LICENSE" DESTINATION . COMPONENT FalloutNewVegasAdapter)

include(CPack)
