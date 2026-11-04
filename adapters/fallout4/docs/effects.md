# Fallout 4 effects

Effects use actual shots, confirmed hits, and fresh player observations.

Thirty direct or representative source-derived transient profiles are built into the DLL. An optional
`InputNextFallout4.audiohaptics` bank beside the working INI overrides matching sections while
leaving other built-in profiles active. It loads before the native owner starts;
`AudioHapticsEnabled=false` in `[Effects]` disables both sources and retains synthetic waveforms.
Missing, corrupt or wrong-game external banks retain the compiled defaults. Classic fallback and
adaptive triggers are unchanged. See
offline preparation.

The haptic design rationale separates published research,
authored waveform choices, and digital verification. Carrier values are design parameters, not
experimentally established ideal frequencies for each action. Firearm families use their complete
carrier/component/envelope/trigger combinations; frequency alone does not guarantee recognition.

The current policy does not retain contact material. The native CommonLibF4 event exposes material
fields, but these are not yet copied or validated by this adapter. This is a current integration limit,
not a claim that the engine or library cannot provide material information.

| Source | Feedback |
| --- | --- |
| Player weapon-fired event | One recoil cue per shot, including misses; not one per pellet |
| Incoming hit | Explosion, block, limb damage, or ordinary impact |
| Outgoing player-attributed hit | Block, critical, bash, power attack, explosion, limb damage, melee, or ordinary contact |
| Equipped instance and mapped actions | Weapon-specific attack resistance, ranged aim resistance, melee block resistance, and an actual-shot trigger pulse |
| VATS mode | Free triggers; optional entry and exit cues |
| Current health and AP | Threshold warnings with hysteresis |
| Validated critical count | Brief cue when availability changes from zero to positive |
| Reload state | One mechanical accent at reload start for the same weapon |
| Power-armor state | Entry and exit accents |
| Active player animation graph | Power-armor and sprint foot-contact accents, optional ordinary foot-contact feedback |
| Sampled physics controller and position | Height-scaled ordinary and power-armor landings |

## Weapon and hit selection

Firearm families use the equipped instance's effective keywords. Instance keywords replace the base set; they are not combined. Unclassified firearms use a moderate generic profile. Engine weapon types distinguish unarmed, one-handed melee, and two-handed melee weapons; unsupported types and throwables receive no held trigger profile.

Hit variants use this precedence:

- Incoming: explosion, block, limb damage, ordinary impact.
- Outgoing: block, critical, bash, power attack, explosion, limb damage, confirmed melee, ordinary contact.

Incoming hits retain the highest mixer priority, including blocks; outgoing explosions keep ordinary outgoing priority. Limb destruction alone does not imply an explosion. Melee contact requires the hit's melee flag, independently of the equipped weapon. Missing extended data produces an ordinary impact; predicted hits are rejected.

Finite nonnegative health damage sets magnitude to `damage / (damage + 50)`; missing or invalid damage uses 0.5. The 50-point reference is independent of maximum health. Policy maps magnitude to contact intensity 0.62–1.0 before `BodyScale`, retaining feedback for a confirmed zero-damage block.

Four families override generic classification and projectile-count fallback:

| Family | Effective instance keywords |
| --- | --- |
| Rotary | `WeaponTypeMinigun`, `WeaponTypeGatlingLaser` |
| Flame | `WeaponTypeFlamer` |
| Cryo | `WeaponTypeCryolater` |
| Launcher | `WeaponTypeMissileLauncher`, `WeaponTypeFatman`, `WeaponTypeBroadsider` |

Their profiles apply per actual shot for automatic and nonautomatic weapons. Body levels precede `BodyScale` and per-profile HD scale; classic fallback lists low/high amplitudes before scaling.

| Family | Body carrier / level | Duration | Classic fallback |
| --- | --- | ---: | --- |
| Rotary | 70 Hz / 0.28 | 32 ms | 0.16 / 0.12 |
| Flame | 42 Hz / 0.22 | 40 ms | 0.12 / 0.10 |
| Cryo | 150 Hz / 0.18 | 36 ms | 0.08 / 0.16 |
| Launcher | 44 Hz / 0.72 | 140 ms | 0.58 / 0.34 |

The corresponding trigger accents replace the generic shot profile. Strengths precede `TriggerScale`; device codes are controller parameters, not frequencies in hertz.

| Family | Start zone, 0–9 | Strength before scale | Device code | Maximum pulse, ms |
| --- | ---: | ---: | ---: | ---: |
| Rotary | 2 | 0.32 | 68 | 24 |
| Flame | 1 | 0.24 | 32 | 36 |
| Cryo | 2 | 0.22 | 110 | 32 |
| Launcher | 4 | 0.44 | 30 | 48 |

## Adaptive triggers

Held resistance requires an identified, fully drawn, ready weapon and a mapped trigger. Melee and unarmed weapons use attack resistance until a sampled block switches it to the distinct mapped secondary trigger. Ranged aim resistance requires aiming. Reload, unsupported mappings, menus, stale state, and VATS withdraw the applicable profiles. Empty hands receive no held profile; drawing a melee weapon produces no body pulse.

