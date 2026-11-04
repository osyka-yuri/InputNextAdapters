# Fallout 4 build and testing

Use an x64 Visual Studio Developer PowerShell with PowerShell 7.6+, CMake 4.4.4+, Ninja and a C++23 toolchain.
Commands run from `adapters/fallout4/` unless stated otherwise.

## Isolated tests

```powershell
cmake --preset msvc-x64
cmake --build --preset msvc-x64
ctest --preset msvc-x64
```

These tests cover policy, configuration, sessions, waveforms and native value contracts without a game SDK.
CI checks isolated targets and the native plugin separately.

## Native plugin

```powershell
cmake --preset msvc-x64-native-tests
cmake --build --preset msvc-x64-native-tests
ctest --preset msvc-x64-native-tests
cmake --install ../../build/fallout4-msvc-x64-native-tests --component Fallout4Adapter `
  --prefix ../../build/fallout4-package
cmake --install ../../build/fallout4-msvc-x64-native-tests --component Fallout4Symbols `
  --prefix ../../build/fallout4-symbols
```

The native test preset is also a separate CI matrix job. It builds the DLL and deterministic native tests
without installing or launching Fallout 4. `msvc-x64-native` builds the Release plugin with tests disabled.
Owned production targets use `/O2` and LTCG, with full optimized symbols installed separately.

The build fetches pinned CommonLibF4, commonlib-shared and spdlog. See [third-party notices](../THIRD_PARTY_NOTICES)
for revisions and terms. FetchContent source overrides can reuse Git checkouts with initialized submodules
offline. `cmake/CommonLibF4Patches.cmake` declares an existing engine destructor without implementing it or changing the dependency pin. Corresponding source includes the dependencies and this amendment.

Ordinary builds check the shared submodule's Git `HEAD`. Extracted source packages use a verified bundle manifest instead. Release packaging rejects source overrides. To test an extracted plugin DLL, set `INPUTNEXTADAPTERS_PACKAGE_PLUGIN` to its absolute path.

Staging creates DLL/settings under `Data/F4SE/Plugins/`, a root README and terms under `licenses/`.
Install InputNext, F4SE and the matching Address Library separately. Before distribution, check the staged DLL's architecture and imports, verify SDK hashes and confirm the exact `F4SEPlugin_Load`/`F4SEPlugin_Version` exports.

Native tests cover dispatch installation/forwarding, UI capture, owner startup, callback ownership, sampling
and engine-value conversion. The package probe loads the actual DLL, passes a null F4SE interface, and
checks that InputNext remains unloaded and the plugin can unload. Later startup failures and callback
boundaries are covered by native owner fixtures; live F4SE initialization and hooks require game checks.

## Runtime package checks

Set `INPUTNEXTADAPTERS_RUNTIME_DIRECTORY` to a matching x64 InputNext package's `bin` directory.
After configuring the native test build above, replace `C:/InputNext/bin` with that path:

```powershell
cmake -S . -B ../../build/fallout4-msvc-x64-native-tests `
  -DINPUTNEXTADAPTERS_RUNTIME_DIRECTORY=C:/InputNext/bin
cmake --build ../../build/fallout4-msvc-x64-native-tests
ctest --test-dir ../../build/fallout4-msvc-x64-native-tests -R '^fallout4\.package\.' `
  --no-tests=error --output-on-failure
```

These tests validate session-generated trigger, body and cleanup requests against the real cold runtime:
valid formats return `UNAVAILABLE`, malformed controls return `INVALID_ARGUMENT`. Ordinary CI needs no runtime package. Release qualification runs these checks against the selected InputNext packages. Accepted live contexts, delivery, cleanup, native hooks and physical effects need separate game/controller checks.

## Analysis and formatting

Isolated diagnostic presets `msvc-x64-analysis`, `msvc-x64-asan` and `clang-cl-x64-ubsan` use the same name
for configure, build and CTest. `msvc-x64-native-analysis` includes the native SDK/plugin in a separate MSVC
analysis build. Diagnostic builds do not use production IPO. UBSan is supported only for isolated targets.

The native plugin requires MSVC; a complete clang-cl build of CommonLibF4 is outside the supported matrix.
The native quality preset keeps that MSVC build, runs clang-tidy on owned targets, and includes owned tests. From the repository root:

```powershell
pwsh -File tools/check_quality.ps1
pwsh -File tools/verify.ps1 -Project fallout4 -Preset msvc-x64-native-quality
```

The tidy profile uses a separate build directory and does not change the ordinary native preset. See source checks
and build profiles for tools and supported configurations.

## Diagnostics

The log is `Documents/My Games/Fallout4/F4SE/InputNextFallout4.log`. Set `Logging / Diagnostics=true` in the
INI for additional native records, then restart the game. Records cover output admission, lifecycle controls,
observation mode, animation subscription state and event queue overflow. Capture failures remain quiet.

Report the action, active menus or VATS mode, controller slot, transport, relevant log excerpt and reproduction steps.
