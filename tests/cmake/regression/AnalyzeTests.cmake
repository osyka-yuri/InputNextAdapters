include_guard(GLOBAL)

function(_expect_analyze_result name build_directory source_pattern target configuration expected_result)
    set(TEST_EXPECTED_FAILURE_MESSAGE "")
    set(TEST_HIDE_NINJA OFF)
    if(ARGC GREATER 6)
        set(TEST_EXPECTED_FAILURE_MESSAGE "${ARGV6}")
    endif()
    if(ARGC GREATER 7 AND ARGV7)
        set(TEST_HIDE_NINJA ON)
        set(TEST_NINJA_UNAVAILABLE_PATH "${sandbox}/ninja-unavailable")
        file(MAKE_DIRECTORY "${TEST_NINJA_UNAVAILABLE_PATH}")
        file(TO_CMAKE_PATH "${TEST_NINJA_UNAVAILABLE_PATH}" TEST_NINJA_UNAVAILABLE_PATH)
    else()
        set(TEST_NINJA_UNAVAILABLE_PATH "")
    endif()
    file(TO_CMAKE_PATH "${INPUTNEXTADAPTERS_SOURCE_DIR}" TEST_SOURCE_DIRECTORY)
    file(TO_CMAKE_PATH "${build_directory}" TEST_BUILD_DIRECTORY)
    set(TEST_SOURCE_PATTERN "${source_pattern}")
    set(TEST_TARGET "${target}")
    set(TEST_CONFIGURATION "${configuration}")
    set(script_path "${sandbox}/${name}.cmake")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/AnalyzeAssertion.cmake.in"
        "${script_path}"
        @ONLY
    )
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -P "${script_path}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(expected_result STREQUAL "success")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Analyze regression '${name}' unexpectedly failed with ${result}.")
        endif()
    elseif(result EQUAL 0)
        message(FATAL_ERROR "Analyze regression '${name}' unexpectedly succeeded.")
    elseif(NOT TEST_EXPECTED_FAILURE_MESSAGE STREQUAL "" AND
           NOT "${output}\n${error}" MATCHES "${TEST_EXPECTED_FAILURE_MESSAGE}")
        message(FATAL_ERROR "Analyze regression '${name}' failed for the wrong reason: ${output}${error}")
    endif()
endfunction()

function(_write_compile_database build_directory contents)
    file(MAKE_DIRECTORY "${build_directory}")
    file(WRITE "${build_directory}/compile_commands.json" "${contents}")
endfunction()

file(TO_CMAKE_PATH "${sandbox}" sandbox)
message(STATUS "[RegressionTest] Checking target-specific MSVC /analyze commands...")
set(shared_pattern "shared_source[.]cpp")

set(order_alpha_first "${sandbox}/analyze-alpha-first")
_write_compile_database("${order_alpha_first}" [=[
[
  {"directory":".","file":"shared_source.cpp","command":"cl.exe /analyze /c shared_source.cpp /FoCMakeFiles/alpha.dir/shared_source.cpp.obj"},
  {"directory":".","file":"shared_source.cpp","command":"cl.exe /W4 /c shared_source.cpp /FoCMakeFiles/beta.dir/shared_source.cpp.obj"}
]
]=])
_expect_analyze_result("alpha-first-beta-must-fail" "${order_alpha_first}" "${shared_pattern}" "beta" "" failure)
_expect_analyze_result("alpha-first-alpha-passes" "${order_alpha_first}" "${shared_pattern}" "alpha" "" success)
_expect_analyze_result("single-config-request-needs-cache-identity" "${order_alpha_first}" "${shared_pattern}" "alpha" "Release" failure)

