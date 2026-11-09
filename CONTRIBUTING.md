# Contributing

Keep each change focused on the shared library or one game adapter. Game observations, event interpretation, settings, and effect recipes belong in that adapter; runtime connection and reusable delivery belong in the shared library.

## Build and test

Use the architecture-matched Visual Studio Developer PowerShell and follow [build and integration](docs/building.md). Native adapters have separate build instructions. Test the behavior changed, including failure and recovery paths where relevant.

Catch2 cases are registered individually with CTest, which supplies fixture paths and selects tests by name:

```powershell
ctest --test-dir build/msvc-x64 --no-tests=error --output-on-failure `
  -R '^runtime_client\.client_move_operations$'
```

Add `TEST_CASE` to the relevant unit, component or integration target; `catch_discover_tests` registers it automatically. Standalone probes use explicit `add_test` entries. Cases must prepare their own state and release hooks, threads, modules and temporary resources on early returns and exceptions.

Keep both vendored SDK headers byte-identical to their source. Updates follow [SDK provenance](docs/sdk.md).

## Formatting and analysis

The local checks require **PowerShell 7.6+**, **PSScriptAnalyzer 1.25+**, and **LLVM 23.1.5** on `PATH`.
Run from the repository root:

```powershell
pwsh -File tools/check_quality.ps1
```

The command checks PowerShell formatting and analysis, then runs one batched clang-format check over owned C/C++ files in `adapters`, `include`, `src`, `tests`, `examples`, and `tools`. Third-party, generated, and build directories are excluded. Use `-ApplyFormat` to apply the same formatters.

Use the dedicated quality preset to run clang-tidy with the MSVC x64 target build:

```powershell
pwsh -File tools/verify.ps1 -Project foundation -Preset msvc-x64-quality
```

For a native plugin, use the adapter's `*-native-quality` preset and matching `-Project`; see its build guide. clang-tidy runs only on owned targets, using their MSVC options with PCH flags excluded from parsing. The compiler still uses its PCH. CMake verifies each applicable `.clang-tidy` configuration before compilation.

The quality build also reports cognitive complexity through the root `.clang-tidy` configuration.

Verification logs and CTest JUnit results are written to a unique directory under `build/verification`. Use `-Jobs` to override build concurrency; the default is the available logical processor count.

## Host tooling

Rust tooling uses the pinned toolchain and workspace lints. Keep product selection, package validation and release checks in their respective modules; run external commands through the bounded process runner.

```powershell
cargo fmt --all -- --check
cargo clippy --locked --workspace --all-targets -- -D warnings
cargo test --locked --workspace
```

See [CI and releases](docs/ci.md) for workflow scope and independent product versions. Local native builds remain available through `tools/verify.ps1`.

## Documentation

Describe current behavior and working commands. Keep README files focused on the product and installation. Put API contracts in reference guides; keep audit reports, task history, research drafts, and local benchmark results outside the repository's product documentation.

Keep supported game versions, settings references, and build commands synchronized with the implementation.
