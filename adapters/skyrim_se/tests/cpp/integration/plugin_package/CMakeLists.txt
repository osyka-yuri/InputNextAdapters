add_executable(skyrim_plugin_package_probe plugin_package_probe.cpp)
skyrim_enable_checks(skyrim_plugin_package_probe)
add_dependencies(skyrim_plugin_package_probe InputNextSkyrim)

add_test(NAME skyrim_plugin_package
    COMMAND skyrim_plugin_package_probe "${_skyrim_package_probe_plugin}"
)
set_tests_properties(skyrim_plugin_package PROPERTIES
    TIMEOUT 30
    LABELS "integration;skyrim_se;package"
)
