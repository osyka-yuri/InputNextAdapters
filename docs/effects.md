# Effect delivery

Link `InputNextAdapters::effects` for reusable controller binding, trigger and lightbar delivery, finite body leases, and waveform generation. A game adapter supplies current observations and effect policy.

For authored haptic values, research boundaries and numeric selection criteria, see
[haptic design rationale](haptics-rationale.md). Delivery checks establish digital behavior; they do
not establish perceived intensity, realism or discrimination on a physical controller.

Compiled [audio-derived haptic profiles](audio-haptics-baking.md) provide representative
source-shaped temporal envelopes and held mean-gain projections for 97 game-specific entries. An optional bank prepared
offline from game sound resources overrides matching sections and leaves other defaults active.
Both sources load before the native owner starts; the renderer receives copied values and performs
no file access or decoding. Missing or invalid external banks retain the compiled profiles.
Supported Skyrim held roles project source means onto the existing continuous renderer. The
audio-haptics opt-out returns both transient and held roles to synthetic recipes.

## Interfaces

| Type | Role |
| --- | --- |
| `OutputPort` | Runtime query and submission interface, also usable by test doubles |
| `RuntimeOutput` | `OutputPort` backed by a `RuntimeClient` |
| `OutputSession` | Controller binding, trigger and LED caches, body ownership, and recovery |
| `DeliveryObserver` | Admission, frame restrictions, and binding-change callbacks |
| `OutputFrame` | Optional left/right trigger, LED and fallback-rumble intents, plus a body program |
| `LedPolicy` | Shared health, stealth and radiation lightbar selection from copied observations |
| `BodyProgram` | Up to five independently routed, timed body voices |
| `TickClock` | Steady-clock time in production or supplied time for deterministic tests |

Public headers are under [include/inputnext_adapters](../include/inputnext_adapters/).

## Caller responsibilities

The borrowed output port and observer must outlive the session. Serialize all session calls through the adapter's output gate. Observer callbacks are synchronous: they may reset attempt history and game state, but must not recursively reconcile, send, withdraw, clear, or call the output port.

The adapter owns observation freshness, focus and lifecycle admission, event queues, and slot selection. Use `DeliveryObserver::admitted()` to enforce those rules.

`frame_restriction()` can return `FrameRestriction::neutral` to silence gameplay while retaining finite neutral body ownership, for example during a menu transition. Its default is `none`. A neutral restriction remains in force for the rest of that `send()` call, even if gameplay resumes before the call returns. Hard admission loss still withdraws accepted output and releases body ownership.

Admission and restrictions are checked again at submission boundaries and after synchronous runtime calls. An already admitted call cannot be cancelled; its accepted receipt is recorded before cleanup against the exact owner. Keep observer checks short, and never hold an ingress lock across session or runtime calls.

## Binding and delivery

1. Start `RuntimeOutput` with an absolute path to the chosen runtime.
2. Construct a session for an explicit slot, 0–3.
3. Call `reconcile()` with the observation time. A new binding requires an observation newer than the binding's start.
4. After successful reconciliation, build an `OutputFrame` and call `send()` with its sample time and clock.
5. Close and drain the session when hard admission ends; use a restricted neutral frame when neutral ownership should remain.

A context is bound only after `open_context` returns `OK`; other statuses create no caller-owned context.
An unsuccessful `reconcile()` suspends `send()` until reconciliation succeeds, without blocking cleanup.
A `PENDING` or `BUSY` context refresh preserves the known binding without consuming output data.
Revocation retains the exact context for cleanup.
`bound()` reports a retained `OPEN` binding, including one whose delivery is suspended.

Targets identify an exact runtime, device and connection. A changed target discards delivery state. Audio-instance changes require a fresh body baseline while preserving independent trigger and LED state. Effects are never redirected from an old target to a replacement controller.

Body clear on `OK` or `STALE_LEASE` releases the local lease owner. `BUSY` preserves the owner and sequence
for retry until its retention deadline. Other clear errors retire the binding and retain its exact context for
close/ACK cleanup; a full cleanup pool keeps the binding and owner until capacity becomes available.
Neither staleness nor local retirement proves a physical clear.

A `BUSY` target query or trigger operation preserves the binding and accepted caches. Other target and trigger
failures retire the binding through the same context cleanup path.

## Lifetimes and body output

Trigger intents, LED intents and fallback-rumble snapshots accept TTLs of 1–200 ms, aged from the observation time.
Trigger requests use the remaining TTL. Fallback values travel in the finite body lease; a later submission
updates or silences them as their snapshots expire. If servicing stops, the body lease bounds their native lifetime.

Left and right triggers share a request when both need submission and have the same remaining TTL. Their accepted caches and lifetimes remain independent; an unchanged channel is not refreshed just to join a batch.

