include_guard(GLOBAL)

set(_vendor_fixture "${INPUTNEXTADAPTERS_SOURCE_DIR}/tests/cmake/regression/VendorOptionsFixture")
set(_vendor_build "${sandbox}/vendor-options")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_vendor_fixture}" -B "${_vendor_build}"
        -G "Ninja Multi-Config" "-DINPUTNEXTADAPTERS_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
    RESULT_VARIABLE _vendor_result OUTPUT_VARIABLE _vendor_output ERROR_VARIABLE _vendor_error
)
if(NOT _vendor_result EQUAL 0)
    message(FATAL_ERROR "Vendor option fixture failed:\n${_vendor_output}${_vendor_error}")
endif()
foreach(configuration IN ITEMS Debug Release)
    foreach(context IN ITEMS enabled disabled)
        file(READ "${_vendor_build}/${configuration}/${context}.txt" actual)
        list(FILTER actual EXCLUDE REGEX "^$")
        set(expected /DNOTE=/wd9999 /DPAIR=left,right
            "SHELL:\"/bigobj\" \"/DNAME=two words\" \"/DPAIR=left,right\""
            "SHELL:-Xclang -fkeep-group")
        if(configuration STREQUAL "Debug")
            list(PREPEND expected /debug-only)
        else()
            list(PREPEND expected /release-only)
        endif()
        if(context STREQUAL "enabled")
            list(APPEND expected /consumer-only)
        endif()
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "Vendor options changed in ${configuration}/${context}:\n${actual}\nExpected:\n${expected}")
        endif()
    endforeach()
endforeach()

# Shell reconstruction must preserve quoting, backslashes and literal flag text.
include("${INPUTNEXTADAPTERS_SOURCE_DIR}/cmake/VendorOptions.cmake")
set(_shell [=[SHELL:/wd4265 /bigobj "/DNOTE=/wd1234 two words" '/DPATH=C:\\sdk\\inc' '/DQUOTE="value"']=])
_inputnext_filter_vendor_shell_option("${_shell}" _shell_filtered)
string(SUBSTRING "${_shell_filtered}" 6 -1 _shell_arguments)
separate_arguments(_shell_actual UNIX_COMMAND "${_shell_arguments}")
set(_shell_expected /bigobj "/DNOTE=/wd1234 two words" [=[/DPATH=C:\sdk\inc]=] [=[/DQUOTE="value"]=])
if(NOT _shell_actual STREQUAL _shell_expected)
    message(FATAL_ERROR "Vendor shell argument quoting changed: ${_shell_actual}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_vendor_fixture}" -B "${sandbox}/vendor-unsupported"
        -G Ninja "-DINPUTNEXTADAPTERS_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        -DINPUTNEXT_VENDOR_UNSUPPORTED=ON
    RESULT_VARIABLE _vendor_result OUTPUT_VARIABLE _vendor_output ERROR_VARIABLE _vendor_error
)
if(_vendor_result EQUAL 0 OR NOT _vendor_error MATCHES "Split conditional SHELL")
    message(FATAL_ERROR "Conditional shell suppression was not rejected clearly:\n${_vendor_output}${_vendor_error}")
endif()
