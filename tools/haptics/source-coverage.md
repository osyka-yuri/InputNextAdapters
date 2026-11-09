# Audio haptics source coverage

The catalogue contains 83 base roles: 52 shared Skyrim roles and 31 Fallout 4 roles. There are admitted sound mappings for 79 distinct roles. Skyrim SE and LE use separate banks because their available audio and native signals differ. LE supports 20 of the shared body roles; its bank contains only profiles that its native frontend can trigger.

## Compiled banks

| Edition | Supported body roles | Bank profiles | Synthetic fallbacks | Bank SHA-256 |
| --- | ---: | ---: | ---: | --- |
| Skyrim SE | 52 | 49 | 3 | `dca79b4b6f3c139bac7d6ae582decd359bdf4570ab2af56f242d30673ccd868f` |
| Skyrim LE | 20 | 18 | 2 | `ec12dc53d26f55a36970e0e42a4cea838a7cfe6bfb99abb42ff66aba092fd374` |
| Fallout 4 | 31 | 30 | 1 | `dcb12acecc775306c8039c1312ec1e278c3a729da63051b9b815d6c543055379` |

The three [bank resources](../../resources/audio-haptics/) contain 97 profiles. CMake embeds these bytes during the C++ build; building the DLLs does not require game archives or FFmpeg. Source patterns, carrier values, durations and styles are recorded in the [SE](manifests/skyrim-se.json), [LE](manifests/skyrim-le.json) and [Fallout 4](manifests/fallout4.json) manifests.

## What the mappings establish

Source audio supplies onset and temporal envelopes. Carrier frequencies, mix, levels and durations are authored controller parameters; they are not measured audio pitch or hardware calibration. A source-family mapping does not identify the exact sound variant or target material selected by the running game.

The baker aligns up to four sorted variants at onset and crops or pads them to the authored duration. Transient effects follow the resulting envelope. Supported Skyrim held effects use a stationary blend derived from the two envelope means, preserving continuous phase as the effect horizon renews. Bow hold uses representative pull loops. Mounted run uses representative hoof contacts because the inspected inventory has walk and sprint families without a separate run family.

Skyrim LE omits profiles requiring magic taxonomy/caster phases, full-draw/held bow phases, actual crossbow shots, shield blocking, footsteps, mounts, stagger/knockdown/killmove or airborne state. Its native frontend does not supply those signals. Short bow draw/release cues remain supported; BowRelease alone does not confirm an actual shot. Entering or leaving water produces no effect.

## Required source archives

The default sound archive is `Skyrim - Sounds.bsa` for Skyrim and `Fallout4 - Sounds.ba2` for Fallout 4. Full-template baking also requires:

- Skyrim SE: `ccQDRSSE001-SurvivalMode.bsa`, supplied with `--archive`.
- Skyrim LE: `Dawnguard.bsa`, supplied with `--archive` for Vampire Lord sources.

The LE Dragonborn BSA is not required by these mappings. An inspected copy contained an invalid UTF-8 resource name; the reader rejects such names rather than guessing an encoding. Archive overrides are applied in command-line order, with loose audio winning last.

Fallout 4's `Body.PowerArmorEnter` uses its dedicated normal and combat entry recordings. The manifest allows a 2000 ms onset search for their late-starting signal, then retains the authored 72 ms cue duration and 54/88 Hz carriers.

## Synthetic fallbacks

| Edition | Role | Mapping limitation |
| --- | --- | --- |
| Skyrim SE and LE | `Body.Melee.Mace.Swing` | No admitted mace-specific or generic blunt swing source. |
| Skyrim SE and LE | `Body.World.StaminaDepleted` | No admitted stamina-depleted sound; unrelated UI cues are not substituted. |
| Skyrim SE | `Body.World.Stagger` | No admitted player-specific stagger cue. |
| Fallout 4 | `Body.CrippledHit` | No admitted player cripple-status cue; generic impacts and NPC pain/death sounds are not substituted. |

LE has no native Survival integration. Survival profiles are excluded from its shipped reference and bank; their shared configuration keys remain accepted for compatibility with older INI files.

## Verification and rebaking

The maintained regressions cover bank parsing, embedded profiles, external overrides, carrier limits, envelope timing, held-phase continuity and absolute quiet-tail RMS. Build the foundation `msvc-x64` preset and the isolated adapter test presets described in the [SE](../../adapters/skyrim_se/docs/validation.md), [LE](../../adapters/skyrim_le/docs/validation.md) and [Fallout 4](../../adapters/fallout4/docs/validation.md) guides, then run from the repository root:

```powershell
cargo test --locked -p inputnext-haptics
ctest --test-dir build/msvc-x64 -R 'audio_haptics' --output-on-failure --no-tests=error
ctest --test-dir build/skyrim-se-msvc-x64 -R 'audio_haptics|calibration' --output-on-failure --no-tests=error
ctest --test-dir build/skyrim-le-msvc-x86 -R 'audio_haptics|calibration' --output-on-failure --no-tests=error
ctest --test-dir build/fallout4-msvc-x64 -R 'audio_haptics' --output-on-failure --no-tests=error
```

These checks validate software output; physical distinction and perceived strength still require controller tests. For source-level reproduction, follow the [baking guide](../../docs/audio-haptics-baking.md) with the installed game archives and a separately supplied FFmpeg executable. Every bake emits a JSON report with selected paths, origins, source sizes and hashes, onset measurements, failures and the output bank hash. Keep that report with the generated bank. Different game assets, sound mods or decoder versions may change the result; raw game audio and local development reports are not distributed.