Profiles account for DualSense's discrete start zones and force levels. At the default `TriggerScale=0.65`, attack resistance encodes as follows:

| Family | Start zone, 0–9 | Force level, 0–8 |
| --- | --- | --- |
| Pistol | 6 | 1 |
| Rifle | 5 | 2 |
| Shotgun | 7 | 2 |
| Heavy firearm | 4 | 2 |
| Energy firearm | 3 | 1 |
| Unclassified firearm | 6 | 1 |
| Rotary firearm | 3 | 1 |
| Flame firearm | 2 | 1 |
| Cryo firearm | 3 | 1 |
| Launcher | 5 | 2 |
| Unarmed weapon | 6 | 1 |
| One-handed melee | 4 | 1 |
| Two-handed melee | 3 | 2 |

These levels also hold for automatic firearms with reduced continuous resistance. Ranged aiming uses zone 8, level 1; melee blocking uses zone 3, level 2. Lower user scales can round to zero; scale zero disables effects.

A confirmed shot adds an accent on the mapped attack trigger. Semi-automatic shots and automatic shots from unknown, unarmed, or melee families use the common pulse. Ordinary automatic firearms use the profiles below; the four special families use their profiles above. Pulses expire from the shot's original timestamp. Idle ticks and dry fire do not extend them. Body recoil remains independent.

Strengths below precede `TriggerScale` (default 0.65), which gives encoded force level 2 for these automatic profiles.

| Firearm family | Start zone, 0–9 | Strength before scale | Device value | Maximum pulse, ms |
| --- | ---: | ---: | ---: | ---: |
| Pistol | 3 | 0.36 | 86 | 36 |
| Rifle | 2 | 0.38 | 62 | 40 |
| Shotgun | 3 | 0.42 | 42 | 48 |
| Heavy firearm | 1 | 0.44 | 38 | 48 |
| Energy firearm | 2 | 0.34 | 104 | 32 |
| Generic firearm | 3 | 0.36 | 70 | 40 |

## VATS and resources

VATS selection, playback, and uncertain transitions release both adaptive triggers. Selection admits actual incoming hits. Playback admits actual player shots and hits. Queued attacks and projected damage do not produce recoil.

Health and AP use normalized current Actor Values, fresh baselines, and hysteresis. The health ratio does not reproduce every HUD overlay or active-effect adjustment. Unavailable inputs produce no warning.

## Lightbar feedback

Fallout 4 supplies absolute health, combat state, actual sneaking/detection and optional current radiation-source counts to the shared lightbar policy. Unknown damage can gain recent-damage priority only when correlated with a sampled HP decrease; known positive damage also covers simultaneous healing with no net HP change.

Radiation remains unknown until a real count event after startup or loading. Missing optional bindings disable only their source. See lightbar feedback for timing, priorities and settings, and [native bindings](bindings.md#led-health-stealth-and-radiation) for source validation and lifecycle details.

## Reload and power armor

Reload feedback occurs once on a fresh transition for the same equipped weapon; leaving reload state produces no completion cue.

`FootLeft` and `FootRight` from the player's active graph select `Body.PowerArmorStep` in power armor, otherwise `Body.SprintStep` while sprinting or `Body.OrdinaryStep` for other contacts. Power-armor and sprint steps default on; ordinary steps default off and include sneaking because the event route has no separate sneak signal.

Landings use the physics controller's grounded, jumping and airborne states, with a continuous player/cell/position history. Descent is measured from the highest sampled airborne position, so a jump returning to its starting ground height still produces a cue. At 64 units of descent, intensity is 0.55 and duration is 80 ms; both increase linearly to 1.0 and 150 ms at 512 units, before INI scaling. Smaller motions remain silent. Gaps over 150 ms, displacement over 512 units per sample, swimming, unsupported physics states and identity changes cancel the interval. The animation tag `PowerArmorHardLanding` is ignored to avoid duplicate output.

Animation replacements may omit foot contacts; no timer invents them. Mechanic cues remain silent in VATS and after a load, focus change, or stale-state baseline. Landing height scales the selected profile; it does not identify the exact sound variant or surface material playing in the engine.

Ordinary, sprint, and power-armor foot contacts last 64, 56, and 56 ms, with distinct carrier and amplitude profiles. Their quiet envelopes retain a digital signal under short delivery delays, aged from the original animation event. Repeated firearm shots keep their separate, shorter weapon-specific timing.

Profile controls, ranges, and defaults are in [INI settings](../README.md#settings) and the [reference INI](../InputNextFallout4.reference.ini).

## Output and limits

Body effects use bounded mixing, smooth onset/release, and explicit classic rumble fallback. Contact, block, power-armor step, and landing envelopes retain a tail under short delivery delays; expired impacts are not restarted. USB HD requires runtime readiness.

Charging progress, rotor speed, sustained beam or thrust intensity, ambient radiation intensity, normalized battery readings, unconfirmed melee swings, and throw release are unsupported. Static weapon data such as time-to-full-charge does not establish current charge progress.

See [native bindings](bindings.md) for source contracts.