set(order_beta_first "${sandbox}/analyze-beta-first")
_write_compile_database("${order_beta_first}" [=[
[
  {"directory":".","file":"shared_source.cpp","command":"cl.exe /W4 /c shared_source.cpp /FoCMakeFiles/beta.dir/shared_source.cpp.obj"},
  {"directory":".","file":"shared_source.cpp","command":"cl.exe /analyze /c shared_source.cpp /FoCMakeFiles/alpha.dir/shared_source.cpp.obj"}
]
]=])
_expect_analyze_result("beta-first-beta-must-fail" "${order_beta_first}" "${shared_pattern}" "beta" "" failure)
_expect_analyze_result("beta-first-alpha-passes" "${order_beta_first}" "${shared_pattern}" "alpha" "" success)
_expect_analyze_result("missing-target-fails" "${order_beta_first}" "${shared_pattern}" "gamma" "" failure)

set(target_suffix_order_first "${sandbox}/analyze-target-suffix-first")
_write_compile_database("${target_suffix_order_first}" [=[
[
  {"directory":".","file":"alpha.dir/shared_source.cpp","command":"cl.exe /analyze /c shared_source.cpp /FoCMakeFiles/beta.dir/alpha.dir/shared_source.cpp.obj"},
  {"directory":".","file":"CMakeFiles/alpha.dir/shared_source.cpp","command":"cl.exe /analyze /c shared_source.cpp /FoCMakeFiles/gamma.dir/CMakeFiles/alpha.dir/shared_source.cpp.obj"}
]
]=])
_expect_analyze_result("first-owner-rejects-nested-target-suffixes" "${target_suffix_order_first}" "${shared_pattern}" "alpha" "" failure "No compile database object matched source")
_expect_analyze_result("first-owner-keeps-foreign-target-valid" "${target_suffix_order_first}" "${shared_pattern}" "beta" "" success)

set(target_suffix_order_reversed "${sandbox}/analyze-target-suffix-reversed")
_write_compile_database("${target_suffix_order_reversed}" [=[
[
  {"directory":".","file":"CMakeFiles/alpha.dir/shared_source.cpp","command":"cl.exe /analyze /c shared_source.cpp /FoCMakeFiles/gamma.dir/CMakeFiles/alpha.dir/shared_source.cpp.obj"},
  {"directory":".","file":"alpha.dir/shared_source.cpp","command":"cl.exe /analyze /c shared_source.cpp /FoCMakeFiles/beta.dir/alpha.dir/shared_source.cpp.obj"}
]
]=])
_expect_analyze_result("first-owner-rejects-nested-target-suffixes-reversed" "${target_suffix_order_reversed}" "${shared_pattern}" "alpha" "" failure "No compile database object matched source")
_expect_analyze_result("first-owner-keeps-foreign-target-valid-reversed" "${target_suffix_order_reversed}" "${shared_pattern}" "beta" "" success)

set(output_field_build "${sandbox}/analyze-output-field")
_write_compile_database("${output_field_build}" [=[
[
  {"directory":".","file":"shared_source.cpp","command":"cl.exe /analyze /c shared_source.cpp","output":"CMakeFiles/output_target.dir/shared_source.cpp.obj"}
]
]=])
_expect_analyze_result("output-field-identifies-target" "${output_field_build}" "${shared_pattern}" "output_target" "" success)

set(separated_fo_build "${sandbox}/analyze-separated-fo")
_write_compile_database("${separated_fo_build}" [=[
[
  {"directory":".","file":"shared source.cpp","command":"cl.exe /analyze /c \"shared source.cpp\" /Fo \"CMakeFiles/quoted_target.dir/shared source.cpp.obj\""}
]
]=])
_expect_analyze_result("separated-fo-option-identifies-target" "${separated_fo_build}" "shared[ ]source[.]cpp" "quoted_target" "" success)

set(flag_order_build "${sandbox}/analyze-flag-order")
_write_compile_database("${flag_order_build}" [=[
[
  {"directory":".","file":"shared_source.cpp","command":"cl.exe /analyze /analyze- /c shared_source.cpp /FoCMakeFiles/disabled_target.dir/shared_source.cpp.obj"},
  {"directory":".","file":"shared_source.cpp","command":"cl.exe /analyze- /analyze /c shared_source.cpp /FoCMakeFiles/enabled_target.dir/shared_source.cpp.obj"}
]
]=])
_expect_analyze_result("analyze-disabled-by-later-flag" "${flag_order_build}" "${shared_pattern}" "disabled_target" "" failure)
_expect_analyze_result("analyze-enabled-by-later-flag" "${flag_order_build}" "${shared_pattern}" "enabled_target" "" success)

