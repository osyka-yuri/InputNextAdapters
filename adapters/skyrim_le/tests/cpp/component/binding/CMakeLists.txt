add_executable(skyrim_le_binding_tests
    binding_test_main.cpp
    binding_translation_tests.cpp
    binding_menu_tests.cpp
    binding_abi_tests.cpp
)
target_include_directories(skyrim_le_binding_tests PRIVATE
    "${InputNextSkyrimLE_SOURCE_DIR}/src"
    "${InputNextAdapters_SOURCE_DIR}/tests"
)
target_link_libraries(skyrim_le_binding_tests PRIVATE
    skyrim_policy
    skyrim_le_sdk
    skyrim_le_actor_values
)
skyrim_enable_checks(skyrim_le_binding_tests)
add_test(NAME skyrim_le_binding COMMAND skyrim_le_binding_tests)
add_test(NAME skyrim_le_binding.translators COMMAND skyrim_le_binding_tests --case translators)
add_test(NAME skyrim_le_binding.menus COMMAND skyrim_le_binding_tests --case menus)
add_test(NAME skyrim_le_binding.abi COMMAND skyrim_le_binding_tests --case abi)
set_tests_properties(
    skyrim_le_binding
    skyrim_le_binding.translators
    skyrim_le_binding.menus
    skyrim_le_binding.abi
    PROPERTIES
    TIMEOUT 30
    LABELS "component;skyrim_le;native;binding"
)
