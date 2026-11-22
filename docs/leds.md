# Lightbar feedback

Each adapter selects one lightbar indication from current game state using the priorities below.

| Adapter | Health | Sneaking and detection | Radiation |
| --- | --- | --- | --- |
| Skyrim SE/AE | Yes | When the optional source is available | — |
| Skyrim Classic/LE | Yes | — | — |
| Oblivion Classic | Yes | From the game's stealth eye while sneaking | — |
| Fallout New Vegas | Yes | Four states while sneaking when the native source is available | — |
| Fallout 4 | Yes | When the optional source is available | From real source-count events |

## Health

By default, a change in current HP shows health for four seconds. Further changes restart the timer. Color follows the health ratio from red through yellow to green. Health loss changes color immediately. Recovery takes 200 ms by default and keeps the original deadline.

The first health sample after startup, loading, a gap or a controller change establishes a baseline. Menus pause the indication. On return, the adapter takes a fresh baseline and restores any remaining part of the original health window. Changing only maximum HP can update an active color. Missing or invalid health releases the health indication.

Critical health has the highest priority during the health window. By default, it enters at 20% or below and leaves at 25% or above. The window expires on schedule even if HP stays low.

## Priorities

The table is ordered from highest to lowest priority.

| Priority | Indication | Eligibility |
| --- | --- | --- |
| 1 | Critical health | Active health window and critical HP |
| 2 | Recent damage | Active health window and independently confirmed recent damage |
| 3 | Radiation | Known positive current radiation source count |
| 4 | Combat health | Active health window while in combat |
| 5 | Stealth | Known detection state while sneaking |
| 6 | Health | Any remaining active health window |

Health timers continue running under higher-priority indications. Any HP change can start the window, including healing and gradual environmental damage.

## Source limits

Skyrim SE/AE and Fallout 4 interpolate from blue through violet to magenta (`#FF00FF`) as the raw detection score rises from 0 to 100 by default. Values outside the configured range use the nearest endpoint.

Oblivion Classic follows the stock stealth eye. The dim eye maps to blue, and the bright eye maps to magenta. [Oblivion GOTY manual, pp. 40–41](https://cdn.akamai.steamstatic.com/steam/apps/22330/manuals/obliv_goty_pc_man-v2.pdf)

Fallout New Vegas uses blue for Hidden, shades of violet for Caution and Detected, and magenta for Danger. [New Vegas lightbar settings](../adapters/fallout_new_vegas/README.md)

Stealth feedback ends when the player stops sneaking or detection becomes unavailable.

Fallout 4's yellow-green pulse shows the presence of a radiation source. After startup or loading, the first source-count event establishes the current value. Changing controllers preserves that value until the next game load or reset.

## Settings

Edit `[LED]` in the game's working INI and restart the game. The reference INI lists the supported keys and their defaults.

| Key | Default | Meaning |
| --- | --- | --- |
| `Enabled` | `true` | Enable lightbar feedback |
| `HealthEnabled` | `true` | Enable health indications |
| `StealthEnabled` | `true` | Enable stealth in adapters that supply detection data |
| `RadiationEnabled` | `true` | Enable radiation in Fallout 4 |
| `HealthHoldMs` | `4000` | Health window after a change, 1–60,000 ms |
| `HealingTransitionMs` | `200` | Recovery smoothing, 0–1,000 ms |
| `CriticalEnter` | `0.20` | Enter critical health at or below this ratio |
| `CriticalExit` | `0.25` | Leave critical health at or above this ratio |
| `DetectionHiddenScore` | `0` | Score for blue in Skyrim SE/AE and Fallout 4 |
| `DetectionAlertScore` | `100` | Score for magenta in Skyrim SE/AE and Fallout 4 |

Critical thresholds must satisfy `0 <= CriticalEnter < CriticalExit <= 1`. Detection endpoints must be integers with `DetectionHiddenScore < DetectionAlertScore`. Invalid settings reject the configuration. Body and trigger switches, strength scales and the game's vibration option do not disable lightbar feedback.

## Availability

The adapter releases its lightbar request in menus and on focus loss, stale observations, loading, death, player unavailability or controller changes. Reconnecting requires a fresh observation. InputNext handles priority between clients.

While the adapter controls the LED group, the player and microphone indicators are off. See the game's installation guide for supported controllers and connections.

Developer contracts and live-test scenarios are in [effect delivery](effects.md#lightbar-policy-and-delivery).
