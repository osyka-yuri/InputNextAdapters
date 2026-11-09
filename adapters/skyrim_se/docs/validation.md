# Skyrim SE/AE build and testing

Use an x64 Visual Studio Developer PowerShell with CMake 4.4.4+, Ninja, and a C++23 toolchain. Commands run from `adapters/skyrim_se/`.

## Isolated tests

```powershell
cmake --preset msvc-x64
cmake --build --preset msvc-x64
ctest --preset msvc-x64
```

These tests need no game SDK and fetch only pinned test dependencies. CI checks isolated targets and the native plugin separately.

## Native plugin

```powershell
cmake --preset msvc-x64-native-tests
cmake --build --preset msvc-x64-native-tests
ctest --preset msvc-x64-native-tests
cmake --install ../../build/skyrim-se-msvc-x64-native-tests --component SkyrimAdapter `
  --prefix ../../build/skyrim-package
cmake --install ../../build/skyrim-se-msvc-x64-native-tests --component SkyrimSymbols `
  --prefix ../../build/skyrim-symbols
```

Enabling the plugin fetches CommonLibSSE-NG and dependencies pinned by the [native dependency catalog](../../../cmake/NativeDependencies.json). See [third-party notices](../THIRD_PARTY_NOTICES) for terms. Development builds can reuse the same sources offline through CMake FetchContent source overrides.

The source build enables SE/AE and disables VR. Staging creates the DLL and settings under `Data/SKSE/Plugins/`, a package README at the root, and notices and licenses under `licenses/`. It does not install InputNext or SKSE.

MSVC Release uses `/O2` and LTCG for owned production targets, with full symbols installed separately. Diagnostic profiles bypass production optimization.

A distributed binary must carry applicable terms and matching corresponding source, including adapter/client/SDK, pinned dependencies, and build scripts. A local install tree alone is not a complete distribution.

Tests cover policy, waveform generation, configuration, lifecycle, finite output, recovery, and native value conversion. They use explicit time, fake output, and native fixtures without running Skyrim.

With the native plugin and tests enabled, `skyrim_plugin_package` loads the actual DLL, checks the SKSE exports,
and verifies that rejected startup neither loads InputNext nor pins the plugin. It does not initialize SKSE or exercise game callbacks.

Before distribution, check the staged DLL's architecture and imports, verify SDK hashes and confirm the exact exports: `SKSEPlugin_Load`, `SKSEPlugin_Query` and `SKSEPlugin_Version`.

## Runtime package checks

Set `INPUTNEXTADAPTERS_RUNTIME_DIRECTORY` to a matching InputNext package's `bin` directory to register
`skyrim.package.*`. These tests validate coordinator-generated trigger, body and cleanup requests against the
real cold runtime: valid formats return `UNAVAILABLE`, malformed controls return `INVALID_ARGUMENT`.
They do not establish accepted live contexts or physical effects.

After configuring the native build above, replace `C:/InputNext/bin` with that directory:

```powershell
cmake -S . -B ../../build/skyrim-se-msvc-x64-native-tests `
  -DINPUTNEXTADAPTERS_RUNTIME_DIRECTORY=C:/InputNext/bin
cmake --build --preset msvc-x64-native-tests
ctest --test-dir ../../build/skyrim-se-msvc-x64-native-tests -R '^skyrim\.package\.' `
  --no-tests=error --output-on-failure
```

Ordinary CI needs no runtime package. Release qualification runs these checks against the selected InputNext packages. Accepted live contexts, delivery, cleanup and physical effects need separate game/controller checks.

## Analysis and formatting

Diagnostic presets `msvc-x64-asan`, `clang-cl-x64-ubsan`, and `msvc-x64-analysis` use the same name for configure, build, and CTest, with separate build directories. They check isolated targets without fetching the game SDK.
`msvc-x64-native` builds only the Release plugin and has no CTest preset.

The package probe normally loads the build-tree DLL. Set `INPUTNEXTADAPTERS_PACKAGE_PLUGIN` to an extracted package DLL to test that binary instead.

ASan and UBSan are mutually exclusive. UBSan uses clang-cl with the static CRT; MSVC analysis writes per-object SARIF logs. See [build profiles](../../../docs/building.md#analysis-and-sanitizers) and [source tools](../../../CONTRIBUTING.md).

Run formatting, native clang-tidy, and CTest for owned test targets from the repository root:

```powershell
pwsh -File tools/check_quality.ps1
pwsh -File tools/verify.ps1 -Project skyrim-se -Preset msvc-x64-native-quality
```

The dedicated quality preset enables the native plugin and tests, retains MSVC for CommonLib, and uses a separate build directory. See [Contributing](../../../CONTRIBUTING.md) for tool versions and options.

For startup logs and configuration errors, see [diagnostics](../README.md#diagnostics).
