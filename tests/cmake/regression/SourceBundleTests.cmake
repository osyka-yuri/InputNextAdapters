set(_source_bundle_root "${sandbox}/source-bundle")
file(MAKE_DIRECTORY "${_source_bundle_root}")
set(_bundle_fixture "${CMAKE_CURRENT_LIST_DIR}/SourceBundleFixture")

function(_source_bundle_create_fixture name)
    set(_root "${_source_bundle_root}/${name}")
    set(_dependency "${_root}/dependencies/skyrim-se/fmt")
    set(_directive_fixture "${_dependency}/include/RE/GenerateDirectives.ps1")
    set(_compiler_linker_fixture "${_dependency}/build/CompilerAndLinker.cmake")
    set(_directxtk_config_fixture "${_dependency}/build/DirectXTK-config.cmake.in")
    set(_directxtk_pkg_fixture "${_dependency}/build/DirectXTK.pc.in")
    file(MAKE_DIRECTORY "${_dependency}/include/RE" "${_dependency}/build")
    file(WRITE "${_dependency}/CMakeLists.txt" [=[
if(NOT DEFINED BUNDLE_SENTINEL)
    message(FATAL_ERROR "The dependency CMake fixture requires a sentinel path.")
endif()
file(WRITE "${BUNDLE_SENTINEL}" "executed\n")
]=])
    file(WRITE "${_directive_fixture}" "# normalized dotted path fixture\n")
    file(WRITE "${_compiler_linker_fixture}" "set(DIRECTXTK_BUILD_FIXTURE ON)\n")
    file(WRITE "${_directxtk_config_fixture}" "@PACKAGE_INIT@\n")
    file(WRITE "${_directxtk_pkg_fixture}" "prefix=@prefix@\n")
    file(SIZE "${_dependency}/CMakeLists.txt" _size)
    file(SHA256 "${_dependency}/CMakeLists.txt" _sha256)
    file(SIZE "${_directive_fixture}" _directive_size)
    file(SHA256 "${_directive_fixture}" _directive_sha256)
    file(SIZE "${_compiler_linker_fixture}" _compiler_linker_size)
    file(SHA256 "${_compiler_linker_fixture}" _compiler_linker_sha256)
    file(SIZE "${_directxtk_config_fixture}" _directxtk_config_size)
    file(SHA256 "${_directxtk_config_fixture}" _directxtk_config_sha256)
    file(SIZE "${_directxtk_pkg_fixture}" _directxtk_pkg_size)
    file(SHA256 "${_directxtk_pkg_fixture}" _directxtk_pkg_sha256)
    set(_manifest "${_root}/inputnext-adapters-source.json")
    file(WRITE "${_manifest}"
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"kind\": \"inputnext-adapters-source-bundle\",\n"
        "  \"product\": \"skyrim-se\",\n"
        "  \"dependencies\": [{\n"
        "    \"name\": \"fmt\",\n"
        "    \"repository\": \"https://example.invalid/fmt.git\",\n"
        "    \"revision\": \"0123456789abcdef0123456789abcdef01234567\",\n"
        "    \"path\": \"dependencies/skyrim-se/fmt\",\n"
        "    \"fetchContent\": \"fmt\",\n"
        "    \"patches\": [],\n"
        "    \"submodules\": [],\n"
        "    \"files\": [\n"
        "      {\"path\": \"CMakeLists.txt\", \"size\": ${_size}, \"sha256\": \"${_sha256}\"},\n"
        "      {\"path\": \"build/CompilerAndLinker.cmake\", \"size\": ${_compiler_linker_size}, \"sha256\": \"${_compiler_linker_sha256}\"},\n"
        "      {\"path\": \"build/DirectXTK-config.cmake.in\", \"size\": ${_directxtk_config_size}, \"sha256\": \"${_directxtk_config_sha256}\"},\n"
        "      {\"path\": \"build/DirectXTK.pc.in\", \"size\": ${_directxtk_pkg_size}, \"sha256\": \"${_directxtk_pkg_sha256}\"},\n"
        "      {\"path\": \"include/RE/GenerateDirectives.ps1\", \"size\": ${_directive_size}, \"sha256\": \"${_directive_sha256}\"}\n"
        "    ]\n"
        "  }]\n"
        "}\n"
    )
    set(_sentinel "${_root}/dependency-cmake-ran.txt")
    set(_output "${_root}/receipt-build")
    set(${name}_ROOT "${_root}" PARENT_SCOPE)
    set(${name}_DEPENDENCY "${_dependency}" PARENT_SCOPE)
    set(${name}_MANIFEST "${_manifest}" PARENT_SCOPE)
    set(${name}_SENTINEL "${_sentinel}" PARENT_SCOPE)
    set(${name}_OUTPUT "${_output}" PARENT_SCOPE)
