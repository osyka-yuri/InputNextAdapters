# InputNextSkyrim

Adaptive triggers, vibration and lightbar feedback for Skyrim Special Edition and Anniversary Edition through InputNext.

## Installation

1. Close Skyrim and back up existing files before replacing them.
2. Copy all six files from the x64 InputNext runtime package's `bin/` beside `SkyrimSE.exe`.
3. Install SKSE and Address Library matching your game version.
4. Merge this package's `Data/` folder into Skyrim's `Data/`, or install it with a mod manager. Launch through SKSE.

Supported runtimes: Steam 1.5.97, 1.6.1170, 1.6.1179 and 1.7.104; GOG 1.6.659, 1.6.1170 and 1.6.1179; Epic 1.6.678. The Microsoft Visual C++ x64 Redistributable is required. VR and hot unloading are unsupported.

Use USB for DualSense and DualSense Edge feedback. HD haptics require the controller's audio device to be available; Bluetooth supports input only.

## Settings and removal

Edit `Data/SKSE/Plugins/InputNextSkyrim.ini`, then restart Skyrim. Copy optional overrides from the reference INI; it is not loaded automatically. Invalid settings prevent startup.

For startup errors, check `InputNextSkyrim.log` in your Windows Documents folder under `My Games/Skyrim Special Edition/SKSE/` or `My Games/Skyrim Special Edition GOG/SKSE/`. The log gives configuration-error details. Check both folders if needed; the folder name may differ from your storefront.

The independent `[LED]` group enables timed health feedback and optional blue-to-magenta sneaking/detection colors. Raw detection endpoints may need tuning. Player and microphone indicators are off while the adapter owns the LED group.

To remove the plugin, close Skyrim and remove its DLL and INI files, or uninstall it through your mod manager. InputNext is installed and removed separately.

Keep the third-party notices and license texts under `licenses/` with the plugin.
The corresponding source archive contains the build, effect and architecture guides.
