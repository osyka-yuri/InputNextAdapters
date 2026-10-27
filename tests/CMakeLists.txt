set(fixture_targets)
include("${PROJECT_SOURCE_DIR}/cmake/InputNextCatch2.cmake")
add_subdirectory(harness)
add_subdirectory(contract/sdk_layout)
add_subdirectory(cpp/unit/case_runner)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RegisterCases.cmake")
add_subdirectory(cpp/integration/runtime_package)
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
