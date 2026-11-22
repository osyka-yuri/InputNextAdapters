# InputNext for Fallout 3

This x86 plugin checks the Fallout 3 and FOSE versions and attempts to write a startup entry to `Data/FOSE/Plugins/InputNextFallout3.log`. A compatible plugin load succeeds even when the log is unavailable. The plugin reports the failure through `OutputDebugStringA` for debugger or DebugView diagnostics.

## Supported runtime

- Windows x86 `Fallout3.exe` 1.7.0.3 and FOSE 1.3 beta 2.
- Standalone DLL with the MSVC C++ runtime linked statically.

The FOSE project's link to the 1.3 beta 2 source archive returns HTTP 404. The build pins the official FOSE 1.2 beta 2 SDK by SHA-256 and uses the Query and Load interface prefix shared with FOSE 1.3 beta 2. [FOSE downloads](https://www.fose.silverlock.org/)

## Installation and check

1. Install the matching x86 FOSE runtime for `Fallout3.exe` 1.7.0.3.
2. Copy `InputNextFallout3.dll` to `Fallout 3/Data/FOSE/Plugins/`.
3. Start Fallout 3 through FOSE.
4. Confirm that `Data/FOSE/Plugins/InputNextFallout3.log` contains a `loaded through FOSE 1.3 beta 2` entry. If the file is unavailable, check debugger output for an `Optional startup receipt could not be written` message. Version incompatibility still causes FOSE to reject the plugin.

## Build and test

Use an x86 Visual Studio Developer PowerShell with CMake 4.4.4 or newer.

The build caches the FOSE SDK archive after verifying its SHA-256. Failed downloads and hash mismatches discard the temporary file so the next configuration can retry. Use `INPUTNEXT_FALLOUT3_FOSE_ARCHIVE` to supply a local copy of the pinned archive.

```powershell
cmake --preset msvc-x86-native-tests
cmake --build --preset msvc-x86-native-tests
ctest --preset msvc-x86-native-tests
cmake --build --preset msvc-x86-native-tests --target fallout3_package
```

The packaging target checks that the plugin is an x86 DLL with the two FOSE exports and no dynamic CRT dependencies. It verifies the archive contents and the packaged DLL before writing `build/fallout3-msvc-x86-native-tests/package/InputNextFallout3-0.1.0-windows-x86.zip`. The archive includes the plugin, this guide, and the InputNextAdapters license.
