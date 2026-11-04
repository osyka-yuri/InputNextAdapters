add_executable(fallout4_plugin_package_probe plugin_package_probe.cpp)
fallout4_enable_checks(fallout4_plugin_package_probe)
add_dependencies(fallout4_plugin_package_probe InputNextFallout4)
add_test(NAME fallout4_plugin_package
    COMMAND fallout4_plugin_package_probe "${_fallout4_package_probe_plugin}"
)
set_tests_properties(fallout4_plugin_package PROPERTIES
    TIMEOUT 30
    LABELS "integration;fallout4;package"
)
