# InputNextOblivion

InputNextOblivion adds HD haptics with classic rumble fallback, adaptive trigger resistance and lightbar feedback to Oblivion Classic.

## Requirements

- Oblivion Classic **1.2.0.416** on Windows and the Microsoft Visual C++ Redistributable (x86).
- **xOBSE 22.15**. [Download](https://github.com/llde/xOBSE/releases/tag/22.15)
- An x86 InputNext build with DirectInput 8 and a supported controller. Connect DualSense or DualSense Edge over USB for feedback.

## Installation

1. Close the game and install xOBSE 22.15 for your edition. [xOBSE installation guide](https://github.com/llde/xOBSE/blob/22.15/README.md#installation)
2. Back up any existing `dinput8.dll`. Copy `dinput8.dll`, `inputnext_runtime.dll`, `GameInputRedist.dll` and `GameInputRawInputProxy.exe` from the same x86 InputNext build beside `Oblivion.exe`. The `inputnext_dinput_bundle` target supplies these files when building InputNext locally.
3. Merge the release archive's `Data/` folder with the game directory or install it with a mod manager. `InputNextOblivion.dll` and both INIs belong in `Data/OBSE/Plugins/`.
4. Connect the controller, start Oblivion and load a save. Start the Steam edition from Steam with `obse_steam_loader.dll` installed. For a non-Steam installation that uses the loader, run `obse_loader.exe`.

If feedback is missing, check `Data/OBSE/Plugins/InputNextOblivion.log` for INI errors, an incompatible InputNext runtime or unavailable xOBSE events. HD haptics require the controller's audio device to be available. Compatible controllers can use classic rumble when HD haptics are unavailable.

### Digital triggers

To bind LT and RT as buttons, add or update `[directinput]` in `inputnext.toml` beside `Oblivion.exe`.

```toml
[directinput]
digital_triggers = true
trigger_threshold = 0.35
```

Restart Oblivion and assign LT to Block and RT to Attack in the game's controls. This example reports a press at 35% of remapped trigger travel. Adjust resistance separately with `TriggerScale` in `InputNextOblivion.ini`.

### Pause on Options

Set `PauseOnOptions=true` in `[InputNextOblivion]` to use the selected controller's Options or Menu button to open pause and back out of its settings. The shortcut requires an active game session and game focus. Release the button before pressing it again.

## Effects

| Situation | Feedback |
| --- | --- |
| Melee | Separate swings and hits for fists, one- and two-handed blades, and one- and two-handed blunt weapons. Power attacks use a stronger swing. Incoming damage, recoil, stagger, dodge and knockout have separate cues. |
| Block | LT resistance while blocking and a short pulse on impact. |
| Bow | Draw and arrow-attached vibration with RT resistance, plus distinct shot and impact pulses. Releasing a new player-owned arrow triggers the shot. Canceling the draw releases the resistance. |
| Magic | Vibration during casting and a pulse on a confirmed cast. Profiles distinguish fire, frost, shock, healing and shield effects. Mixed or unknown forms use the neutral profile. |
| Player state | In combat, the heartbeat starts below 20% HP and stops at 25% HP or when combat ends. Magicka and fatigue have separate cues when they fall to 5% or below. The lightbar shows health changes and stealth. |
| Movement | Landing, diving, surfacing, mounting and dismounting. Optional footsteps and hoofbeats follow sustained travel on the ground. |
| Items and menus | Potion and ingredient use, equipping and unequipping, dropping items, potion crafting, enchanting, spellmaking, opening locks, breaking picks and successful pickpocketing. |
| Other actions | Vampire feeding and soul capture. Skill increases, quest completion, map discovery, training and gate closures are opt-in. |

The DLL embeds 38 profiles derived from `Oblivion - Sounds.bsa`. The other 22 use synthesized patterns. An optional `InputNextOblivion.audiohaptics` beside the INI overrides matching profiles. Set `AudioHapticsEnabled=false` to use synthesis throughout.

## Settings

Edit `Data/OBSE/Plugins/InputNextOblivion.ini` and restart Oblivion. To tune a cue, copy its `[Body.*]` section from `InputNextOblivion.reference.ini` and edit the values. Omitted keys use defaults. Each section and key can appear once. Invalid settings prevent the adapter from loading.

| Setting | Default | Meaning |
| --- | --- | --- |
| `ControllerSlot` | `0` | InputNext slot, 0–3. |
| `RumbleScale` | `1.0` | Overall body strength, 0–1. |
| `TriggerScale` | `1.0` | Overall adaptive-trigger strength, 0–1. |
| `BodyEnabled` | `true` | Enables vibration. Set it to `false` or use `RumbleScale=0` to leave the vibration channel available to other sources. |
| `TriggersEnabled` | `true` | Enables adaptive trigger effects. |
| `MenuEffectsEnabled` | `true` | Allows confirmed results in supported menus. |
| `AudioHapticsEnabled` | `true` | Enables embedded and optional audio-haptic profiles. |
| `PauseOnOptions` | `false` | Sends Escape from Options / Menu in gameplay, pause and settings. |
| `[LED] Enabled` | `true` | Enables the lightbar. |
| `[LED] HealthEnabled` | `true` | Shows recent health changes in red, yellow or green. |
| `[LED] StealthEnabled` | `true` | Shows blue for the dim stealth eye and magenta for the bright eye. Critical health, recent damage and combat health take priority. |
| `[LED] HealthHoldMs` | `4000` | Health indication after an HP change, 1–60,000 ms. |
| `[LED] HealingTransitionMs` | `200` | Recovery color transition, 0–1,000 ms. |
| `[LED] CriticalEnter` / `CriticalExit` | `0.20` / `0.25` | Health ratios for critical lightbar priority. Use `0 <= CriticalEnter < CriticalExit <= 1`. |

Per effect, `HDScale` and `ClassicScale` accept 0–2, `DurationScale` accepts 0.25–2, and `FrequencyHz` accepts 20–1,000 Hz or `default`.

## Detection and lifecycle

- After an arrow attaches, the adapter tracks new player-owned arrows in the current cell. It scans up to 256 references per pass and checks for a shot for up to 150 ms after release.
- A block-impact animation gives one pulse. A single matching hit shares that cue. Multiple possible hits keep their damage cues alongside the block pulse.
- In menus, short cues play only for supported results. The adapter checks player counters for locks, broken picks, training and pickpocketing in the matching menu and on the first gameplay sample after it closes. Gate closures can play after a cell load in the same save and on the same controller connection.
- The output worker discards samples older than 150 ms. Focus loss, death, loading, event queue overflow and controller changes clear active feedback. Reconnecting requires a fresh sample. The worker can process 64 queued events and a gate closure in one pass.
- Mods that change animations, HUD state or xOBSE events can change when cues play. If event registration fails, the log names the unavailable event. [xOBSE 22.15 event reference](https://github.com/llde/xOBSE/blob/22.15/obse_command_doc.html#L5934)

## In-game check

1. Compare a missed swing with a hit. Block an incoming blow, then take one unblocked. Check for distinct block and damage feedback.
2. Draw and fire an arrow, then cancel a second draw. A shot gives one release pulse. Canceling should release the resistance without the pulse.
3. Compare single-element and mixed spells. In combat, drop below 20% HP and then heal to 25% or above to check the heartbeat and health light.
4. Open a lock, break a pick and pickpocket an item. Check for a short pulse after each result. With `Body.Action.Training` enabled, take a lesson. With `Body.Action.GateClosed` enabled, take a Sigil Stone and check for one pulse after returning.
5. With footstep and hoofbeat cues enabled, walk, run, sneak and ride. Try walking into a wall or jumping. New steps should stop. While crouched, check the lightbar against the stealth eye, then stand or open a menu.
6. Switch focus, load a different save and reconnect the controller. Check that fresh actions produce feedback without replaying earlier cues. If `PauseOnOptions` is enabled, check the shortcut in gameplay and pause settings.

## Build and package

Use CMake 4.4.4+, Ninja and an x86 Visual Studio Developer PowerShell. Run from `adapters/oblivion/`.

```powershell
cmake --preset msvc-x86-native-tests
cmake --build --preset msvc-x86-native-tests
ctest --preset msvc-x86-native-tests
```

Use `msvc-x86-native-quality` in all three commands to run the same suite with clang-tidy. Put LLVM 23.1.5 on `PATH` for that preset.

Build the installable DLL with the production preset and stage it in a fresh package directory.

```powershell
cmake --preset msvc-x86-native
cmake --build --preset msvc-x86-native
cmake --install ../../build/oblivion-msvc-x86-native --component OblivionAdapter --prefix ../../build/oblivion-package
Push-Location ../../build/oblivion-package
try {
    cmake -E tar cf ../InputNextOblivion-0.1.0-windows-x86.zip --format=zip Data README.md LICENSE
} finally {
    Pop-Location
}
```

CMake verifies the pinned xOBSE source archive. To use a local copy, pass `-DINPUTNEXT_OBLIVION_XOBSE_ARCHIVE=<path>` when configuring. The ZIP contains the DLL, both INIs, this guide and the license. Install xOBSE and InputNext separately.

## Uninstall

Close Oblivion and remove `InputNextOblivion.dll` and its INIs from `Data/OBSE/Plugins/`. You can also remove its log and any external bank. To uninstall InputNext, remove its bundle files and restore the backed-up DLLs.