endfunction()

function(_source_bundle_run name)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DHELPER_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
            "-DMANIFEST=${${name}_MANIFEST}"
            "-DINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH=${${name}_MANIFEST}"
            "-DSOURCE_ROOT=${${name}_DEPENDENCY}"
            "-DSENTINEL=${${name}_SENTINEL}"
            -DFETCHCONTENT_FULLY_DISCONNECTED=ON
            -S "${_bundle_fixture}"
            -B "${${name}_OUTPUT}"
        WORKING_DIRECTORY "${${name}_OUTPUT}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
    )
    set(${name}_RESULT "${_result}" PARENT_SCOPE)
    set(${name}_OUTPUT_TEXT "${_output}${_error}" PARENT_SCOPE)
endfunction()

function(_fallout4_patch_bundle_run manifest output sentinel out_result out_text)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DHELPER_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
            "-DINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH=${manifest}"
            "-DINPUTNEXTADAPTERS_FALLOUT4_SENTINEL=${sentinel}"
            -DFETCHCONTENT_FULLY_DISCONNECTED=ON
            -S "${CMAKE_CURRENT_LIST_DIR}/Fallout4SourceBundleFixture"
            -B "${output}"
        WORKING_DIRECTORY "${output}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error
    )
    set(${out_result} "${_result}" PARENT_SCOPE)
    set(${out_text} "${_output}${_error}" PARENT_SCOPE)
endfunction()

_source_bundle_create_fixture(valid)
file(MAKE_DIRECTORY "${valid_OUTPUT}")
if(EXISTS "${valid_ROOT}/.git")
    message(FATAL_ERROR "The source bundle fixture unexpectedly contains Git metadata.")
endif()
_source_bundle_run(valid)
if(NOT valid_RESULT EQUAL 0 OR NOT EXISTS "${valid_SENTINEL}")
    message(FATAL_ERROR "A verified source bundle without .git did not configure cleanly:\n${valid_OUTPUT_TEXT}")
endif()
if(NOT EXISTS "${valid_OUTPUT}/inputnext-adapters-dependency-sources.json")
    message(FATAL_ERROR "The source bundle configure did not write its typed dependency source receipt.")
endif()
file(READ "${valid_OUTPUT}/inputnext-adapters-dependency-sources.json" _receipt)
string(JSON _receipt_product GET "${_receipt}" product)
string(JSON _receipt_dependency GET "${_receipt}" dependencies 0 name)
if(NOT _receipt_product STREQUAL "skyrim-se" OR NOT _receipt_dependency STREQUAL "fmt")
    message(FATAL_ERROR "The dependency source receipt has the wrong product or dependency identity.")
endif()

_source_bundle_create_fixture(missing)
file(MAKE_DIRECTORY "${missing_OUTPUT}")
file(REMOVE "${missing_DEPENDENCY}/CMakeLists.txt")
_source_bundle_run(missing)
if(missing_RESULT EQUAL 0 OR EXISTS "${missing_SENTINEL}")
    message(FATAL_ERROR "Missing source bundle content was not rejected before dependency CMake execution.")
endif()

_source_bundle_create_fixture(extra)
file(MAKE_DIRECTORY "${extra_OUTPUT}")
file(WRITE "${extra_DEPENDENCY}/unexpected.txt" "extra\n")
_source_bundle_run(extra)
if(extra_RESULT EQUAL 0 OR EXISTS "${extra_SENTINEL}")
    message(FATAL_ERROR "Extra source bundle content was not rejected before dependency CMake execution.")
