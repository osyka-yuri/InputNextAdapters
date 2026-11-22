# InputNext runtime setup

Install InputNext beside the game executable before adding a plugin. Use the package matching the game process, even on 64-bit Windows.

## Install InputNext

| Game | Executable | InputNext package |
| --- | --- | --- |
| Skyrim SE/AE | `SkyrimSE.exe` | x64 XInput |
| Skyrim Classic/LE | `TESV.exe` | x86 XInput |
| Oblivion Classic | `Oblivion.exe` | x86 DirectInput 8 |
| Fallout New Vegas | `FalloutNV.exe` | x86 XInput |
| Fallout 4 | `Fallout4.exe` | x64 XInput |

Close the game and back up any existing input DLL you are replacing. Copy the files for your game beside its executable. Keep them from the same InputNext build and update them together.

### Skyrim, New Vegas, and Fallout 4

Use these files from the matching XInput archive's `bin/` directory.

```text
xinput1_3.dll
xinput1_4.dll
xinput9_1_0.dll
inputnext_runtime.dll
GameInputRedist.dll
GameInputRawInputProxy.exe
```

### Oblivion

Use an x86 InputNext build that includes the DirectInput 8 facade. Look for these four files in the archive's `bin/` directory.

```text
dinput8.dll
inputnext_runtime.dll
GameInputRedist.dll
GameInputRawInputProxy.exe
```

For a source build, use an InputNext revision that provides `inputnext_dinput_bundle`. Run from the InputNext repository root in an x86 Visual Studio Developer PowerShell.

```powershell
cmake --preset windows-x86-production
cmake --build --preset windows-x86-production --target inputnext_dinput_bundle
```

The four deployment files are in `build/windows-x86-production/xinput/`. [Oblivion installation and bindings](../adapters/oblivion/README.md#installation)

### Check input

Follow the InputNext build's Windows requirements. If the GameInput system service is missing, install Microsoft's GameInput redistributable. Connect the controller and check that the game reads its buttons and sticks before adding the plugin. [Game adapters](../adapters/README.md)

## Controller feedback

Use USB for DualSense and DualSense Edge feedback. HD haptics require the controller's audio device to be available; otherwise the plugin uses classic rumble. Sony Bluetooth currently supports input only.

Use one plugin or other direct-effect integration per selected controller slot.

## Uninstall

Close the game, remove the files installed from InputNext and restore the backed-up originals. Remove each game plugin separately using its installation guide.
