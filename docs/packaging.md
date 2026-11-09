# Product packages

The host CLI creates one versioned binary ZIP, symbols ZIP, and source ZIP from a completed native package build. Use an empty output directory outside the checkout and keep the report there:

```powershell
inputnext-adapters-ci package `
  --root "$PWD" `
  --toolset "$PWD/build/ci/toolchain.json" `
  --product foundation `
  --architecture x64 `
  --build-directory "$PWD/build/ci/builds/foundation-x64-package" `
  --output "$env:TEMP/inputnext-foundation-x64" `
  --report "$env:TEMP/inputnext-foundation-x64/package-report.json" `
  --mode development `
  --jobs 16
```

The output names are:

- `InputNextAdapters-{product}-{version}-windows-{architecture}.zip`
- `InputNextAdapters-{product}-{version}-windows-{architecture}-symbols.zip`
- `InputNextAdapters-{product}-{version}-source.zip`

Each ZIP includes `manifest.json` with the product version, architecture where applicable, source identity and a sorted path/size/SHA-256 inventory for every other file. The command verifies the ZIP before moving it to the output directory. Foundation binaries use `InputNextAdaptersBinary`; their two compile PDBs use `InputNextAdaptersSymbols`. Native adapters use their own binary and symbol components.

## Source packages

Skyrim SE and Fallout 4 source ZIPs contain the pinned dependency and submodule files used by the Windows build. These include OpenVR's headers and Windows library and CommonLibF4's shared submodule. The OpenVR macOS framework is excluded. Authored files retain their repository-relative paths. Git metadata, generated build files, DLLs and GameInput redistributables are excluded. Skyrim LE includes SKSE acquisition and preparation instructions. Its official SKSE archive stays outside the ZIP.

Native source pins and approved amendments live in [NativeDependencies.json](../cmake/NativeDependencies.json). CMake reads this catalog, and the Rust verifier embeds it from its trusted source tree. Build receipts and packaged manifests must match those identities and the actual source inventories.

CMake writes `inputnext-adapters-dependency-sources.json` in the build directory, recording the product and each dependency's source directory, repository, revision and patches. Packaging accepts those directories only under the build tree's `_deps/`.

The source ZIP's `inputnext-adapters-source.json` uses schema 1. It records the bundle kind, product, dependency identities, relative paths, amendments and submodule revisions, plus sorted file inventories with sizes and SHA-256 hashes. CMake checks canonical paths and complete contents before enabling FetchContent overrides.

## Package checks

The package command checks native PE architecture and the exact named exports, then tests the extracted DLL with the already-built native probes. Required product test identities must be present. The CMake package path is restored to its build-tree default afterward.

Foundation binaries are tested from a relocated install prefix. Every source ZIP is extracted and built without Git metadata. SE/F4 consumers supply `INPUTNEXTADAPTERS_SOURCE_BUNDLE` for dependency verification.

## Build an extracted source package

Use a source ZIP created by the package command above. If using a published package, verify its published SHA-256 before extraction. To configure an extracted SE source bundle, run from an x64 Visual Studio developer shell:

```powershell
$sourceRoot = "$env:TEMP/inputnext-skyrim-se-source"
cmake -S "$sourceRoot/adapters/skyrim_se" -B "$env:TEMP/inputnext-skyrim-se-consumer" -G Ninja `
  -DINPUTNEXT_SKYRIM_BUILD_PLUGIN=ON `
  -DINPUTNEXT_SKYRIM_BUILD_TESTS=OFF `
  -DINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH="$sourceRoot/inputnext-adapters-source.json" `
  -DFETCHCONTENT_FULLY_DISCONNECTED:BOOL=ON
cmake --build "$env:TEMP/inputnext-skyrim-se-consumer" --target InputNextSkyrim --parallel 16
```

For Fallout 4, use the matching adapter options and target:

```powershell
$sourceRoot = "$env:TEMP/inputnext-fallout4-source"
cmake -S "$sourceRoot/adapters/fallout4" -B "$env:TEMP/inputnext-fallout4-consumer" -G Ninja `
  -DINPUTNEXT_FALLOUT4_BUILD_PLUGIN=ON `
  -DINPUTNEXT_FALLOUT4_BUILD_TESTS=OFF `
  -DINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH="$sourceRoot/inputnext-adapters-source.json" `
  -DFETCHCONTENT_FULLY_DISCONNECTED:BOOL=ON
cmake --build "$env:TEMP/inputnext-fallout4-consumer" --target InputNextFallout4 --parallel 16
```

Release packaging sets `INPUTNEXTADAPTERS_RELEASE_BUILD=ON`, which rejects source bundles and FetchContent overrides. `INPUTNEXTADAPTERS_PRODUCTION` controls optimization separately.
