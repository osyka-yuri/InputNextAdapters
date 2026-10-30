cmake_minimum_required(VERSION 4.4.4)
project(InputNextVendorOptionsRegression LANGUAGES CXX)
include("${INPUTNEXTADAPTERS_SOURCE_DIR}/cmake/VendorOptions.cmake")

add_library(vendor INTERFACE)
add_library(Vendor::Library ALIAS vendor)
if(INPUTNEXT_VENDOR_UNSUPPORTED)
    target_compile_options(vendor INTERFACE "$<$<CONFIG:Debug>:SHELL:/wd4265 /bigobj>")
endif()
target_compile_options(vendor INTERFACE
    /wd4265 /WD4324 -wd4702
    "$<$<CONFIG:Debug>:/wd4702;/debug-only>"
    "$<IF:$<CONFIG:Debug>,/wd4005,/release-only>"
    /DNOTE=/wd9999
    /DPAIR=left,right
    "SHELL:/wd4100 /bigobj /DNAME='two words' /DPAIR=left,right"
    "SHELL:/wd4200"
    "SHELL:-Xclang -fkeep-group"
    "$<$<BOOL:$<TARGET_PROPERTY:VENDOR_TEST_GATE>>:/wd4018;/consumer-only>"
)
inputnext_scope_vendor_warning_suppressions(Vendor::Library)
get_target_property(first vendor INTERFACE_COMPILE_OPTIONS)
inputnext_scope_vendor_warning_suppressions(vendor)
get_target_property(second vendor INTERFACE_COMPILE_OPTIONS)
if(NOT first STREQUAL second)
    message(FATAL_ERROR "Repeated vendor scoping changed the filtered interface.")
endif()
inputnext_scope_vendor_warning_suppressions(absent)
add_library(empty INTERFACE)
inputnext_scope_vendor_warning_suppressions(empty)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/probe.cpp" "int probe() { return 0; }\n")
foreach(context IN ITEMS enabled disabled)
    add_library(${context} OBJECT "${CMAKE_CURRENT_BINARY_DIR}/probe.cpp")
    target_link_libraries(${context} PRIVATE vendor)
    if(context STREQUAL "enabled")
        set_property(TARGET ${context} PROPERTY VENDOR_TEST_GATE TRUE)
    else()
        set_property(TARGET ${context} PROPERTY VENDOR_TEST_GATE FALSE)
    endif()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${context}.txt"
        CONTENT "$<TARGET_PROPERTY:${context},COMPILE_OPTIONS>")
endforeach()