`send()` attempts eligible triggers, then LEDs, then body work. A retryable channel failure leaves independent channels eligible. After synchronous calls, cleanup removes accepted trigger and LED state that expired or became restricted; it does not replay the cue. Calls remain serialized, so this order provides an earlier delivery opportunity without guaranteeing a continuing cadence through a blocked runtime call.

Body ownership uses a 200 ms lease with a 100 ms refresh interval. A silent lease still suppresses lower-priority rumble. Clears address the exact accepted owner; failed clears never extend the native lease.

Before destroying a session, retry `close_and_drain()` until it completes, keeping the output port and observer alive.
Session destruction performs no output I/O. TTL expiry alone does not release context capacity.

Cleanup retains exact context/target identities and complete close receipts across transient failures. A retained
receipt is retried through ACK; the session does not reopen or replace its owner. Up to 32 local cleanup records
can be retained; saturation blocks another binding instead of discarding one. The runtime's separate 32-context
public limit is shared by clients of the same runtime. Native TTL starts at SDK entry. The client retains a lease identity
until clear/staleness or a conservative bound of call return plus TTL; that bound does not extend the native effect.

Regular cleanup advances one pending record per pass, in round-robin order: at most three runtime calls
(query, close, ACK), or one ACK for a retained receipt. Admission loss can add a second pass during `send()`.
`close_and_drain()` visits the full pool. These bounds limit work per call, not the duration of synchronous runtime operations.
`reconcile()` and `send()` each service cleanup. Call `maintain_cleanup()` when neither operation runs,
so pending close/ACK operations continue without target resolution or opening another context.

A body program contains at most five voices with their own routing, source identity, timing, and priority. `build_body_pcm()` renders bounded 48 kHz stereo windows of up to 9,600 frames from the original cue times. Game adapters select the recipes and position gains.

PCM is rendered only when capabilities advertise HD readiness and a nonzero audio-instance ID. Without them, the session maintains its finite body lease and rumble fallback using `KEEP` with no PCM. With advertised HD, a classic receipt still permits PCM retries for new or renewed cues.

Each body request carries an explicit rumble fallback. Renewals preserve cue timing; expired ownership does not revive an old lease or automatically replay a finished transient.

## Lightbar policy and delivery

`LedPolicy` consumes copied `LedObservation` values and explicit time without runtime I/O. It owns health baselines, smoothing, deadlines and indication selection. Adapters collapse independent incoming-hit and confirmed-damage evidence to their latest timestamps before updating it. Equal normalized input, update time and prior state produce the same result; event arrival order within that input does not choose the winner.

An attributed hit with unknown damage can promote a corresponding sampled HP decrease. Independently known positive damage can activate health even when simultaneous healing leaves sampled HP unchanged. Neither case changes the ordinary HP-change rule. Invalid programmatic `LedSettings` disable policy output. See [lightbar feedback](leds.md) for priorities, settings and source limits.

`LedIntent::source_at` preserves the original observation time independently of the frame's render timestamp. Policy freshness is 150 ms; delivery accepts finite TTLs of at most 200 ms aged from that source. A logical health window is maintained by repeated fresh observations, never by a four-second native lease.

The session submits the complete LED group, records its accepted revision, and conditionally clears that exact contribution when absent, expired or restricted. Trigger cleanup does not clear LED ownership. Cleanup after body work also rechecks triggers if a blocking LED clear removed its owner. Audio-only changes preserve the independent LED cache.

Use `withdraw_leds()` with trigger/body withdrawal in external admission-loss paths. Full context close also withdraws LEDs. Releasing ownership requires expiry or withdrawal; submitting black retains it. The policy explicitly sets player index, microphone LED and reserved fields to zero because the SDK owns the group atomically.

### Live LED validation

Automated tests do not establish native engine calls, event cadence or physical output. Validate on the supported game build and controller:

| Scenario | Check |
| --- | --- |
| Load a Fallout 4 save while exposed to radiation | The initial count stays unknown until a real event; subsequent zero/positive counts update the indication |
| Sustained regeneration, damage, healing and sneak transitions | Colors and priority follow current state; hidden health timers keep running |
| Menus, focus changes and controller reconnection during a long session | Old indications withdraw and do not replay or move to another controller |
| Another LED client gains and releases ownership | Runtime arbitration and physical handover follow the SDK priority/expiry rules |

For detection cost, profile the native detection call separately from first-use Address Library resolution. After warm-up, record call counts and latency percentiles along the same populated route with LED disabled, stealth disabled and both enabled. Disabled cases must make zero detection queries. Record game build, mods and scene; no in-game cost bound is established by component tests.

## Diagnostics

`diagnostics()` reports the last status, body-owner presence, accepted route, and fallback reason.
The last status includes cleanup operations and does not indicate delivery readiness.

An accepted route describes logical admission, not native completion or a physical effect. The session does not
poll progress. Lower-level integrations retaining their own receipts can query correlated progress through `RuntimeClient`.

Use [RuntimeClient](runtime-client.md) for direct ABI calls and [SDK reference](sdk.md) for header provenance and runtime limits.
