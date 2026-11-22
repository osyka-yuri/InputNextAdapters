# Audio haptics source coverage

The counts below cover body effects. Skyrim SE and LE use separate banks to match each edition's
available sounds and native events. Fallout 4, Fallout New Vegas, and Oblivion use their own effect
catalogues.

## Compiled banks

| Edition | Supported body roles | Bank profiles | Synthetic fallbacks | Bank SHA-256 |
| --- | ---: | ---: | ---: | --- |
| Skyrim SE | 52 | 49 | 3 | `dca79b4b6f3c139bac7d6ae582decd359bdf4570ab2af56f242d30673ccd868f` |
| Skyrim LE | 20 | 18 | 2 | `ec12dc53d26f55a36970e0e42a4cea838a7cfe6bfb99abb42ff66aba092fd374` |
| Fallout 4 | 31 | 30 | 1 | `dcb12acecc775306c8039c1312ec1e278c3a729da63051b9b815d6c543055379` |
| Oblivion | 60 | 38 | 22 | `918f4c991405a4084162c19034d77e5f99de00fe7f31cf6dc5ccff1b597f96ea` |
| Fallout New Vegas | 35 | 25 | 10 | `a323b8dc9732b7cdc3b068a546dd6e8b3372fe6a18bf9e7459d9a251730884af` |

The five embedded banks contain 160 profiles. Each manifest records source patterns, carriers,
durations and styles. [Banks](../../resources/audio-haptics/) · [Skyrim SE](manifests/skyrim-se.json) · [Skyrim LE](manifests/skyrim-le.json) · [Fallout 4](manifests/fallout4.json) · [Fallout New Vegas](manifests/fallout-new-vegas.json) · [Oblivion](manifests/oblivion.json)

## What the mappings establish

Source recordings supply onset timing and temporal envelopes. The game manifests and adapters
set carriers, mix, levels and durations. Each mapping represents a sound family, so runtime
feedback uses the same authored profile across material and recording variants in that family.

The baker aligns up to four sorted variants at onset and crops or pads them to the authored duration. Transient effects follow the resulting envelope. Supported Skyrim held effects use a stationary blend derived from the two envelope means, preserving continuous phase as the effect horizon renews. Bow hold uses representative pull loops. Mounted run uses representative hoof contacts because the inspected inventory has walk and sprint families without a separate run family.

Skyrim LE omits profiles requiring magic taxonomy/caster phases, full-draw/held bow phases, actual crossbow shots, shield blocking, footsteps, mounts, stagger/knockdown/killmove or airborne state. Its native frontend does not supply those signals. Short bow draw/release cues remain supported; BowRelease alone does not confirm an actual shot. Entering or leaving water produces no effect.

Fallout 4's `Body.PowerArmorEnter` uses dedicated normal and combat entry recordings. The manifest
allows a 2000 ms onset search for late-starting cues and keeps the 72 ms duration and 54/88 Hz carriers.

## Required source archives

The baker opens `Skyrim - Sounds.bsa` for Skyrim, `Fallout4 - Sounds.ba2` for Fallout 4,
`Fallout - Sound.bsa` for Fallout New Vegas, and `Oblivion - Sounds.bsa` for Oblivion. Pass the
following Skyrim archives with `--archive` for the full manifests.

- Skyrim SE uses `ccQDRSSE001-SurvivalMode.bsa` for Survival sources.
- Skyrim LE uses `Dawnguard.bsa` for Vampire Lord sources.

The LE Dragonborn BSA is not required by these mappings. An inspected copy contained an invalid UTF-8 resource name; the reader rejects such names rather than guessing an encoding. Archive overrides are applied in command-line order, with loose audio winning last.

## Oblivion

`Oblivion - Sounds.bsa` supplies all 38 recorded profiles. These cover melee, blocks, bows,
magic, dodge, landing, mounts, potion use, crafting, locks, skill increases, vampire feeding,
Soul Trap and a gate closing. Walk, run and sneak profiles use stock steps from several
surfaces. Mounted walk and run use individual hoof contacts. [Oblivion manifest](manifests/oblivion.json)

Bow draw and casting average the envelope bands for sustained feedback. The adapter sets
step and hoofbeat cadence from grounded travel. The remaining 22 roles use synthesis,
including the attached-arrow phase, recoil, heartbeat, water transitions, training and pickpocketing.

## Fallout New Vegas

