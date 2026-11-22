# InputNextFalloutNewVegas

Controller feedback for Fallout New Vegas `1.4.0.525` on Windows x86. The plugin uses xNVSE 6.4.9, ShowOff 1.84, and the InputNext x86 XInput runtime. A DualSense or DualSense Edge connected over USB can use HD haptics, adaptive triggers, and the light bar. The plugin uses classic rumble when HD output is unavailable.

## Installation

1. Install [xNVSE 6.4.9](https://github.com/xNVSE/NVSE/releases/tag/6.4.9) and [ShowOff 1.84](https://github.com/Demorome/ShowOff-NVSE/releases/tag/1.84) in the game directory.
2. Copy the x86 XInput bundle's `xinput1_3.dll`, `xinput1_4.dll`, `xinput9_1_0.dll`, `inputnext_runtime.dll`, `GameInputRedist.dll`, and `GameInputRawInputProxy.exe` beside `FalloutNV.exe`. Start the game through xNVSE and check the controller's buttons and sticks.
3. Close the game. Extract the adapter archive to the game directory. `InputNextFalloutNewVegas.dll` and its INI belong in `Data/NVSE/Plugins`. Select one effect plugin for the same controller slot. Move the legacy `xnext_falloutnv_adapter.dll` out of that folder if it is installed.
4. Restart the game. `InputNextFalloutNewVegas.log` reports configuration errors, runtime ABI checks, and event registration. Enable `Diagnostics` under `[Logging]` for delivery and game-signal details.

For the Steam PCR edition (`Fallout New Vegas enplczru`), launch `nvse_loader.exe -appid 22490` from the game directory.

Edit `InputNextFalloutNewVegas.ini` with the game closed. Set `ControllerSlot` to the slot used by your controller. `BodyScale` and `TriggerScale` adjust the two output types independently. Match `[Controls] AttackTrigger` and `AimTrigger` to your bindings if you remap them. Use `none` for actions assigned to a button or keyboard. The reference INI has an editable section for every body and trigger profile.

Set `GaitCues=true` to enable quiet pulses for walking, running, and moving while crouched outside combat. A pulse requires three consecutive ground movements in the same cell and gait. The filter allows up to 924 game units per second, three times the default run speed. Successive samples must agree within a factor of three in speed and 60 degrees in direction. The 620 ms cooldown continues through brief movement dropouts, combat, and airborne periods. Opening a menu, loading a save, or changing the player clears it. Turn off `[Body.OrdinaryStep]` and `[Body.RunStep]` to keep just the crouch movement pulses. `[Body.Progress] Enabled=true` enables progression cues.

With `[LED] StealthEnabled=true`, the lightbar reads player detection from the game while crouched, including at rest. Hidden is blue, Caution and Detected use shades of violet, and Danger is magenta. Critical health, recent damage, radiation, and combat health retain their priority. With diagnostics enabled, `player detection` records state changes as `0` through `3` and uses `-1` when detection clears.

With separate trigger bindings, the attack trigger has resistance while the weapon is ready and the aim trigger adds resistance while aiming. If both actions share a trigger, aim sets the sustained resistance while aiming. The short recoil pulse still plays on the attack trigger for a confirmed shot. A confirmed empty magazine releases attack resistance after the last shot's recoil.

Shot feedback requires both ShowOff firearm events. The log identifies missing sources and reports when shot feedback becomes available after a save is loaded. Observed effects such as landing and health still work with partial event registration. Contact cues distinguish separate reported projectile or blast impacts. xNVSE can collapse multiple `OnHit` reports for the same attacker and target within a game tick.

Hit data distinguishes outgoing critical and sneak-critical contacts from ordinary hits. Incoming hits can report a crippled limb or a block against an identified melee or unarmed weapon. A confirmed damaging hit with unknown weapon data uses the ordinary damage cue. The player's process state provides separate stagger and knockdown cues. The adapter groups outgoing explosion damage by the explosion object and confirms the player as its source.

Dry-fire feedback follows the player's confirmed Attack press on the empty-weapon path in FalloutNV `1.4.0.525`. It still plays when the game skips replaying a click that is already sounding. The adapter installs the hook on the first game loop if the player check and call match the original executable. A mismatch disables dry-fire feedback and is recorded in the log. The hook stays installed until the game process exits.

The built-in audio profiles are envelopes baked from `Fallout - Sound.bsa`. Place an `InputNextFalloutNewVegas.audiohaptics` bank next to the DLL to override matching profiles. If the file cannot be read, fails validation, or targets a different game, the adapter keeps the built-in profiles and records the reason. `AudioHapticsEnabled=false` selects synthesized feedback.

## In-game checks

| Scenario | Expected behavior |
| --- | --- |
| Fire a pistol, an automatic rifle, and a shotgun | Recoil follows confirmed player shots. One shotgun discharge gives one impulse. NPC shots alone leave the controller idle. |
| Aim a loaded pistol, fire its last round, then reload | With the default bindings LT and RT both have resistance while aiming with ammo. The last round gives its usual recoil. An empty magazine leaves only LT resistance while aiming. The reload completion cue requires a confirmed increase in loaded rounds. |
| Tap Attack with an empty weapon and no spare ammo | Each press recognized by the game's dry-fire path produces a short grip cue. |
| Jump with a weapon drawn and land | The landing cue plays through the grips. An active trigger resistance stays steady through the landing recovery animation. |
| Enable `GaitCues`, then walk, run, and move while crouched outside combat | Each gait has its own quiet grip cue. The cues stop while standing still or airborne. |
| Land a melee hit and take damage | A swing, contact, and incoming damage have distinct cues. A confirmed hit and its HP loss produce one damage impulse. |
| Land a critical hit and a sneak critical, then block a melee attack | The two critical types use separate contact profiles. A confirmed block plays a short grip cue, including when the hit causes zero HP damage. |
| Take a hit that cripples a limb, then stagger or fall | The limb injury and observed loss of balance have distinct profiles. Holding the same reaction state produces a single transition cue. |
| Take damage from an explosion | A confirmed blast uses the explosion profile and produces one damage impulse. |
| Damage several opponents with the same grenade | One outgoing confirmation plays across all affected opponents. |
| Select an attack in V.A.T.S. and execute it | Entering selection and starting execution play brief cues. Confirmed shots and hits play during execution. |
| Open the Pip-Boy or pick a lock | Opening a menu clears gameplay feedback. Confirmed aid use, equipment changes, crafting, repair, opening a lock, and breaking a bobby pin can play short impulses. |
| Hack a terminal successfully, then exhaust the attempts on a different terminal | The correct password and terminal lockout each produce a distinct short grip cue. |
| Crouch and change detection state | The lightbar follows the HUD's detection state. Standing up or opening a menu clears the stealth indication. |
| Lose focus, load a save, or reconnect the controller | Previous effects clear. The plugin resumes from a fresh observation on the current controller. |

With `[Logging] Diagnostics=true`, each terminal callback logs current menu visibility, whether a recent snapshot saw the terminal menu, and whether xNVSE passed arguments. A confirmed callback can produce its cue during the transition back to gameplay after the menu closes. Check for one corresponding `native event delivered` entry for each outcome.

Combat diagnostics record hit flags, location, limb damage, and reaction transitions. Compare the frame and monotonic millisecond values when checking two nearby explosions. Multiple targets hit by the same explosion share its contact identifier. The reaction log includes the raw knockdown byte and animation action.

## Build and package

Use an x86 Visual Studio Developer PowerShell. Run these commands from this adapter directory.

```powershell
cmake --preset msvc-x86-native
cmake --build --preset msvc-x86-native
cpack --config ../../build/fallout-new-vegas-msvc-x86-native/CPackConfig.cmake -B ../../build/fallout-new-vegas-release
```

CPack checks that the plugin is an x86 DLL with the two xNVSE exports and no dynamic CRT dependencies. It checks the ZIP contents and confirms that the packaged DLL matches the build.

Run the test and quality preset from the repository root.

```powershell
./tools/verify.ps1 -Project fallout-new-vegas -Preset msvc-x86-quality
```
