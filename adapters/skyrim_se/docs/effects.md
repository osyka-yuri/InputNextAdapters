# Skyrim SE/AE effects

Feedback follows native actions and phases.

The DLL includes 49 audio-derived stock profiles. An optional `InputNextSkyrim.audiohaptics` bank beside the working INI overrides their
temporal envelopes for transients and stationary source-mean gains for supported held profiles. It is loaded once before gameplay callbacks start.
`AudioHapticsEnabled=false` in `[InputNextSkyrim]` disables both compiled and external audio profiles and uses synthetic waveforms. Missing, corrupt,
wrong-game or inapplicable entries retain those profiles; triggers and classic fallback keep their
existing behavior. See [offline preparation](../../../docs/audio-haptics-baking.md).

| Action | Feedback |
| --- | --- |
| Melee | Swing and confirmed-contact cues, with distinct light/heavy weapon envelopes |
| Bow | Draw, attached, and drawn textures; onset, readiness, and actual release pulses |
| Crossbow | Verified-source shot cue; no inferred reload timing |
| Blocking | Shield/weapon left-trigger resistance and confirmed blocked-hit accents |
| Magic | Charging and concentration from caster state; fire-and-forget release from an actual fire event |
| Damage | High-priority confirmed incoming impact |
| Shout | Actual voice-fire impulse, with distinct one-, two-, and three-word profiles when the native level is available |
| Low health | Quiet combat heartbeat below 20%, stopping after recovery to 25% |
| Health lightbar | Current/max HP color for four seconds after any HP change, including regeneration; no total-duration cap |
| Stealth lightbar | Blue-to-magenta from verified raw detection data while actually sneaking |

Known fire, frost, shock, healing, and ward sources select distinct profiles. Unknown spell sources stay neutral. Unknown melee sources do not inherit an equipped weapon's classification.

## Timing and hands

Bow phases are independent of trigger depth. The adapter uses observed attack phases without an inferred time-to-full-draw. Draw, attached, and fully drawn textures use 118, 150, and 160 Hz body carriers, with a quieter fully drawn hold.

Caster state drives held feedback; exact action sources drive release. Cancellation, sheathing, equipment changes, unsupported phases, and gameplay suppression remove held cues without fabricating a release. Left and right hands have independent trigger and body positions.

## Landings

Landings measure descent from the highest continuously sampled airborne position, including jumps
that return to the same ground height. A minimum descent of 64 game units rejects small motion;
strength and duration increase with descent up to 512 units.

Swimming, mounting and missing position data cancel tracking. Player or location discontinuities
and sample gaps over 150 ms restart the baseline. Detection uses copied movement state; entering
and leaving water produce no feedback.

## Shouts

The SE/AE adapter copies the player's current shout variation when it receives the voice-fire event. The active shout must match the event source. Missing state, an invalid variation, and voice powers retain the original neutral voice impulse; button-hold duration is not used to infer a level.

Each release produces one body-wide impulse. Additional words increase its duration and weight while lowering its carrier, rather than generating a separate pulse for each spoken word. Shouts leave the hand triggers available for their existing weapon and magic effects.

| Words | Duration | Body carrier | Body level | Classic low / high |
| --- | --- | --- | --- | --- |
| One | 110 ms | 104 Hz | 0.32 | 0.28 / 0.14 |
| Two | 170 ms | 88 Hz, with a quiet 132 Hz component | 0.44 | 0.42 / 0.22 |
| Three | 240 ms | 72 Hz, with a quiet 108 Hz component | 0.56 | 0.56 / 0.30 |
| Unknown / voice power | 160 ms | 80 Hz | 0.42 | 0.42 / 0.22 |

Levels are normalized recipe amplitudes before position gain, user scaling, and limiting. All four profiles share `[Body.Voice]` adjustments, including mute, HD/classic gain, duration, and carrier override.

## Body output

The body plan holds two hand cues, two finite hand releases, and one body-wide transient. A stronger cue replaces another only in its own position; discarded cues do not return as a backlog.

The builder renders bounded 48 kHz stereo windows from each cue's original time. Hand cues route to their side; body-wide cues are mirrored. Smooth onset/release and deterministic noise shape the waveforms.

Position gains are 0.50 per held hand, 0.50 per hand transient, and 0.75 for body transients; melee swings use 0.75. The final peak ceiling is 0.75 times the clamped body user scale. Removing a cue does not increase another cue's gain.

| Cue | Body carrier |
| --- | --- |
| Unarmed | 78 Hz, damped contact |
| Dagger | Contact: 260 Hz resonance + 104 Hz body; swing: 104 Hz |
| Sword | Contact: 216 Hz resonance + 82 Hz body; swing: 82 Hz |
| Axe | Contact: 176 Hz resonance + 58 Hz body; swing: 58 Hz |
| Mace | 68 Hz, damped heavy contact |
| Two-handed | Contact: 54 Hz body + weak 144 Hz component; swing: 54 Hz |
| Incoming hit / shield block | 104 / 78 Hz |
| Bow readiness / release | 265 / 340 Hz |
| Crossbow shot | 72 Hz |
| Fire / frost / shock | 116 / 76 / 290 Hz |
| Healing / ward / generic magic | 188 / 128 / 174 Hz |
| Voice fire | 104 / 88 / 72 Hz for one / two / three words; 80 Hz when unknown |
| Critical-health heartbeat | 66 / 94 Hz pair, body level 0.30 |

Body carrier frequencies use hertz. Adaptive-trigger vibration uses encoded device values.

Known melee families also have distinct timing. These are finite envelopes from the actual action timestamp; a delayed delivery renders only the remaining tail.

Carrier and envelope differences describe authored digital profiles. Their perceived separation requires comparison on each controller.

Blunt contacts use damped bass pulses. Blade contacts combine a resonant component and a lower
body component; body weight increases from dagger to sword to axe. Preparatory swings omit the
impact-excited resonant component. Two-handed is a neutral heavy profile because the current class
does not distinguish every blade/blunt subtype. These are authored tactile projections of impact
properties, not recordings or material recognition. See the [research and selection rationale](../../../docs/haptics-rationale.md).

| Weapon | Swing / contact duration |
| --- | --- |
| Unarmed | 55 / 70 ms |
| Dagger | 50 / 60 ms |
| Sword | 60 / 80 ms |
| Axe | 65 / 90 ms |
| Mace | 75 / 100 ms |
| Two-handed | 90 / 110 ms |

Power attacks add 10 ms to contact, capped at 120 ms; bash contact remains short. Foot-contact envelopes last 50 / 55 / 60 / 55 ms for walking / running / sprinting / sneaking and 60 / 65 / 70 ms for mounted walking / running / sprinting. These quiet tails preserve a digital signal under short delivery delays. Ordinary walk, run, and sneak feedback remains optional. The critical-health heartbeat retains its two-beat cadence at a lower PCM level than combat impacts.

## Suppression and fallback

Menus retain finite neutral body ownership while suppressing gameplay effects. Loading, death, lost focus, faults, and stale observations withdraw output. Recovery requires fresh game state.

USB HD requires the exact ready audio instance. Classic fallback uses the authored rumble envelope or silence. Renewals preserve cue timing, and failed one-shot PCM is not replayed after reacquisition. See [architecture](architecture.md) and [shared delivery](../../../docs/effects.md).

Configure strengths and individual profiles through the [INI settings](../README.md#settings).
The LED policy and its full priority order are documented in [lightbar feedback](../../../docs/leds.md).
