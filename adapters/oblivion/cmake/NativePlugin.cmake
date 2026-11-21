include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/Dependencies.cmake")

add_library(InputNextOblivion SHARED
    src/InputNextOblivion.def
    src/native/diagnostics.cpp
    src/native/effect_worker.cpp
    src/native/player_state.cpp
    src/native/magic_forms.cpp
    src/native/event_handler.cpp
    src/native/task_handler.cpp
    src/native/interfaces.cpp
    src/native/sdk_compat.cpp
    src/plugin.cpp
)

target_include_directories(InputNextOblivion PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(InputNextOblivion PRIVATE oblivion_observation oblivion_effects oblivion_sdk)

set_target_properties(InputNextOblivion PROPERTIES
    OUTPUT_NAME "InputNextOblivion"
    PREFIX ""
    SUFFIX ".dll"
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
)

oblivion_enable_checks(InputNextOblivion PRODUCTION)

if(INPUTNEXT_OBLIVION_BUILD_TESTS)
    target_compile_definitions(InputNextOblivion PRIVATE INPUTNEXT_OBLIVION_ENABLE_TEST_EXPORTS)
endif()

if(INPUTNEXTADAPTERS_PRODUCTION
        AND NOT INPUTNEXTADAPTERS_ENABLE_ASAN
        AND NOT INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE)
    install(FILES "$<TARGET_PDB_FILE:InputNextOblivion>"
        DESTINATION symbols
        COMPONENT OblivionSymbols
    )
endif()

install(TARGETS InputNextOblivion
    RUNTIME DESTINATION "Data/OBSE/Plugins"
    COMPONENT OblivionAdapter
)
install(FILES InputNextOblivion.ini InputNextOblivion.reference.ini
    DESTINATION "Data/OBSE/Plugins"
    COMPONENT OblivionAdapter
)
install(FILES README.md "${InputNextAdapters_SOURCE_DIR}/LICENSE"
    DESTINATION .
    COMPONENT OblivionAdapter
)
