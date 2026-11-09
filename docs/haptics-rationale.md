# Haptic design rationale

These settings are research-informed authored waveforms for DualSense and DualSense Edge.
They are not physically calibrated reference effects. A carrier is one part of a waveform, alongside
amplitude, secondary components, noise, attack/release, decay, rhythm, routing and action timing.
This document covers the 52 shared Skyrim SE/LE and 31 Fallout 4 default body profiles, plus their
adaptive-trigger profiles. Physical comfort, recognizability and controller-to-controller equivalence
remain unmeasured.

The tables below describe the built-in fallback recipes. Compiled
[audio-derived profiles](audio-haptics-baking.md) provide source-shaped envelopes for selected
transients while retaining authored output levels and finite event timing. Held Skyrim roles use stationary source-mean gains on the existing continuous renderer; their levels are multiplied by the source mean sum, without looping the transient envelope. An optional external
bank can override those profiles by section. Their carrier targets and processing choices are
recorded in the baking manifest; this is not a direct copy of acoustic frequency or proof of
physical fidelity.

## Evidence and its limits

| Primary source | Observation used here | Limit on transfer |
| --- | --- | --- |
| [Mahns et al., 2006, Table 1](https://journals.physiology.org/doi/abs/10.1152/jn.00483.2005) | Mean fingertip frequency Weber fractions were approximately 0.19 at 50 Hz, 0.21 at 100 Hz and 0.14 at 200 Hz. Frequency separation should be considered relative to the carrier. | Five participants, controlled probe contact and stimulus level; these are not short-impulse DualSense thresholds or action-frequency recommendations. |
| [Harris et al., 2006](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0000100) | Amplitude changes and temporal noise affected frequency judgments. | Fingertip experiments with much longer stimuli; our additive waveform noise is not their cycle-timing jitter. No inverse-frequency amplitude correction is inferred. |
| [DualSense validation, Frontiers 2024, sections 2.6, 3.1 and discussion](https://www.frontiersin.org/journals/neuroscience/articles/10.3389/fnins.2024.1425398/full) | Measured controller response is not flat across frequency. | The 10–1050 Hz input sweep is the measurement stimulus, not a manufacturer-rated or uniformly perceptible bandwidth; the setup used a blanket instead of a player's grip and did not establish Edge equivalence. |
| [Sony waveform authoring, 2020](https://www.sony.com/en/SonyInfo/technology/stories/entries/Haptics/) | Sony describes waveform design and generation from game sound effects. | This provides an authoring approach, without a public table of optimal action frequencies. |
| [Team ASOBI GDC 2025 session description](https://gdcvault.com/play/1035347/Feel-the-World-The-DualSense) | The session describes Astro Bot interactions, waveforms and integration with audiovisual feedback. | The session description is a production reference, not numeric calibration data used by these presets. |

The 20–1000 Hz `FrequencyHz` range is a software limit on the primary carrier override. It is not
the controller's measured mechanical passband. The 48 kHz PCM sampling rate is also not a tactile
bandwidth specification. Trigger device codes and classic low/high amplitudes are not physical Hz.

## Impact timbre and weight

Rule **M1** separates timbre from weight. Unarmed and mace use damped low-frequency contact
pulses. Dagger, sword and axe contacts combine a resonant mode and a lower body mode. Increasing
body mix, duration and persistence gives the heavier blade profiles more weight without making
unarmed and axe neighbors in the same linear pitch ladder. Swings contain only the lower preparatory
cue: a free-air action does not excite a contact resonance in this model. These are authored tactile
projections, not extracted weapon recordings or a physical modal simulation.

This inference follows research on recorded impact sounds: [Aramaki et al., 2011](https://kronland.fr/publications/controlling-the-perceived-material-in-an-impact-sound-synthesizer/)
found that damping and spectral content contribute to perceived material. [Hjortkjaer and McAdams,
2016](https://orbit.dtu.dk/en/publications/spectral-and-temporal-cues-for-perception-of-material-and-action-/)
found different contributions of spectrum and temporal envelope to material/action judgments.
Their acoustic results do not specify DualSense frequencies. The numerical projection below is an
explicit design choice; it requires validation in the hands.

| Family | Swing Hz | Contact main + second Hz | Second amplitude mix | Swing / contact, ms | Swing / contact level | Attack / release, ms; decay, /s |
| --- | ---: | --- | ---: | --- | --- | --- |
| Unarmed | 78 | 78 + none | 0 | 55 / 70 | 0.44 / 0.58 | 2.5 / 14; 18 |
| Mace | 68 | 68 + none | 0 | 75 / 100 | 0.68 / 0.82 | 1.5 / 26; 10 |
| Dagger | 104 | 260 + 104 | 0.18 | 50 / 60 | 0.50 / 0.62 | 0.8 / 10; 8 |
| Sword | 82 | 216 + 82 | 0.30 | 60 / 80 | 0.56 / 0.70 | 1 / 16; 6 |
| Axe | 58 | 176 + 58 | 0.44 | 65 / 90 | 0.60 / 0.76 | 1.2 / 22; 4.5 |
| Two-handed | 54 | 54 + 144 | 0.18 | 90 / 110 | 0.74 / 0.88 | 1.8 / 30; 4 |

Second mix is an amplitude weight, not energy fraction. Both components currently share one decay
and envelope. Thus this is a compact texture approximation; it does not reproduce independently
measured decay rates of real modal components. Two-handed is neutral-heavy because the captured
weapon class merges blade and blunt subtypes. No material is inferred from that grouping.

Unarmed classic fallback uses low/high amplitudes of .14/.08 for swings and .30/.18 for contacts.
The low channel is dominant; these authored amplitudes have no perceptual loudness normalization.

Power, block and bash scale both frequencies together. User primary-frequency overrides preserve
the component ratio and may deliberately change the intended texture. No inverse-frequency or
frequency-squared amplitude correction is applied: PCM amplitude is not measured skin displacement,
velocity or acceleration, and the mechanical transfer function under grip is unknown.

## Input data and physical sound

The current Skyrim body cue contains weapon class, action flags and block state, without contact
material or the audio actually played. Compiled or externally overridden audio-derived envelopes
are copied from startup settings, rather than captured from a live sound descriptor. Its ordinary
TESHitEvent likewise has no material field. CommonLib nevertheless
provides BGSMaterialType, material-indexed BGSImpactDataSet and impact sound descriptors: a future
capture path could obtain more context, but must correlate it to the actual contact. Equipped armor
alone is not proof of the material at the hit point. Skyrim LE uses its separate SDK path.

Fallout 4 TESHitEvent has a material name and optional HitData with a numeric material field. Our
native normalization currently copies flags and health damage, omitting those material fields. This
is an integration limit, not a library limitation. Actual field population and classification would
need game-level validation. Adding that capture path is separate from authoring these defaults.

A sound descriptor is not the PCM actually played after variant selection, pitch changes and mixing.
These presets therefore use sound-informed structures without claiming exact reproduction of the
current game audio, target material, armor, weapon geometry or modded sound assets.

## Rationale rules for all remaining profiles

The rules below justify the role and composition, including opt-in default recipes. They do not imply that a publication selected the
exact carrier, amplitude or duration. Those exact choices remain hypotheses unless an applicable
digital requirement is stated. An effect can share a carrier with another role. No global pitch
ladder is imposed across unrelated actions, phases, warnings or firearm families.

| Rule | Profiles covered | Composition and decision |
| --- | --- | --- |
| B1 | Skyrim bow draw and three tension phases | Keep phase-dependent carriers 118/150/160 Hz and lower held gain; phase, level and trigger resistance carry the progression. These close carriers are not promised to be recognizable in isolation. |
| B2 | Skyrim bow readiness/release and crossbow shot | Keep finite readiness/release accents at 265/340 Hz, release secondary at 170 Hz, crossbow at 72 Hz. These are authored transient textures; no measured material or weapon-frequency identity is claimed. |
| S1 | Six Skyrim magic kinds, concentration and release | Keep the six carrier/envelope/noise/modulation combinations. Exact fire/frost/shock/healing/ward/generic labels are authored associations. Sustained and release roles are differentiated by envelope and duration. |
| I1 | Skyrim incoming/shield hit and active world cues | Keep priority, bounded impulse energy, stagger/knockdown/landing envelopes and paired stamina/landing components. Water entry and exit have no feedback profiles. Carrier overlaps are accepted across roles. Stronger/longer cannot be inferred from carrier alone. |
| Q1 | Seven Skyrim foot/mounted gaits and three Fallout step profiles | Keep low level and finite foot-contact tails, with walking/running/sprinting magnitude hierarchy. Fixed 40–60 ms per-channel RMS guards detect a vanishing digital tail; they are not sensory thresholds. |
| W1 | Skyrim critical health and Fallout health/AP warnings | Keep low-level two-pulse or finite warning patterns and existing state hysteresis. Heartbeat rhythm is defined by pulse timing and cadence, not the 66/94 Hz carriers. |
| V1 | Skyrim voice/shout including word variants | Keep duration, level and secondary-component changes across words. Their carriers may be close because timing and magnitude also encode progression. Unknown source retains its neutral profile. |
| C1 | Skyrim cold/hunger/exhaustion and two transformations | Keep distinct transient or modulated compositions at their existing levels. Exact frequencies and mapping to content are authored; no semantic recognition result exists. |
| F1 | Ten Fallout firearm recoil families | Keep full main/secondary/noise/envelope compositions and actual-shot timing. Low-frequency single-shot pulses may contain few cycles and are treated as impulses, not stable pitch codes. Heavy/launcher duration and magnitude, energy/cryo higher components, and rotary/flame repetition matter alongside carriers. |
| F2 | Nine Fallout contact/damage profiles | Keep explosion/block/limb/critical/melee/power/bash/general-contact distinctions in envelope, pulse pattern and level, plus actual hit attribution and magnitude. Primary carriers alone are not an identification code. |
| F3 | Fallout reload, armor entry/exit and landing | Keep finite mechanical accents and their different component/envelope combinations. Ordinary step and armor step share Q1's low-level checks. |
| U1 | Fallout VATS entry/exit and critical-ready | Keep brief interface accents at modest levels; preserve gameplay suppression and trigger release. Exact pitches are authored UI associations. |

## Digital budgets

- M1 spectra are measured after the renderer and limiter on a fixed 200 ms right-channel window. Goertzel power is summed on 5 Hz bins from 20 through 500 Hz. Blunt contacts and preparatory blade swings have less than 15% of this power at 150–500 Hz; blade contacts have more than 45%. Blade-contact centroids exceed the maximum blunt centroid by a factor of 1.4. The unarmed/mace centroid distance is smaller than unarmed/blade distance. These are authored digital regression budgets, not material-recognition or tactile thresholds.
- Confirmed melee contact carries more rendered stereo energy than swing. Existing 20/40 ms
  remaining-energy guards are relative checks, with no universal perceptual meaning.
- Q1 tails use a fixed 20 ms window starting 40 ms after the original cue time. All 960 frames at
  48 kHz count, including trailing silence. Each channel must reach 0.015 FS RMS at nominal gain,
  or 0.0075 FS at half gain, over 16 deterministic noise seeds. These are authored digital budgets
  (.01 FS plus 50% headroom), not acceleration measurements or a guarantee of perceptibility.
- The shared renderer retains its existing peak limiting, scale-zero mute, original timestamps,
  finite expiry and phase/noise continuity across windows. These are software properties.
- Trigger choices are checked after default global scaling and discrete encoding. Nonzero encoded
  force and distinct zones/levels are digital properties; an encoded level is not a calibrated force
  in newtons, a comfort rating or proof of semantic recognition. Device frequency codes remain codes.

The executable catalogue records all 83 base profiles, their variants and trigger scenarios directly
from the production builders. Each body profile is assigned one of the rationale rules above.

## Physical validation still required

On each controller separately, first check matched perceived intensity across candidate carriers,
then compare complete profiles using blinded pairs. Test all fifteen melee-family pairs for both
swing and contact, with common seeds first and varied seeds/gameplay mixing later. Include soft
steps, sustained magic, repeated firearms, simultaneous cues and user-selected lower gain. Measure
mechanical output under a repeatable grip if objective intensity calibration is needed.

No ABX result, comfort assessment, measured tactile threshold or Astro Bot equivalence is currently
available. Hardware validation can justify changing these authored values again; preserving an
integer or a numerical spacing target takes no precedence over observed quality.