endif()

_source_bundle_create_fixture(generated_metadata)
file(MAKE_DIRECTORY "${generated_metadata_OUTPUT}")
file(WRITE "${generated_metadata_DEPENDENCY}/build/CMakeCache.txt" "generated cache\n")
_source_bundle_run(generated_metadata)
if(generated_metadata_RESULT EQUAL 0 OR EXISTS "${generated_metadata_SENTINEL}")
    message(FATAL_ERROR "Generated metadata was not rejected before dependency CMake execution.")
endif()

_source_bundle_create_fixture(altered)
file(MAKE_DIRECTORY "${altered_OUTPUT}")
file(WRITE "${altered_DEPENDENCY}/CMakeLists.txt" "altered source\n")
_source_bundle_run(altered)
if(altered_RESULT EQUAL 0 OR EXISTS "${altered_SENTINEL}")
    message(FATAL_ERROR "Altered source bundle content was not rejected before dependency CMake execution.")
endif()

_source_bundle_create_fixture(path_list_separator)
file(MAKE_DIRECTORY "${path_list_separator_OUTPUT}")
file(READ "${path_list_separator_MANIFEST}" _path_list_manifest)
string(JSON _first_path GET "${_path_list_manifest}" dependencies 0 files 0 path)
string(JSON _second_path GET "${_path_list_manifest}" dependencies 0 files 1 path)
string(JSON _path_list_manifest SET "${_path_list_manifest}" dependencies 0 files 0 path "\"\"")
string(JSON _path_list_manifest SET "${_path_list_manifest}"
    dependencies 0 files 1 path "\"${_first_path};${_second_path}\"")
file(WRITE "${path_list_separator_MANIFEST}" "${_path_list_manifest}")
_source_bundle_run(path_list_separator)
if(path_list_separator_RESULT EQUAL 0 OR EXISTS "${path_list_separator_SENTINEL}")
    message(FATAL_ERROR "Flattened malformed JSON paths were accepted before dependency CMake execution.")
endif()

_source_bundle_create_fixture(hash_separator)
file(MAKE_DIRECTORY "${hash_separator_OUTPUT}")
file(READ "${hash_separator_MANIFEST}" _hash_separator_manifest)
string(JSON _hash_separator_count LENGTH "${_hash_separator_manifest}" dependencies 0 files)
math(EXPR _hash_separator_last "${_hash_separator_count} - 1")
string(JSON _last_hash GET "${_hash_separator_manifest}" dependencies 0 files ${_hash_separator_last} sha256)
string(JSON _hash_separator_manifest SET "${_hash_separator_manifest}"
    dependencies 0 files ${_hash_separator_last} sha256 "\"${_last_hash};ignored\"")
file(WRITE "${hash_separator_MANIFEST}" "${_hash_separator_manifest}")
_source_bundle_run(hash_separator)
if(hash_separator_RESULT EQUAL 0 OR EXISTS "${hash_separator_SENTINEL}")
    message(FATAL_ERROR "A list separator in a declared hash was accepted before dependency CMake execution.")
endif()

_source_bundle_create_fixture(escaping)
file(MAKE_DIRECTORY "${escaping_OUTPUT}")
file(READ "${escaping_MANIFEST}" _escaping_manifest)
string(REPLACE "CMakeLists.txt" "../outside.txt" _escaping_manifest "${_escaping_manifest}")
file(WRITE "${escaping_MANIFEST}" "${_escaping_manifest}")
_source_bundle_run(escaping)
if(escaping_RESULT EQUAL 0 OR EXISTS "${escaping_SENTINEL}")
    message(FATAL_ERROR "Escaping source bundle paths were not rejected before dependency CMake execution.")
endif()

_source_bundle_create_fixture(dot_segment)
file(MAKE_DIRECTORY "${dot_segment_OUTPUT}")
file(READ "${dot_segment_MANIFEST}" _dot_segment_manifest)
string(REPLACE "\"path\": \"CMakeLists.txt\"" "\"path\": \"./CMakeLists.txt\""
    _dot_segment_manifest "${_dot_segment_manifest}")
