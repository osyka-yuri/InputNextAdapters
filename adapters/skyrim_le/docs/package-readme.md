# InputNextSkyrimLE

Vibration and health lightbar feedback for Skyrim Classic and Legendary Edition through InputNext.

## Installation

1. Close Skyrim and back up existing files before replacing them.
2. Copy all six files from the x86 InputNext runtime package's `bin/` beside `TESV.exe`.
3. Install SKSE 1.7.3 for Skyrim 1.9.32.0.
4. Merge this package's `Data/` folder into Skyrim's `Data/`, or install it with a mod manager. Launch through SKSE.

The Microsoft Visual C++ x86 Redistributable is required. VR and hot unloading are unsupported. Use USB for DualSense and DualSense Edge feedback. HD haptics require the controller's audio device to be available; Bluetooth supports input only.

## Settings and removal

Edit `Data/SKSE/Plugins/InputNextSkyrimLE.ini`, then restart Skyrim. Copy the overrides you need from the reference INI; it is not loaded automatically. Each section may appear once. Invalid settings prevent startup.

Stock audio haptics are built in. An optional `InputNextSkyrimLE.audiohaptics` file beside the INI overrides matching profiles. Missing or invalid files retain the defaults. Set `AudioHapticsEnabled=false` in `[InputNextSkyrim]` to use synthetic effects.

LE supports melee, incoming hits, shouts, short bow draw/release cues, health, stamina and beast-form feedback. Adaptive triggers, magic, sustained bow tension, footsteps and mounted feedback are unavailable. Older body and trigger settings remain accepted; unsupported effects stay inactive.

The independent `[LED]` group enables timed health feedback. LE has no stealth colors. Player and microphone indicators are off while the adapter controls the LED group.

For startup errors, check `Data/SKSE/Plugins/InputNextSkyrimLE.log`. A `configuration=invalid line=N` record points to an INI error; zero means no specific line is available. Detailed reasons are sent to Windows debug output during startup.

To remove the plugin, close Skyrim and remove its DLL and INI files, or uninstall it through your mod manager. InputNext is installed and removed separately.

Keep the notices and license texts under `licenses/` with the plugin.
The source archive contains build and binding guides. Obtain SKSE from its [official site](https://skse.silverlock.org/).
