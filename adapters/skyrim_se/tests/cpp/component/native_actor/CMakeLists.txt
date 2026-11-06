add_executable(skyrim_native_actor_tests native_actor_tests.cpp)
target_include_directories(skyrim_native_actor_tests PRIVATE
    "${InputNextAdapters_SOURCE_DIR}/tests"
)
target_link_libraries(skyrim_native_actor_tests PRIVATE skyrim_native_observation)
target_compile_definitions(skyrim_native_actor_tests PRIVATE ENABLE_COMMONLIBSSE_TESTING=1)
target_precompile_headers(skyrim_native_actor_tests PRIVATE <SKSE/Impl/PCH.h>)
skyrim_enable_checks(skyrim_native_actor_tests)
add_test(NAME skyrim_native_actor COMMAND skyrim_native_actor_tests)
set_tests_properties(skyrim_native_actor PROPERTIES
    LABELS "component;skyrim_se;native;actor"
)