file(WRITE "${dot_segment_MANIFEST}" "${_dot_segment_manifest}")
_source_bundle_run(dot_segment)
if(dot_segment_RESULT EQUAL 0 OR EXISTS "${dot_segment_SENTINEL}")
    message(FATAL_ERROR "Dot-segment source bundle paths were not rejected before dependency CMake execution.")
endif()

_source_bundle_create_fixture(wrong_pin)
file(MAKE_DIRECTORY "${wrong_pin_OUTPUT}")
file(READ "${wrong_pin_MANIFEST}" _wrong_pin_manifest)
string(REPLACE "0123456789abcdef0123456789abcdef01234567" "1123456789abcdef0123456789abcdef01234567"
    _wrong_pin_manifest "${_wrong_pin_manifest}")
file(WRITE "${wrong_pin_MANIFEST}" "${_wrong_pin_manifest}")
_source_bundle_run(wrong_pin)
if(wrong_pin_RESULT EQUAL 0 OR EXISTS "${wrong_pin_SENTINEL}")
    message(FATAL_ERROR "Wrong source bundle pin was not rejected before dependency CMake execution.")
endif()

_source_bundle_create_fixture(linked)
file(MAKE_DIRECTORY "${linked_OUTPUT}")
file(CREATE_LINK "${linked_DEPENDENCY}/CMakeLists.txt" "${linked_DEPENDENCY}/alias.txt"
    SYMBOLIC RESULT _link_result)
if(NOT _link_result STREQUAL "0")
    message(FATAL_ERROR "Could not create the isolated link fixture required by the source bundle gate: ${_link_result}")
endif()
_source_bundle_run(linked)
if(linked_RESULT EQUAL 0 OR EXISTS "${linked_SENTINEL}")
    message(FATAL_ERROR "Linked source bundle content was not rejected before dependency CMake execution.")
endif()

_source_bundle_create_fixture(release_override)
file(MAKE_DIRECTORY "${release_override_OUTPUT}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DHELPER_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DMANIFEST=${release_override_MANIFEST}"
        -DINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH=
        "-DSOURCE_ROOT=${release_override_DEPENDENCY}"
        "-DSENTINEL=${release_override_SENTINEL}"
        -DINPUTNEXTADAPTERS_RELEASE_BUILD:BOOL=ON
        "-DFETCHCONTENT_SOURCE_DIR_UNDECLARED:PATH=${release_override_DEPENDENCY}"
        -DFETCHCONTENT_FULLY_DISCONNECTED=ON
        -S "${_bundle_fixture}"
        -B "${release_override_OUTPUT}"
    WORKING_DIRECTORY "${release_override_OUTPUT}"
    RESULT_VARIABLE _release_override_result
    OUTPUT_VARIABLE _release_override_output
    ERROR_VARIABLE _release_override_error
)
if(_release_override_result EQUAL 0 OR EXISTS "${release_override_SENTINEL}")
    message(FATAL_ERROR "Release source override was not rejected before dependency CMake execution.")
endif()

_source_bundle_create_fixture(release_bundle)
file(MAKE_DIRECTORY "${release_bundle_OUTPUT}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DHELPER_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DMANIFEST=${release_bundle_MANIFEST}"
        "-DINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH=${release_bundle_MANIFEST}"
        "-DSOURCE_ROOT=${release_bundle_DEPENDENCY}"
        "-DSENTINEL=${release_bundle_SENTINEL}"
        -DINPUTNEXTADAPTERS_RELEASE_BUILD:BOOL=ON
        -DFETCHCONTENT_FULLY_DISCONNECTED=ON
        -S "${_bundle_fixture}"
        -B "${release_bundle_OUTPUT}"
    WORKING_DIRECTORY "${release_bundle_OUTPUT}"
    RESULT_VARIABLE _release_bundle_result
    OUTPUT_VARIABLE _release_bundle_output
    ERROR_VARIABLE _release_bundle_error
)
if(_release_bundle_result EQUAL 0 OR EXISTS "${release_bundle_SENTINEL}")
    message(FATAL_ERROR "Release source bundle override was not rejected before dependency CMake execution.")
endif()

