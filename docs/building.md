# Build and integration

The library requires Windows, CMake 4.4.4+, Ninja and a C++23 MSVC-compatible toolchain. MSVC and clang-cl are supported on x86 and x64. The root build verifies the vendored SDK hashes and downloads no game SDK.

## Build and test

Open a Visual Studio Developer PowerShell matching the target architecture. The verification script requires PowerShell 7.6+ and runs configure, build and CTest for one preset:

```powershell
pwsh -File tools/verify.ps1 -Preset msvc-x64
pwsh -File tools/verify.ps1 -Preset msvc-x86
```

Use `msvc-x64` in an x64 Developer PowerShell and `msvc-x86` in an x86 Developer PowerShell. A preset checks bitness but does not switch the compiler environment. Build parallelism defaults to the process's available logical processor count. Use `-Jobs` to override it.

Tests default on for a top-level build and off under `add_subdirectory`. Set `INPUTNEXTADAPTERS_BUILD_TESTS` explicitly to override that choice. Keep the same diagnostic language and code page for configure and build. After changing compiler or encoding, use `--fresh` when configuring and `--clean-first` when building.

## CMake targets

| Target | Contents |
| --- | --- |
| `InputNextAdapters::runtime_client` | Runtime loading and direct ABI calls |
| `InputNextAdapters::effects` | Output sessions and body waveform generation; links the runtime client |

The project follows the parent's compiler, architecture, and CRT settings. Warnings and analysis options apply to project targets.

## Use from source

```cmake
add_subdirectory(path/to/InputNextAdapters inputnext_adapters)
target_link_libraries(my_game_adapter PRIVATE InputNextAdapters::effects)
```

Link only `InputNextAdapters::runtime_client` when you need the direct client without shared effect delivery.

The read-only probe is a standalone consumer:

```powershell
cmake -S examples/effects_probe -B build/effects-probe-source -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DINPUTNEXTADAPTERS_SOURCE_DIR="$PWD"
cmake --build build/effects-probe-source
```

Run `effects_probe.exe --help`. With an absolute path to a trusted runtime DLL, it inspects targets and capabilities; it submits no effects.

## Install and use a package

```powershell
cmake --install build/msvc-x64 --prefix build/install
cmake -S examples/effects_probe -B build/effects-probe-installed -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$PWD/build/install"
cmake --build build/effects-probe-installed
```

In another project:

```cmake
find_package(InputNextAdapters CONFIG REQUIRED)
target_link_libraries(my_game_adapter PRIVATE InputNextAdapters::effects)
```

The installation includes both libraries, public headers, CMake configuration, SDK provenance, and licenses. The package can be relocated. Its architecture, toolchain, and CRT must match the consumer; use a source build when these settings need to follow the host project.

For a Release build, install the libraries and symbols separately:

```powershell
cmake --install build/msvc-x64-release --component InputNextAdaptersBinary --prefix build/install
cmake --install build/msvc-x64-release --component InputNextAdaptersSymbols --prefix build/symbols
```

Use `msvc-x86-release` for x86. Both Release presets disable tests and have no CTest preset. Native adapters use their own install components; see their build guides.

Native package probes use the build-tree DLL by default. To probe a DLL extracted from a package, configure with `-DINPUTNEXTADAPTERS_PACKAGE_PLUGIN:FILEPATH=<absolute-DLL-path>`; CMake resolves the canonical path and requires an existing `.dll` file before registering the probe.

## Corresponding source dependencies

SE and Fallout 4 source packages include their pinned dependencies and submodules. For a published package, verify its archive hash before extraction; locally built packages are verified by the package command. Pass `-DINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH=<bundle-root>/inputnext-adapters-source.json` to build without Git metadata. CMake checks paths, dependency identities and complete file inventories before using those sources. Skyrim LE obtains SKSE separately.

Release packaging sets `INPUTNEXTADAPTERS_RELEASE_BUILD=ON`, which rejects source bundles and FetchContent source overrides. This is separate from the optimization option `INPUTNEXTADAPTERS_PRODUCTION`. See [product packages](packaging.md) for bundle contents and build examples.

## Analysis and sanitizers

Use separate profiles for ordinary builds, memory checks, undefined-behavior checks, and static analysis:

