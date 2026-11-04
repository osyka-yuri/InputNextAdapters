# InputNextFallout4

Adaptive triggers, vibration and lightbar feedback for Fallout 4 through InputNext.

## Installation

1. Close Fallout 4 and back up existing files before replacing them.
2. Copy all six files from the x64 InputNext runtime package's `bin/` beside `Fallout4.exe`.
3. Install F4SE 0.7.9 and Address Library `version-1-11-240-0.bin` for Fallout 4 1.11.240.0.
4. Merge this package's `Data/` folder into Fallout 4's `Data/`, or install it with a mod manager. Launch through F4SE.

The Microsoft Visual C++ x64 Redistributable is required. Other game versions, VR and hot unloading are unsupported. Use USB for DualSense and DualSense Edge feedback. HD haptics require the controller's audio device to be available; Bluetooth supports input only.

## Settings and removal

Edit `Data/F4SE/Plugins/InputNextFallout4.ini`, then restart the game. Copy optional overrides from the reference INI; it is not loaded automatically. Invalid settings prevent startup; check `Documents/My Games/Fallout4/F4SE/InputNextFallout4.log` for the reason.

The independent `[LED]` group enables timed health, optional sneaking/detection colors and current radiation-source presence. Radiation stays unknown until a real event after startup or loading; it does not measure RAD/s. Player and microphone indicators are off while the adapter owns the LED group.

To remove the plugin, close Fallout 4 and remove its DLL and INI files, or uninstall it through your mod manager. InputNext is installed and removed separately.

Keep the notices and license texts under `licenses/` with the plugin.
The corresponding source archive contains the build, effect and native-binding guides.