set(_git_fixture "${_source_bundle_root}/git-revision")
set(_git_fixture_script "${CMAKE_CURRENT_LIST_DIR}/CheckGitRevision.cmake")
file(MAKE_DIRECTORY "${_git_fixture}")
file(WRITE "${_git_fixture}/revision.txt" "fixture\n")
execute_process(COMMAND git init --quiet "${_git_fixture}" RESULT_VARIABLE _git_init_result)
if(NOT _git_init_result EQUAL 0)
    message(FATAL_ERROR "Could not initialize the temporary Git revision fixture.")
endif()
execute_process(COMMAND git -C "${_git_fixture}" config user.name "B6 CMake fixture")
execute_process(COMMAND git -C "${_git_fixture}" config user.email "b6-fixture@example.invalid")
execute_process(COMMAND git -C "${_git_fixture}" add revision.txt)
execute_process(COMMAND git -C "${_git_fixture}" commit --quiet -m "fixture" RESULT_VARIABLE _git_commit_result)
if(NOT _git_commit_result EQUAL 0)
    message(FATAL_ERROR "Could not commit the temporary Git revision fixture.")
endif()
execute_process(
    COMMAND git -C "${_git_fixture}" rev-parse HEAD
    OUTPUT_VARIABLE _git_actual_revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DHELPER_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DSOURCE_DIRECTORY=${_git_fixture}"
        "-DEXPECTED_REVISION=${_git_actual_revision}"
        -P "${_git_fixture_script}"
    RESULT_VARIABLE _git_expected_result
    OUTPUT_VARIABLE _git_expected_output
    ERROR_VARIABLE _git_expected_error
)
if(NOT _git_expected_result EQUAL 0)
    message(FATAL_ERROR "Correct ordinary Git submodule revision was rejected:\n${_git_expected_output}${_git_expected_error}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DHELPER_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DSOURCE_DIRECTORY=${_git_fixture}"
        -DEXPECTED_REVISION=0000000000000000000000000000000000000000
        -P "${_git_fixture_script}"
    RESULT_VARIABLE _git_wrong_result
    OUTPUT_VARIABLE _git_wrong_output
    ERROR_VARIABLE _git_wrong_error
)
if(_git_wrong_result EQUAL 0)
    message(FATAL_ERROR "An ordinary Git submodule at the wrong HEAD was accepted.")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DHELPER_SOURCE_DIR=${INPUTNEXTADAPTERS_SOURCE_DIR}"
        "-DSOURCE_DIRECTORY=${_source_bundle_root}/missing-git"
        -DEXPECTED_REVISION=0000000000000000000000000000000000000000
        -P "${_git_fixture_script}"
    RESULT_VARIABLE _git_missing_result
    OUTPUT_VARIABLE _git_missing_output
    ERROR_VARIABLE _git_missing_error
)
if(_git_missing_result EQUAL 0)
    message(FATAL_ERROR "An ordinary missing Git submodule HEAD was accepted.")
endif()

set(_fallout4_root "${_source_bundle_root}/fallout4-patch-mismatch")
set(_commonlib_root "${_fallout4_root}/dependencies/fallout4/commonlibf4")
set(_shared_root "${_commonlib_root}/lib/commonlib-shared")
set(_spdlog_root "${_fallout4_root}/dependencies/fallout4/spdlog")
set(_fallout4_output "${_fallout4_root}/receipt-build")
set(_fallout4_sentinel "${_fallout4_root}/dependency-cmake-ran.txt")
file(MAKE_DIRECTORY "${_shared_root}" "${_spdlog_root}" "${_fallout4_output}"
    "${_commonlib_root}/include/RE/B")
file(WRITE "${_commonlib_root}/CMakeLists.txt"
    "file(WRITE \"${_fallout4_sentinel}\" \"CommonLibF4 executed\\n\")\n")
file(WRITE "${_commonlib_root}/include/RE/B/BSAnimationGraphManager.h" "unapproved patch content\n")
file(WRITE "${_shared_root}/CMakeLists.txt" "# fake pinned submodule source\n")
file(WRITE "${_spdlog_root}/CMakeLists.txt"
    "file(WRITE \"${_fallout4_sentinel}\" \"spdlog executed\\n\")\n")