| Preset | Purpose |
| --- | --- |
| `msvc-x64`, `msvc-x86` | Debug builds and tests |
| `msvc-x64-quality` | x64 tests with clang-tidy attached to owned targets |
| `msvc-x64-release`, `msvc-x86-release` | Optimized MSVC Release libraries with LTCG and full symbols |
| `clang-cl-x64` | Debug build and tests with Clang |
| `msvc-x64-asan` | x64 AddressSanitizer with debug information |
| `clang-cl-x64-ubsan` | x64 UBSan with the static CRT |
| `msvc-x64-analysis` | MSVC `/analyze` and per-object SARIF logs |

Test-enabled profiles use the same name for configure, build, and CTest:

```powershell
cmake --preset clang-cl-x64-ubsan
cmake --build --preset clang-cl-x64-ubsan
ctest --preset clang-cl-x64-ubsan
```

Release presets enable `INPUTNEXTADAPTERS_PRODUCTION`: owned targets use `/O2 /Ob3 /Gw /Gy`, LTCG, `/fp:precise` and full symbols. Executables and DLLs enable linker warnings as errors, reference elimination, identical COMDAT folding, ASLR and DEP; x64 also enables high-entropy ASLR. PDB references contain a filename rather than an absolute producer path. Native plugin presets use the same policy.

Production optimization defaults off for source consumers and preserves their CRT and architecture. Sanitizer and `/analyze` profiles bypass it. These presets use LTCG without PGO training.

The quality preset uses a separate MSVC x64 build directory and requires clang-tidy 23.1.5 on `PATH`. Ordinary presets do not enable tidy. See [Contributing](../CONTRIBUTING.md) for formatting and analysis commands.

The corresponding options are `INPUTNEXTADAPTERS_ENABLE_ASAN`, `INPUTNEXTADAPTERS_ENABLE_UBSAN` and `INPUTNEXTADAPTERS_ENABLE_MSVC_ANALYZE`, all off by default. ASan and UBSan cannot be enabled together. `/analyze` requires genuine MSVC; external compiler headers are excluded.

Instrumented libraries publish sanitizer compile and link requirements to consumers. Installed packages resolve runtimes through the consumer's compiler, without storing producer toolchain paths. Source and relocated-package tests include an intentional error that must be detected.

clang-cl ASan requires a clang-cl consumer and an available ASan runtime DLL at launch. The package resolves the import library and runtime thunk through the consumer's compiler. The thunk follows the executable's CRT: an unset `MSVC_RUNTIME_LIBRARY` uses CMake's dynamic default; an explicitly empty value uses clang-cl's static default.

LLVM 23.1.5 rejects `/MDd` and `/MTd` with ASan. A Debug ASan build must explicitly choose a release CRT, for example `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL`. Consumer checks preserve that choice. Registered project tests locate the runtime through their own environment; applications must deploy or otherwise locate it.

UBSan requires x64 clang-cl and a working C++ runtime, checked during configure. The reference profile selects `RelWithDebInfo` and `MultiThreaded` (`/MT`) for the pinned LLVM runtime. The library preserves the host project's CRT choice; an incompatible choice fails the runtime link check. Undefined behavior stops at the first diagnostic.

Native SE/AE and Fallout 4 projects have their own profiles. Fallout 4's native plugin requires MSVC; its UBSan profile checks only isolated targets. Skyrim LE is x86 and has no supported sanitizer profile.

## Consumer checks

CTest includes source and installed-package consumers. With Ninja, `test_prep/<test-name>` or `test_prep/all` builds test dependencies without running them:

```powershell
cmake --build build/msvc-x64 --target test_prep/inputnext_adapters_consumer_installed
ctest --test-dir build/msvc-x64 --output-on-failure `
  -R '^inputnext_adapters_consumer_installed$' --no-tests=error
```

For an explicit relocated-package check, install to a scratch prefix first:

```powershell
cmake -DINPUTNEXTADAPTERS_MODE=installed `
  -DINPUTNEXTADAPTERS_SOURCE_DIR="$PWD" `
  -DINPUTNEXTADAPTERS_BINARY_DIR="$PWD/build" `
  -DINPUTNEXTADAPTERS_INSTALL_PREFIX="$PWD/build/install" `
  -DINPUTNEXTADAPTERS_CXX_COMPILER=cl `
  -DINPUTNEXTADAPTERS_CONFIGURATION=Debug `
  -P tests/cmake/VerifyConsumer.cmake
```

Match the installed library's compiler, architecture, and configuration. `INPUTNEXTADAPTERS_MODE=add_subdirectory` checks the source consumer instead.

Native plugins are separate projects; see [game adapters](../adapters/README.md).
