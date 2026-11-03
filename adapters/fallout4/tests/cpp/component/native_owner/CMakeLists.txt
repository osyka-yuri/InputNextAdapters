add_executable(fallout4_native_owner_component_tests
    admission_withdrawal_tests.cpp
    request_lifecycle_tests.cpp
    startup_lifetime_tests.cpp
    worker_iteration_tests.cpp
    support.cpp
    support.hpp
    "${InputNextFallout4_SOURCE_DIR}/tests/cpp/component/native_dispatch/support.cpp"
    "${InputNextFallout4_SOURCE_DIR}/tests/cpp/component/native_ui_capture/ui_capture_test.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/dispatch.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/owner.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/owner_services.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/owner_pipeline.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/owner_mailbox.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/observation_requests.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/events.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/observation.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/radiation_source.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/animation_subscription.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/bindings.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/led_bindings.cpp"
    "${InputNextFallout4_SOURCE_DIR}/src/native/ui_capture.cpp"
)

target_include_directories(fallout4_native_owner_component_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${InputNextFallout4_SOURCE_DIR}/tests"
    "${InputNextFallout4_SOURCE_DIR}/src"
    "${InputNextAdapters_SOURCE_DIR}/tests"
)

target_link_libraries(fallout4_native_owner_component_tests PRIVATE
    Catch2::Catch2WithMain
    fallout4_config
    fallout4_session
    Fallout4::CommonLibF4
)

fallout4_enable_checks(fallout4_native_owner_component_tests)
target_precompile_headers(fallout4_native_owner_component_tests PRIVATE <F4SE/Impl/PCH.h>)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/results")
catch_discover_tests(fallout4_native_owner_component_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 60
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/results"
    OUTPUT_SUFFIX .txt
)