set(multi_config_build "${sandbox}/analyze-multiconfig")
file(MAKE_DIRECTORY "${multi_config_build}")
file(WRITE "${multi_config_build}/CMakeCache.txt" "CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n")
_write_compile_database("${multi_config_build}" [=[
[
  {"directory":".","file":"shared_source.cpp","command":"cl.exe /analyze- /c shared_source.cpp /FoCMakeFiles/config_target.dir/Debug/shared_source.cpp.obj","output":"CMakeFiles/config_target.dir/Debug/shared_source.cpp.obj"},
  {"directory":".","file":"shared_source.cpp","command":"cl.exe /analyze /c shared_source.cpp /FoCMakeFiles/config_target.dir/Release/shared_source.cpp.obj","output":"CMakeFiles/config_target.dir/Release/shared_source.cpp.obj"}
]
]=])
_expect_analyze_result("release-configuration-selected" "${multi_config_build}" "${shared_pattern}" "config_target" "Release" success)
_expect_analyze_result("debug-configuration-still-disabled" "${multi_config_build}" "${shared_pattern}" "config_target" "Debug" failure)
_expect_analyze_result("unrequested-configurations-must-agree" "${multi_config_build}" "${shared_pattern}" "config_target" "" failure)

set(single_config_build "${sandbox}/analyze-singleconfig")
file(MAKE_DIRECTORY "${single_config_build}")
file(WRITE "${single_config_build}/CMakeCache.txt" "CMAKE_BUILD_TYPE:STRING=Debug\n")
_write_compile_database("${single_config_build}" [=[
[
  {"directory":".","file":"shared_source.cpp","command":"cl.exe /analyze /c shared_source.cpp /FoCMakeFiles/single_target.dir/shared_source.cpp.obj"}
]
]=])
_expect_analyze_result("single-config-command-matches" "${single_config_build}" "${shared_pattern}" "single_target" "Debug" success)
_expect_analyze_result("single-config-command-rejects-mismatch" "${single_config_build}" "${shared_pattern}" "single_target" "Release" failure)

set(ninja_fixture "${sandbox}/analyze-ninja-fallback")
file(MAKE_DIRECTORY "${ninja_fixture}")
file(WRITE "${ninja_fixture}/build.ninja" [=[
rule COMPILE
  command = cl.exe $FLAGS /c $in /Fo$out

rule GENERATE
  command = echo /analyze

build generated_dependency.txt: GENERATE

build CMakeFiles/ninja_alpha.dir/shared_source.cpp.obj: COMPILE shared_source.cpp | generated_dependency.txt
  FLAGS = /analyze

build CMakeFiles/ninja_beta.dir/shared_source.cpp.obj: COMPILE shared_source.cpp | generated_dependency.txt
  FLAGS = /W4
]=])
_expect_analyze_result("ninja-exact-beta-command-fails" "${ninja_fixture}" "${shared_pattern}" "ninja_beta" "" failure)
_expect_analyze_result("ninja-exact-alpha-command-passes" "${ninja_fixture}" "${shared_pattern}" "ninja_alpha" "" success)

set(unused_flags_ninja_fixture "${sandbox}/analyze-ninja-unused-flags")
file(MAKE_DIRECTORY "${unused_flags_ninja_fixture}")
file(WRITE "${unused_flags_ninja_fixture}/build.ninja" [=[
rule COMPILE
  command = cl.exe /c $in /Fo$out

build CMakeFiles/ninja_unused_flags.dir/shared_source.cpp.obj: COMPILE shared_source.cpp
  FLAGS = /analyze
]=])
_expect_analyze_result(
    "ninja-does-not-infer-unused-flags"
    "${unused_flags_ninja_fixture}" "${shared_pattern}" "ninja_unused_flags" "" failure
    "does not enable[ \t\r\n]+/analyze" TRUE
)

