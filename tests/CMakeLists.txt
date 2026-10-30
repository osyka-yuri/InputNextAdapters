set(fixture_targets)

include("${PROJECT_SOURCE_DIR}/cmake/InputNextCatch2.cmake")
add_subdirectory(harness)
add_subdirectory(contract/sdk_layout)
add_subdirectory(cpp/integration/runtime_package)
add_subdirectory(cpp/integration/output_session)
add_subdirectory(cpp/unit/case_runner)
add_subdirectory(cpp/unit/effects)
add_subdirectory(cpp/unit/led_policy)
add_subdirectory(cpp/component/effects)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RegisterCases.cmake")

set(_inputnext_msvc_runtime_argument)
if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(_inputnext_msvc_runtime_argument
        "-DINPUTNEXTADAPTERS_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}"
    )
endif()

function(add_inputnext_fake_runtime kind fixture_id target_name)
    add_library(${target_name} MODULE fake_runtime.cpp)
    set_target_properties(${target_name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${target_name}"
        SUFFIX ".dll"
    )
    target_compile_definitions(${target_name} PRIVATE
        FAKE_RUNTIME_KIND=${kind}
        TEST_FIXTURE_ID=${fixture_id}
    )

    target_include_directories(${target_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/third_party/inputnext/include"
    )
    inputnext_configure_target(${target_name})

    if(kind EQUAL 1)
        target_sources(${target_name} PRIVATE fake_runtime_controls.def)
    else()
        target_sources(${target_name} PRIVATE fake_runtime.def)
    endif()

    set(fixture_targets ${fixture_targets} ${target_name} PARENT_SCOPE)
endfunction()

add_inputnext_fake_runtime(0 1 fake_runtime_kind_0_id_1)
add_inputnext_fake_runtime(0 2 fake_runtime_kind_0_id_2)
foreach(kind RANGE 1 14)
    math(EXPR fixture_id "${kind} + 2")
    add_inputnext_fake_runtime(${kind} ${fixture_id} fake_runtime_kind_${kind}_id_${fixture_id})
endforeach()

set(_inputnext_runtime_fixture_arguments
        "$<TARGET_FILE:fake_runtime_kind_0_id_1>"
        "$<TARGET_FILE:fake_runtime_kind_0_id_2>"
        "$<TARGET_FILE:fake_runtime_kind_1_id_3>"
        "$<TARGET_FILE:fake_runtime_kind_2_id_4>"
        "$<TARGET_FILE:fake_runtime_kind_3_id_5>"
        "$<TARGET_FILE:fake_runtime_kind_4_id_6>"
        "$<TARGET_FILE:fake_runtime_kind_5_id_7>"
        "$<TARGET_FILE:fake_runtime_kind_6_id_8>"
        "$<TARGET_FILE:fake_runtime_kind_7_id_9>"
        "$<TARGET_FILE:fake_runtime_kind_8_id_10>"
        "$<TARGET_FILE:fake_runtime_kind_9_id_11>"
        "$<TARGET_FILE:fake_runtime_kind_10_id_12>"
        "$<TARGET_FILE:fake_runtime_kind_11_id_13>"
        "$<TARGET_FILE:fake_runtime_kind_12_id_14>"
        "$<TARGET_FILE:fake_runtime_kind_13_id_15>"
        "$<TARGET_FILE:fake_runtime_kind_14_id_16>"
)

add_subdirectory(cpp/unit/runtime_client)
add_subdirectory(cpp/integration/runtime_client)

foreach(mode IN ITEMS add_subdirectory installed)
    if(mode STREQUAL "installed")
        set(_consumer_build_dependencies
            BUILD_DEPENDS inputnext_adapters_runtime_client inputnext_adapters_effects
        )
    else()
        set(_consumer_build_dependencies)
    endif()

    add_test(NAME inputnext_adapters_effects_probe_${mode}
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUTNEXTADAPTERS_MODE=${mode}"
            "-DINPUTNEXTADAPTERS_CONSUMER=effects_probe"
            "-DINPUTNEXTADAPTERS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DINPUTNEXTADAPTERS_BINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DINPUTNEXTADAPTERS_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DINPUTNEXTADAPTERS_GENERATOR=${CMAKE_GENERATOR}"
            "-DINPUTNEXTADAPTERS_CONFIGURATION=$<CONFIG>"
            "-DINPUTNEXTADAPTERS_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DINPUTNEXTADAPTERS_CXX_COMPILER_TARGET=${CMAKE_CXX_COMPILER_TARGET}"
            ${_inputnext_msvc_runtime_argument}
            "-DINPUTNEXTADAPTERS_ENABLE_ASAN:BOOL=${INPUTNEXTADAPTERS_ENABLE_ASAN}"
            "-DINPUTNEXTADAPTERS_ENABLE_UBSAN:BOOL=${INPUTNEXTADAPTERS_ENABLE_UBSAN}"
            "-DINPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE:BOOL=${INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE}"
            "-DINPUTNEXTADAPTERS_ENABLE_CLANG_TIDY:BOOL=${INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY}"
            "-DINPUTNEXTADAPTERS_HELPERS_SOURCE_DIR:PATH=${PROJECT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyConsumer.cmake"
        ${_consumer_build_dependencies}
    )
    add_test(NAME inputnext_adapters_consumer_${mode}
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUTNEXTADAPTERS_MODE=${mode}"
            "-DINPUTNEXTADAPTERS_CONSUMER=common_effects"
            "-DINPUTNEXTADAPTERS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DINPUTNEXTADAPTERS_BINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DINPUTNEXTADAPTERS_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DINPUTNEXTADAPTERS_GENERATOR=${CMAKE_GENERATOR}"
            "-DINPUTNEXTADAPTERS_CONFIGURATION=$<CONFIG>"
            "-DINPUTNEXTADAPTERS_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DINPUTNEXTADAPTERS_CXX_COMPILER_TARGET=${CMAKE_CXX_COMPILER_TARGET}"
            ${_inputnext_msvc_runtime_argument}
            "-DINPUTNEXTADAPTERS_ENABLE_ASAN:BOOL=${INPUTNEXTADAPTERS_ENABLE_ASAN}"
            "-DINPUTNEXTADAPTERS_ENABLE_UBSAN:BOOL=${INPUTNEXTADAPTERS_ENABLE_UBSAN}"
            "-DINPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE:BOOL=${INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE}"
            "-DINPUTNEXTADAPTERS_ENABLE_CLANG_TIDY:BOOL=${INPUTNEXTADAPTERS_ENABLE_CLANG_TIDY}"
            "-DINPUTNEXTADAPTERS_HELPERS_SOURCE_DIR:PATH=${PROJECT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyConsumer.cmake"
        ${_consumer_build_dependencies}
    )
    set_tests_properties(
        inputnext_adapters_effects_probe_${mode}
        inputnext_adapters_consumer_${mode}
        PROPERTIES LABELS "integration;cmake"
    )
endforeach()

add_test(NAME inputnext_adapters_cmake_regression
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUTNEXTADAPTERS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DINPUTNEXTADAPTERS_BINARY_DIR=${PROJECT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RegressionTests.cmake"
)
set_tests_properties(inputnext_adapters_cmake_regression PROPERTIES
    LABELS "integration;cmake"
)

if(INPUTNEXTADAPTERS_ENABLE_ASAN)
    add_test(NAME inputnext_adapters_asan_external_consumer
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUTNEXTADAPTERS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DINPUTNEXTADAPTERS_BINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DINPUTNEXTADAPTERS_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DINPUTNEXTADAPTERS_GENERATOR=${CMAKE_GENERATOR}"
            "-DINPUTNEXTADAPTERS_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DINPUTNEXTADAPTERS_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DINPUTNEXTADAPTERS_CONFIGURATION=$<CONFIG>"
            "-DINPUTNEXTADAPTERS_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DINPUTNEXTADAPTERS_CXX_COMPILER_TARGET=${CMAKE_CXX_COMPILER_TARGET}"
            ${_inputnext_msvc_runtime_argument}
            "-DINPUTNEXTADAPTERS_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyAsanConsumer.cmake"
        BUILD_DEPENDS inputnext_adapters_runtime_client inputnext_adapters_effects
    )
    set_tests_properties(inputnext_adapters_asan_external_consumer PROPERTIES
        LABELS "integration;cmake;asan"
        TIMEOUT 180
    )
endif()

if(INPUTNEXTADAPTERS_ENABLE_UBSAN)
    add_test(NAME inputnext_adapters_ubsan_external_consumer
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUTNEXTADAPTERS_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DINPUTNEXTADAPTERS_BINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DINPUTNEXTADAPTERS_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DINPUTNEXTADAPTERS_GENERATOR=${CMAKE_GENERATOR}"
            "-DINPUTNEXTADAPTERS_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DINPUTNEXTADAPTERS_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DINPUTNEXTADAPTERS_CONFIGURATION=$<CONFIG>"
            "-DINPUTNEXTADAPTERS_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DINPUTNEXTADAPTERS_CXX_COMPILER_TARGET=${CMAKE_CXX_COMPILER_TARGET}"
            ${_inputnext_msvc_runtime_argument}
            "-DINPUTNEXTADAPTERS_ENABLE_UBSAN:BOOL=${INPUTNEXTADAPTERS_ENABLE_UBSAN}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyUbsanConsumer.cmake"
        BUILD_DEPENDS inputnext_adapters_runtime_client inputnext_adapters_effects
    )
    set_tests_properties(inputnext_adapters_ubsan_external_consumer PROPERTIES
        LABELS "integration;cmake;ubsan"
        TIMEOUT 300
    )
endif()

if(TARGET InputNextAdapters::ClangClAsanRuntime)
    get_property(_inputnext_registered_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${_inputnext_registered_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:InputNextAdapters::ClangClAsanRuntime>"
    )
endif()
