add_executable(oblivion_load_tests
    load_tests.cpp
    load_scenarios.cpp
    mock_xobse.cpp
)

# Put the fake runtime beside the load test executable, where the plugin resolves it.
add_library(oblivion_load_test_runtime MODULE
    fake_runtime.cpp
    fake_runtime.def
)
set_target_properties(oblivion_load_test_runtime PROPERTIES
    OUTPUT_NAME "inputnext_runtime"
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:oblivion_load_tests>"
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:oblivion_load_tests>"
)
target_include_directories(oblivion_load_test_runtime PRIVATE
    "${InputNextAdapters_SOURCE_DIR}/third_party/inputnext/include"
)
oblivion_enable_checks(oblivion_load_test_runtime)

target_include_directories(oblivion_load_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${InputNextAdapters_SOURCE_DIR}/tests"
)

target_link_libraries(oblivion_load_tests PRIVATE oblivion_sdk)
oblivion_enable_checks(oblivion_load_tests)

add_dependencies(oblivion_load_tests InputNextOblivion oblivion_load_test_runtime)
add_test(NAME oblivion_load COMMAND oblivion_load_tests "$<TARGET_FILE:InputNextOblivion>")
set_tests_properties(oblivion_load PROPERTIES
    TIMEOUT 30
    LABELS "integration;oblivion;native;load"
)

add_executable(oblivion_worker_shutdown_tests
    worker_shutdown_tests.cpp
    "${PROJECT_SOURCE_DIR}/src/native/diagnostics.cpp"
    "${PROJECT_SOURCE_DIR}/src/native/effect_worker.cpp"
)
target_include_directories(oblivion_worker_shutdown_tests PRIVATE "${InputNextAdapters_SOURCE_DIR}/tests")
target_link_libraries(oblivion_worker_shutdown_tests PRIVATE oblivion_effects)
oblivion_enable_checks(oblivion_worker_shutdown_tests)
add_dependencies(oblivion_worker_shutdown_tests oblivion_load_test_runtime)
foreach(status IN ITEMS busy invalid)
    add_test(NAME oblivion_worker_shutdown_${status} COMMAND oblivion_worker_shutdown_tests
        "$<TARGET_FILE:oblivion_load_test_runtime>" "close_${status}")
    set_tests_properties(oblivion_worker_shutdown_${status} PROPERTIES
        TIMEOUT 6
        LABELS "integration;oblivion;native;effects"
    )
endforeach()
