include("${HELPER_SOURCE_DIR}/cmake/DependencySources.cmake")
inputnext_require_source_git_revision(
    "${SOURCE_DIRECTORY}"
    "${EXPECTED_REVISION}"
    "Temporary submodule fixture"
)
