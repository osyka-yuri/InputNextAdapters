# InputNextFallout4

Adaptive triggers, vibration, and prioritized lightbar feedback for Fallout 4 through InputNext. This F4SE plugin provides weapon, impact, player-state, health, stealth, and optional radiation-source-count feedback.

[Effects](docs/effects.md) · [Build and testing](docs/validation.md) · [Architecture](docs/architecture.md) · [Native bindings](docs/bindings.md)

## Features

- Actual-shot recoil, firearm aim resistance, and melee attack/block resistance.
- Damage-scaled confirmed-hit accents for melee contact, critical attacks, explosions, blocks, and limb damage.
- VATS-aware feedback with free adaptive triggers during selection and playback.
- Health and AP warnings, reload accents, and power-armor cues.
- Height-scaled ordinary and power-armor landings from sampled physics state.
- USB audio haptics with classic rumble fallback.
- Timed health, actual sneaking/detection, and optional current radiation-source-count lightbar feedback.

## Requirements

- 64-bit Windows and Fallout 4 **1.11.240.0**.
- **F4SE 0.7.9** and the matching Address Library database, `version-1-11-240-0.bin`.
- The x64 InputNext runtime; see runtime setup.
- The Microsoft Visual C++ x64 Redistributable required by the plugin's build.

Other executable versions and Fallout 4 VR are unsupported. DLC is optional. DualSense and DualSense Edge require USB for feedback; HD haptics also require the controller's audio device to be available.

## Installation

1. Close Fallout 4 and install x64 InputNext beside `Fallout4.exe`.
2. Install F4SE 0.7.9 and place `version-1-11-240-0.bin` under `Data/F4SE/Plugins/`.
3. Merge the plugin package's `Data/` folder into Fallout 4's `Data/`, or install it with a mod manager. The DLL and INI files belong in `Data/F4SE/Plugins/`.
4. Launch through F4SE.

Keep the package's notices and licenses under `licenses/` with the plugin. Use one plugin or other direct-effect integration per controller slot. Sony Bluetooth currently supports input only.

## Settings

[InputNextFallout4.ini](InputNextFallout4.ini) is read at startup. Restart Fallout 4 after editing it. Missing settings use built-in defaults.

| Section / key | Default | Purpose |
| --- | --- | --- |
| `General / Slot` | `0` | InputNext slot, 0–3 |
| `General / Enabled` | `true` | Enable the adapter |
| `Effects / TriggerScale` | `0.65` | Trigger strength, 0–1 |
| `Effects / BodyScale` | `0.65` | Body strength, 0–1 |
| `Effects / BodyEnabled` | `true` | Enable body feedback |
| `Effects / AudioHapticsEnabled` | `true` | Use compiled audio profiles and optional bank overrides |
| `Effects / TriggersEnabled` | `true` | Enable adaptive triggers |
| `Effects / ResourceCues` | `true` | Health, AP, and critical-ready cues |
| `Effects / UICues` | `false` | Brief VATS entry and exit cues |
| `LED / Enabled` | `true` | Enable prioritized lightbar feedback |
| `Logging / Diagnostics` | `false` | Additional native diagnostic records |

Copy only the profile overrides you need from [InputNextFallout4.reference.ini](InputNextFallout4.reference.ini) into the working INI. The reference file describes each effect and its defaults; it is not loaded.

Body profiles support `Enabled`, `HDScale` and `ClassicScale` (0–2), `DurationScale` (0.25–2), and `FrequencyHz` (`default` or 20–1000). Trigger profiles support `Enabled`, `StrengthScale` (0–2), `ResistanceStart` (`default` or 0–1), and `VibrationFrequencyCode` (`default` or 1–255).

Ordinary walking, running and sneaking steps are disabled by default; sprint and power-armor steps are enabled. Each profile can be adjusted independently.

The `[LED]` group independently controls health, stealth and radiation feedback. Radiation remains unknown until the first real source-count event after startup or load. See lightbar settings, priorities and source limits for the complete configuration.

Invalid, unknown, out-of-range, or duplicate settings and read errors prevent startup without applying a partial configuration. Check `Documents/My Games/Fallout4/F4SE/InputNextFallout4.log` for the reason.

## Compatibility

Menus suppress gameplay effects while retaining a finite silent body lease to suppress the game's lower-priority rumble. Loading, death, lost focus, stale observations and controller changes discard old effects; feedback resumes from fresh state. `UICues` only controls VATS accents. Hot unloading is unsupported. See [effect limits](docs/effects.md#output-and-limits) for signals that do not drive feedback.

## Uninstall

Close Fallout 4 and remove the plugin's DLL and INI files from `Data/F4SE/Plugins/`, or uninstall it through your mod manager. InputNext is removed separately.

## License

Authored code is [MIT licensed](../../LICENSE). CommonLib dependencies have GPL terms and exceptions; retain [third-party notices](THIRD_PARTY_NOTICES) and the included licenses with binary distributions.
