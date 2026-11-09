# Lightbar feedback

Each adapter selects one lightbar indication from current game state using the priorities below.

| Adapter | Health | Sneaking and detection | Radiation |
| --- | --- | --- | --- |
| Skyrim SE/AE | Yes | When the optional source is available | — |
| Skyrim Classic/LE | Yes | — | — |
| Fallout 4 | Yes | When the optional source is available | From real source-count events |

## Health

By default, any change in current HP shows health for four seconds, including damage, healing and passive regeneration. Further changes restart the timer; continuous changes can keep it visible indefinitely. Color follows `currentHP / maxHP`, from red through yellow to green. Loss changes color immediately; recovery smooths over 200 ms by default without extending the timer.

The first sample after startup, loading, an observation gap or a controller change establishes a silent baseline. A menu is an intentional pause: output stays dark there, and a fresh same-player gameplay sample can restore a still-live indication only until its original deadline. HP observed across the menu establishes a baseline; it does not create or extend a health window. Changing maximum HP can update an existing indication's color but does not start another window. Missing or invalid health produces no health indication.

Critical health has the highest priority while a health window is active. By default it enters at 20% and leaves at 25%. Static critical HP still expires with that window; gradual environmental damage activates it normally.

## Priorities

The first eligible row wins:

| Priority | Indication | Eligibility |
| --- | --- | --- |
| 1 | Critical health | Active health window and critical HP |
| 2 | Recent damage | Active health window and independently confirmed recent damage |
| 3 | Radiation | Known positive current radiation source count |
| 4 | Combat health | Active health window while in combat |
| 5 | Stealth | Known detection score while actually sneaking |
| 6 | Health | Any remaining active health window |

Entering sneak cannot replace recent damage or combat healing. Radiation takes precedence over ordinary healing. Hidden observations and timers continue updating, so an expired health indication does not replay when another effect ends. Ordinary health activity never requires an attack event.

## Source limits

With default settings, stealth transitions from blue through violet to bright magenta (`#FF00FF`) as the raw detection score rises from 0 to 100. Nonpositive scores stay blue; scores at or above 100 stay magenta. The score is not a percentage. Custom detection endpoints change this color range and may need tuning for mods. Unknown detection data suppresses stealth.

Fallout 4's yellow-green radiation pulse indicates current source presence, not RAD/s or accumulated dose. The available source provides change events without an initial value. Radiation therefore remains unknown after startup or loading until the first real count event; an already-exposed player may initially have no indication. Changing controllers preserves a known count until the next game load or reset.

## Settings

Edit `[LED]` in the game's working INI, then restart the game. Missing keys use the defaults below. The game's reference INI lists its supported keys; it is not loaded automatically.

| Key | Default | Meaning |
| --- | --- | --- |
| `Enabled` | `true` | Enable lightbar feedback |
| `HealthEnabled` | `true` | Enable health indications |
| `StealthEnabled` | `true` | Enable stealth; SE/AE and Fallout 4 only |
| `RadiationEnabled` | `true` | Enable radiation; Fallout 4 only |
| `HealthHoldMs` | `4000` | Health window after a change, 1–60,000 ms |
| `HealingTransitionMs` | `200` | Recovery smoothing, 0–1,000 ms |
| `CriticalEnter` | `0.20` | Enter critical health at or below this ratio |
| `CriticalExit` | `0.25` | Leave critical health at or above this ratio |
| `DetectionHiddenScore` | `0` | Blue endpoint; SE/AE and Fallout 4 only |
| `DetectionAlertScore` | `100` | Magenta endpoint; SE/AE and Fallout 4 only |

Critical thresholds must satisfy `0 <= CriticalEnter < CriticalExit <= 1`. Detection endpoints must be integers with `DetectionHiddenScore < DetectionAlertScore`. Invalid settings reject the configuration. Body and trigger switches, strength scales and the game's vibration option do not disable lightbar feedback.

## Availability

Menus suppress gameplay lightbar output. Focus loss, stale observations, loading, death, unavailable players and controller changes clear the prior indication; reconnecting requires fresh observations. When an indication ends, the adapter releases its LED contribution; InputNext handles priority between different clients.

Player indicators and the microphone indicator are off while this feature controls the LED group. See the game's installation guide for supported controllers and transport.

Developer contracts and live-test scenarios are in [effect delivery](effects.md#lightbar-policy-and-delivery).
