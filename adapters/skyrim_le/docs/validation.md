# Skyrim Classic/LE build and testing

Use an x86 Visual Studio Developer PowerShell with CMake 4.4.4+, Ninja and a C++23 toolchain.
Commands run from `adapters/skyrim_le/` unless stated otherwise.

## Isolated tests

```powershell
cmake --preset msvc-x86
cmake --build --preset msvc-x86
ctest --preset msvc-x86
```

These tests need no game SDK. CI checks isolated targets and the native x86 plugin separately.
Shared policy, waveforms, configuration, sessions and native scheduling use controlled values and explicit time.

## Native plugin

```powershell
cmake --preset msvc-x86-native-tests
cmake --build --preset msvc-x86-native-tests
ctest --preset msvc-x86-native-tests
cmake --install ../../build/skyrim-le-msvc-x86-native-tests --component SkyrimLEAdapter `
  --prefix ../../build/skyrim-le-package
cmake --install ../../build/skyrim-le-msvc-x86-native-tests --component SkyrimLESymbols `
  --prefix ../../build/skyrim-le-symbols
```

The native test preset builds the Release plugin and its Query, Load and binding checks without Skyrim installed. Owned production targets use MSVC `/O2` and LTCG, with full symbols installed separately. `msvc-x86-native` disables tests; `msvc-x86-native-analysis` enables static analysis in a separate build.

Configuration retrieves and verifies official SKSE 1.7.3. Offline builds can set
`INPUTNEXT_SKYRIM_LE_SKSE_ARCHIVE`; a source override also requires that archive and must match it.
Original inputs remain untouched. See [SDK preparation and patches](../cmake/SKSE-PATCHES.md).
Distribution source must include the authored build and patch scripts; obtain SKSE from its official site
instead of rehosting its archive or source.

Staging creates the DLL and settings under `Data/SKSE/Plugins/`, a root README and terms under
`licenses/`. Install the x86 InputNext runtime and SKSE separately.

The native tests check x86 SDK layouts and bindings, the Query interface, and rejected Load calls including
throwing foreign callbacks. They load the actual DLL and verify that rejected startup does not load InputNext
or pin the plugin. They do not initialize the game or register live callbacks.

Before distribution, check the staged PE32 DLL for machine `14C`, expected system/CRT imports and exactly the undecorated `SKSEPlugin_Query` and `SKSEPlugin_Load` exports. Verify the SDK hashes.

## Runtime package checks

Set `INPUTNEXTADAPTERS_RUNTIME_DIRECTORY` to a matching native x86 InputNext package's `bin` directory.
After configuring the native build above, replace `C:/InputNext/bin` with that path:

```powershell
cmake -S . -B ../../build/skyrim-le-msvc-x86-native-tests `
  -DINPUTNEXTADAPTERS_RUNTIME_DIRECTORY=C:/InputNext/bin
cmake --build --preset msvc-x86-native-tests
ctest --test-dir ../../build/skyrim-le-msvc-x86-native-tests -R '^skyrim\.package\.' `
  --no-tests=error --output-on-failure
```

The shared Skyrim package tests validate generated trigger, body and cleanup requests against the actual cold
runtime: valid formats return `UNAVAILABLE`, malformed controls return `INVALID_ARGUMENT`. Ordinary CI needs no runtime package. Release qualification runs these checks against the selected InputNext packages. Accepted live contexts, delivery, cleanup and physical effects need separate game/controller checks.

## Analysis and formatting

`msvc-x86-analysis` checks isolated targets; `msvc-x86-native-analysis` includes the native SDK and plugin.
Use the same preset name for configure, build and CTest. The common project's supported ASan/UBSan profiles
are x64 and do not instrument this x86 frontend.

For formatting, native clang-tidy, and CTest on owned test targets, run from the repository root:

```powershell
pwsh -File tools/check_quality.ps1
pwsh -File tools/verify.ps1 -Project skyrim-le -Preset msvc-x86-native-quality
```

`clang-cl-x86` is an alternative SDK-free build. Native clang-cl builds additionally set
`INPUTNEXT_SKYRIM_LE_BUILD_PLUGIN=ON`. The quality preset retains the x86 MSVC native build and enables tests in its own incremental directory. See Contributing for tool versions.

## Diagnostics

`Data/SKSE/Plugins/InputNextSkyrimLE.log` is replaced at startup. If it cannot open, Windows debug output
remains available. Records cover lifecycle, scheduling, observation gates, event admission and output status.

For invalid settings, the file records `configuration=invalid line=N`. Check that line in
`InputNextSkyrimLE.ini`; zero means no specific line is available. The full reason is sent to
Windows debug output. Capture that output while starting Skyrim to read it.

| Record | Meaning |
| --- | --- |
| `task_pool=unavailable` | Scheduler singleton unavailable; submission deferred |
| `first_task_submission` / `first_task_run` | Task submitted / executed |
| First accepted action or hit | Native event admitted |
| Output status or route | Runtime acceptance or fallback decision |
| `menu_state=blocking` | Observed blocker, name, reason and flags |
| `menu_state=clear` | Clear menu observation |
| `menu_state=blocked` | Unreliable snapshot or unavailable menu manager |
| `body_vibration_preference` | Missing, invalid, disabled or enabled game setting |

Menu transitions are capped at 16 records; each vibration-preference state is logged once.
