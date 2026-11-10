# InputNextSkyrim

Adaptive triggers and vibration for Skyrim Special Edition and Anniversary Edition through InputNext. Feedback follows weapon actions, spellcasting, impacts and player state.

[Effects](docs/effects.md) · [Build and testing](docs/validation.md) · [Architecture](docs/architecture.md)

## Features

- Distinct melee, bow, crossbow, magic, shout, and damage effects.
- One-, two-, and three-word shout impulses when the native variation is available.
- Independent feedback for the left and right hands.
- Blocking resistance and a combat low-health heartbeat.
- Timed health lightbar feedback and optional sneaking/detection colors.
- USB audio haptics with classic rumble fallback.
- Global strength controls and individual effect profiles.

## Requirements

- 64-bit Windows and a Skyrim runtime listed below.
- SKSE and Address Library matching the game runtime.
- The x64 InputNext runtime; see [runtime setup](../../docs/runtime-setup.md).
- The Microsoft Visual C++ x64 Redistributable required by the plugin's build.

| Store | Accepted runtime versions |
| --- | --- |
| Steam | 1.5.97, 1.6.1170, 1.6.1179, 1.7.104 |
| GOG | 1.6.659, 1.6.1170, 1.6.1179 |
| Epic Games Store | 1.6.678 |

VR is unsupported. DLC is optional. DualSense and DualSense Edge require USB for feedback; HD haptics also require the controller's audio device to be available.

## Installation

1. Close Skyrim and install [x64 InputNext](../../docs/runtime-setup.md) beside `SkyrimSE.exe`.
2. Install SKSE and Address Library matching your game version.
3. Download the `InputNextAdapters-skyrim-se-*-windows-x64.zip` package from [Skyrim SE releases](https://github.com/osyka-yuri/InputNextAdapters/releases?q=skyrim-se).
4. Merge the plugin package's `Data/` folder into Skyrim's `Data/`, or install it with a mod manager. The DLL and INI files belong in `Data/SKSE/Plugins/`.
5. Launch through SKSE.

Keep the package's notices and licenses under `licenses/` with the plugin. Use one plugin or other direct-effect integration per controller slot.

## Settings

The INI is read at startup. Restart Skyrim after editing it. Missing settings use built-in defaults.

| Key | Default | Purpose |
| --- | --- | --- |
| `ControllerSlot` | `0` | InputNext slot, 0–3 |
| `TriggerScale` | `1.0` | Trigger strength, 0–1 |
| `RumbleScale` | `1.0` | Body strength for HD and classic output, 0–1 |
| `BodyEnabled` | `true` | Enable body effects |
| `AudioHapticsEnabled` | `true` | Use compiled audio profiles and optional bank overrides |
| `TriggersEnabled` | `true` | Enable adaptive triggers |
| `RespectGameVibration` | `true` | Follow Skyrim's Vibration option for body effects |

The independent `[LED]` group controls timed health feedback and a blue-to-magenta gradient while actually sneaking. Unavailable detection data suppresses stealth; raw score endpoints may need tuning. See [lightbar settings, priorities and source limits](../../docs/leds.md).

To keep InputNext body effects while disabling Skyrim's original vibration, set `RespectGameVibration=false` and turn off Vibration in the game. Triggers are independent of that option.

Copy only the overrides you need from [InputNextSkyrim.reference.ini](InputNextSkyrim.reference.ini) into [InputNextSkyrim.ini](InputNextSkyrim.ini). The reference file is not loaded. Each section may appear once.

Walk, run, sneak, mounted walk and mounted run effects are disabled by default; sprint and mounted sprint (gallop) remain enabled. `Scale` keys multiply built-in strengths, `FrequencyHz` uses hertz, and `VibrationFrequencyCode` uses device codes. `default` retains the built-in frequency or resistance position.

Landing feedback grows with fall height, including ordinary jumps. Entering and leaving water produce no feedback. See [landing behavior](docs/effects.md#landings) for detection limits.

Invalid, unknown, or duplicate settings prevent startup. The [startup log](#diagnostics) identifies the configuration error.

## Compatibility

Menus suppress gameplay effects. Loading, death, lost focus, stale game state and controller changes discard old effects; feedback resumes from fresh state. Hot unloading is unsupported.

## Diagnostics

Look for `InputNextSkyrim.log` in your Windows Documents folder under either:

- `My Games/Skyrim Special Edition/SKSE/`
- `My Games/Skyrim Special Edition GOG/SKSE/`

Check both folders if needed; the folder name may differ from your storefront. Configuration errors include the INI path and reason, with the line, section and key when available.

## Uninstall

Close Skyrim and remove the plugin's DLL and INI files from `Data/SKSE/Plugins/`, or uninstall it through your mod manager. [InputNext is removed separately](../../docs/runtime-setup.md#uninstall).

## License

Authored code is [MIT licensed](licenses/InputNext-MIT.txt). CommonLibSSE-NG has GPL-3.0-or-later terms and exceptions. See [third-party notices](THIRD_PARTY_NOTICES) for distribution requirements.
