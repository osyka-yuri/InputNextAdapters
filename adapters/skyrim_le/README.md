# InputNextSkyrimLE

Vibration and health lightbar feedback for Skyrim Classic and Legendary Edition. This x86 SKSE plugin uses InputNext and the shared Skyrim effect profiles.

[Native bindings](docs/bindings.md) · [Build and testing](docs/validation.md)

## Features

- Melee swings and hits, incoming hits, shouts, and short bow draw/release cues.
- Low-health and stamina feedback, plus werewolf and vampire-lord transformations.
- Timed health lightbar feedback.
- Adjustable body strength, with USB audio haptics or classic rumble fallback.

Adaptive triggers, magic feedback, sustained bow tension, footsteps and mounted feedback are unavailable in LE. See [native limits](docs/bindings.md#native-limits) for the full list.

## Requirements

- Windows supported by InputNext, Skyrim **1.9.32.0**, and **SKSE 1.7.3**.
- The native x86 InputNext runtime; see [runtime setup](../../docs/runtime-setup.md).
- The Microsoft Visual C++ x86 Redistributable required by the plugin's build.

Dawnguard, HearthFires, and Dragonborn are optional. Vampire-lord detection requires Dawnguard. Creation Club Survival and VR are unsupported.

## Installation

1. Close Skyrim and install [x86 InputNext](../../docs/runtime-setup.md) beside `TESV.exe`.
2. Install SKSE 1.7.3 for Skyrim 1.9.32.0.
3. Merge the plugin package's `Data/` folder into Skyrim's `Data/`, or install it with a mod manager. The DLL and INI files belong in `Data/SKSE/Plugins/`.
4. Launch through SKSE.

Keep the package's notices and licenses under `licenses/` with the plugin. Use one plugin or other direct-effect integration per controller slot.

DualSense and DualSense Edge require USB for feedback. HD haptics also require the controller's audio device to be available; Bluetooth currently supports input only.

## Settings

The INI is read at startup. Restart Skyrim after editing it. Missing settings use built-in defaults.

| Key | Default | Purpose |
| --- | --- | --- |
| `ControllerSlot` | `0` | InputNext slot, 0–3 |
| `RumbleScale` | `1.0` | Body strength for HD and classic output, 0–1 |
| `BodyEnabled` | `true` | Enable body effects |
| `AudioHapticsEnabled` | `true` | Use compiled audio profiles and optional bank overrides |
| `RespectGameVibration` | `true` | Follow Skyrim's Vibration option for body effects |

The independent `[LED]` group controls health feedback. LE has no stealth colors. See [lightbar settings](../../docs/leds.md).

Copy the overrides you need from [InputNextSkyrimLE.reference.ini](InputNextSkyrimLE.reference.ini) into [InputNextSkyrimLE.ini](InputNextSkyrimLE.ini). The reference file is not loaded. Each section may appear once. Older body and trigger options remain accepted for compatibility; unsupported effects stay inactive.

Invalid, unknown or duplicate settings prevent startup. See [diagnostics](docs/validation.md#diagnostics) to locate configuration errors.

## Compatibility and diagnostics

Menus suppress gameplay effects. Loading, death, lost focus and stale game state discard old effects; feedback resumes from fresh state. Hot unloading is unsupported.

The log is `Data/SKSE/Plugins/InputNextSkyrimLE.log`. It records lifecycle and output status; detailed configuration-error reasons use Windows debug output.

## Uninstall

Close Skyrim and remove the plugin's DLL and INI files from `Data/SKSE/Plugins/`, or uninstall it through your mod manager. [InputNext is removed separately](../../docs/runtime-setup.md#uninstall).

## License

Authored code is [MIT licensed](../../LICENSE). The native plugin includes official SKSE support sources with separate [source terms](licenses/SKSE-source-terms.txt). See [third-party notices](THIRD_PARTY_NOTICES).