The Fallout New Vegas v104 archive supplies 25 of the 35 mapped profiles. Recorded sources cover
weapon-family shots, reload cues, melee swings and impacts, grenade actions, V.A.T.S., stimpak use,
weapon equip and repair, lockpicking, and terminal hacking outcomes. The broken-pin envelope plays
on xNVSE's lockpick-break event. Terminal success and lockout use the `passgood` and `passbad`
envelopes on confirmed terminal outcomes. An incorrect password guess alone does not produce a
lockout cue. The dry-fire envelope plays on the player's confirmed empty-weapon attack.
`Body.BlockImpact` uses three melee-block recordings. `Body.ExplosionDealt` has a separate, shorter
and lighter profile from the verified high grenade-impact recordings. Critical hits, sneak
criticals, crippled hits, stagger, and knockdown use synthesis because the archive has no dedicated
player-action recordings. The generic shot, incoming and outgoing hits, crafting, and landing
profiles also use synthesis. The bake report
records the selected variants, onset measurements, file hashes, bank hash, and source archive
version and SHA-256. [Fallout New Vegas bake report](../../resources/audio-haptics/fallout-new-vegas.audiohaptics.json)

## Skyrim, Fallout 4, and Fallout New Vegas synthetic fallbacks

| Edition | Role | Mapping limitation |
| --- | --- | --- |
| Skyrim SE and LE | `Body.Melee.Mace.Swing` | No admitted mace-specific or generic blunt swing source. |
| Skyrim SE and LE | `Body.World.StaminaDepleted` | No admitted stamina-depleted sound; unrelated UI cues are not substituted. |
| Skyrim SE | `Body.World.Stagger` | No admitted player-specific stagger cue. |
| Fallout 4 | `Body.CrippledHit` | No admitted player cripple-status cue; generic impacts and NPC pain/death sounds are not substituted. |
| Fallout New Vegas | `Body.ShotGeneric` | The selected shot recordings correspond to specific weapon families. |
| Fallout New Vegas | `Body.HitDealt`, `Body.HitTaken` | The inspected archive contains shared impact and pain cues. |
| Fallout New Vegas | `Body.CriticalHit`, `Body.SneakCritical` | No dedicated player critical-hit recordings; shared impacts do not identify critical or stealth attacks. |
| Fallout New Vegas | `Body.CrippledHit` | The archive has no player-specific cripple-status sound. |
| Fallout New Vegas | `Body.Stagger`, `Body.Knockdown` | The archive has no player-specific stagger or knockdown recordings. |
| Fallout New Vegas | `Body.Craft`, `Body.Landing` | The archive search found no matching crafting or player landing sounds. |

LE has no native Survival integration. Survival profiles are excluded from its shipped reference and bank; their shared configuration keys remain accepted for compatibility with older INI files.

## Verification and rebaking

Build the foundation `msvc-x64` preset and the adapter test presets. Then run the audio profile
checks from the repository root. [Skyrim SE](../../adapters/skyrim_se/docs/validation.md) · [Skyrim LE](../../adapters/skyrim_le/docs/validation.md) · [Fallout 4](../../adapters/fallout4/docs/validation.md) · [Oblivion](../../adapters/oblivion/README.md#build-and-package)

```powershell
cargo test --locked -p inputnext-haptics
ctest --test-dir build/msvc-x64 -R 'audio_haptics' --output-on-failure --no-tests=error
ctest --test-dir build/skyrim-se-msvc-x64 -R 'audio_haptics|calibration' --output-on-failure --no-tests=error
ctest --test-dir build/skyrim-le-msvc-x86 -R 'audio_haptics|calibration' --output-on-failure --no-tests=error
ctest --test-dir build/fallout4-msvc-x64 -R 'audio_haptics' --output-on-failure --no-tests=error
ctest --test-dir build/oblivion-msvc-x86-native-tests -R 'oblivion_audio_profiles|oblivion\.effect_policy\.(body|bow)\.' --output-on-failure --no-tests=error
```

These checks validate software output; physical distinction and perceived strength still require controller tests. For source-level reproduction, follow the [baking guide](../../docs/audio-haptics-baking.md) with the installed game archives and a separately supplied FFmpeg executable. Every bake emits a JSON report with selected paths, origins, source sizes and hashes, onset measurements, failures and the output bank hash. Keep that report with the generated bank. Different game assets, sound mods or decoder versions may change the result; raw game audio and local development reports are not distributed.
