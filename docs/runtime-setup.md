# InputNext runtime setup

Install InputNext beside the game executable before adding a plugin. Use the package matching the game process, even on 64-bit Windows.

## Install InputNext

| Game | Executable | InputNext package |
| --- | --- | --- |
| Skyrim SE/AE | `SkyrimSE.exe` | x64 |
| Skyrim Classic/LE | `TESV.exe` | x86 |
| Fallout 4 | `Fallout4.exe` | x64 |

1. Close the game and extract the matching InputNext runtime ZIP.
2. Back up existing files with the names below. Replacing another mod's XInput DLL replaces its controller implementation.
3. Copy all six files from the archive's `bin/` beside the game executable, keeping their names:

   ```text
   xinput1_3.dll
   xinput1_4.dll
   xinput9_1_0.dll
   inputnext_runtime.dll
   GameInputRedist.dll
   GameInputRawInputProxy.exe
   ```

4. Connect the controller and check ordinary input before adding the [game plugin](../adapters/README.md).

Use files from the same InputNext package and update them together. Follow its guide for supported Windows versions. If the GameInput system service is missing, install Microsoft's GameInput redistributable.

## Controller feedback

Use USB for DualSense and DualSense Edge feedback. HD haptics require the controller's audio device to be available; otherwise the plugin uses classic rumble. Sony Bluetooth currently supports input only.

Use one plugin or other direct-effect integration per selected controller slot.

## Uninstall

Close the game, remove the files installed from InputNext and restore the backed-up originals. Remove each game plugin separately using its installation guide.