set(fixture_compiler "")
set(fixture_ninja "")
set(parent_cache "${INPUTNEXTADAPTERS_BINARY_DIR}/CMakeCache.txt")
if(EXISTS "${parent_cache}")
    file(STRINGS "${parent_cache}" compiler_entry REGEX "^CMAKE_CXX_COMPILER:")
    file(STRINGS "${parent_cache}" make_program_entry REGEX "^CMAKE_MAKE_PROGRAM:")
    if(compiler_entry)
        string(REGEX REPLACE "^[^=]*=" "" fixture_compiler "${compiler_entry}")
    endif()
    if(make_program_entry MATCHES "[Nn]inja(-build)?([.]exe)?$")
        string(REGEX REPLACE "^[^=]*=" "" fixture_ninja "${make_program_entry}")
    endif()
endif()
if(fixture_compiler STREQUAL "" OR NOT EXISTS "${fixture_compiler}")
    find_program(discovered_compiler NAMES cl.exe)
    if(discovered_compiler)
        set(fixture_compiler "${discovered_compiler}")
    endif()
endif()
if(fixture_ninja STREQUAL "" OR NOT EXISTS "${fixture_ninja}")
    find_program(discovered_ninja NAMES ninja ninja-build)
    if(discovered_ninja)
        set(fixture_ninja "${discovered_ninja}")
    endif()
endif()

if(fixture_compiler AND EXISTS "${fixture_compiler}" AND fixture_ninja AND EXISTS "${fixture_ninja}")
    message(STATUS "[RegressionTest] Checking exact configuration selection in a real Ninja Multi-Config build...")
    set(real_ninja_multi_config_build "${sandbox}/analyze-real-ninja-multiconfig")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -S "${CMAKE_CURRENT_LIST_DIR}/NinjaMultiConfigFixture"
            -B "${real_ninja_multi_config_build}"
            -G "Ninja Multi-Config"
            "-DCMAKE_CXX_COMPILER=${fixture_compiler}"
            "-DCMAKE_MAKE_PROGRAM=${fixture_ninja}"
            -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
            -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF
        RESULT_VARIABLE fixture_configure_result
        OUTPUT_VARIABLE fixture_configure_output
        ERROR_VARIABLE fixture_configure_error
    )
    if(NOT fixture_configure_result EQUAL 0)
        message(FATAL_ERROR "Could not configure the real Ninja Multi-Config analysis fixture: ${fixture_configure_output}${fixture_configure_error}")
    endif()
    if(EXISTS "${real_ninja_multi_config_build}/compile_commands.json" OR
       NOT EXISTS "${real_ninja_multi_config_build}/CMakeFiles/impl-Debug.ninja" OR
       NOT EXISTS "${real_ninja_multi_config_build}/CMakeFiles/impl-Release.ninja")
        message(FATAL_ERROR "The real Ninja Multi-Config fixture did not generate the expected compile-database-free implementation files.")
    endif()
    _expect_analyze_result(
        "real-ninja-multiconfig-release-selects-analyze-enabled-command"
        "${real_ninja_multi_config_build}" "shared_source[.]cpp" "ninja_multi_config_target" "Release" success
    )
    _expect_analyze_result(
        "real-ninja-multiconfig-debug-selects-analyze-disabled-command"
        "${real_ninja_multi_config_build}" "shared_source[.]cpp" "ninja_multi_config_target" "Debug" failure
    )
    _expect_analyze_result(
        "real-ninja-multiconfig-unrequested-configurations-must-agree"
        "${real_ninja_multi_config_build}" "shared_source[.]cpp" "ninja_multi_config_target" "" failure
    )
else()
    message(WARNING "Skipping real Ninja Multi-Config regression because neither the configured C++ compiler nor Ninja is available.")
endif()