file(SIZE "${_commonlib_root}/CMakeLists.txt" _commonlib_cmake_size)
file(SHA256 "${_commonlib_root}/CMakeLists.txt" _commonlib_cmake_sha)
file(SIZE "${_commonlib_root}/include/RE/B/BSAnimationGraphManager.h" _patch_file_size)
file(SHA256 "${_commonlib_root}/include/RE/B/BSAnimationGraphManager.h" _patch_file_sha)
file(SIZE "${_shared_root}/CMakeLists.txt" _shared_cmake_size)
file(SHA256 "${_shared_root}/CMakeLists.txt" _shared_cmake_sha)
file(SIZE "${_spdlog_root}/CMakeLists.txt" _spdlog_cmake_size)
file(SHA256 "${_spdlog_root}/CMakeLists.txt" _spdlog_cmake_sha)
set(_fallout4_manifest "${_fallout4_root}/inputnext-adapters-source.json")
file(WRITE "${_fallout4_manifest}"
    "{\n"
    "  \"schemaVersion\": 1,\n"
    "  \"kind\": \"inputnext-adapters-source-bundle\",\n"
    "  \"product\": \"fallout4\",\n"
    "  \"dependencies\": [{\n"
    "    \"name\": \"commonlibf4\",\n"
    "    \"repository\": \"https://github.com/libxse/commonlibf4.git\",\n"
    "    \"revision\": \"7c8c6f8a349ac85abf6fc8a7e5a71732d9339954\",\n"
    "    \"path\": \"dependencies/fallout4/commonlibf4\",\n"
    "    \"fetchContent\": \"fallout4_commonlibf4\",\n"
    "    \"patches\": [{\"id\": \"inputnext-commonlibf4-animation-manager-destructor-v1\", \"path\": \"include/RE/B/BSAnimationGraphManager.h\", \"sha256\": \"5c483703ba54a10a7ec051109072acfd866ea24376dad3a59ad8f93729a3facb\"}],\n"
    "    \"submodules\": [{\"name\": \"commonlib-shared\", \"path\": \"lib/commonlib-shared\", \"repository\": \"https://github.com/libxse/commonlib-shared.git\", \"revision\": \"29fbdb0e2dc548c9ab22f6964981d75090dc9094\"}],\n"
    "    \"files\": [\n"
    "      {\"path\": \"CMakeLists.txt\", \"size\": ${_commonlib_cmake_size}, \"sha256\": \"${_commonlib_cmake_sha}\"},\n"
    "      {\"path\": \"include/RE/B/BSAnimationGraphManager.h\", \"size\": ${_patch_file_size}, \"sha256\": \"${_patch_file_sha}\"},\n"
    "      {\"path\": \"lib/commonlib-shared/CMakeLists.txt\", \"size\": ${_shared_cmake_size}, \"sha256\": \"${_shared_cmake_sha}\"}\n"
    "    ]\n"
    "  }, {\n"
    "    \"name\": \"spdlog\",\n"
    "    \"repository\": \"https://github.com/gabime/spdlog.git\",\n"
    "    \"revision\": \"79524ddd08a4ec981b7fea76afd08ee05f83755d\",\n"
    "    \"path\": \"dependencies/fallout4/spdlog\",\n"
    "    \"fetchContent\": \"spdlog\",\n"
    "    \"patches\": [],\n"
    "    \"submodules\": [],\n"
    "    \"files\": [{\"path\": \"CMakeLists.txt\", \"size\": ${_spdlog_cmake_size}, \"sha256\": \"${_spdlog_cmake_sha}\"}]\n"
    "  }]\n"
    "}\n"
)
_fallout4_patch_bundle_run(
    "${_fallout4_manifest}" "${_fallout4_output}" "${_fallout4_sentinel}"
    _fallout4_patch_result _fallout4_patch_output)
if(_fallout4_patch_result EQUAL 0 OR EXISTS "${_fallout4_sentinel}")
    message(FATAL_ERROR
        "An unapproved CommonLibF4 amendment ran dependency CMake before rejection:\n${_fallout4_patch_output}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/NativeDependenciesTests.cmake")
