# Skyrim adapter architecture

The native layer supplies value observations to a shared effect policy and a serialized output coordinator. Game pointers stay inside the native boundary.

| Component | Responsibility |
| --- | --- |
| Native observation | Player, equipment, caster, attack, UI, and event-source data |
| Recipes and policy | Cue selection, hand phases, priorities, cancellation, and effect envelopes |
| Coordinator | Game epochs, bounded inboxes, freshness, and controller selection |
| Shared `LedPolicy` | Health baselines, LED timing, color transitions and indication priorities |
| Shared delivery | Independent trigger/LED caches, finite body ownership, PCM, and runtime calls |
| Native owner | Startup, stable callbacks, worker, and queued SKSE tasks |

## Lifetime and scheduling

Startup validates runtime and interfaces before publishing callbacks. SKSE messaging has no unregister operation, so the owner and pinned plugin stay alive for the process. Callback-escaped partial failures retain inert state. Heavy teardown does not run under loader lock.

Callbacks copy bounded IDs with epoch and timestamp. They admit player actions only after an observation establishes player identity. They do not resolve forms or submit output under event-source locks.

One worker owns coordinator ticks and reserves at most one SKSE task for native sampling. A task reads native state outside the coordinator locks and publishes checked values; it never ticks or sends output. Publication wakes output processing without requesting another native sample. Events, lifecycle controls, focus changes, and task-detected menu transitions request urgent sampling and output processing. Task completion also wakes output processing, while regular sampling remains bounded by its observation deadline.

The worker reserves and dispatches due observation work before its output tick. Tickets are revalidated at dispatch and publication. A slow body call can delay the next worker iteration, but cannot delay an observation already dispatched in that iteration.

The coordinator has separate ingress and delivery locks. Callbacks briefly transfer copied values through ingress; policy, PCM rendering, and runtime calls use delivery without holding ingress. Diagnostics return the last completed output snapshot and current ingress fault/overflow state, so a blocked runtime call does not block a native diagnostic read.

Mandatory sink-registration failure leaves the owner inert. The optional footstep source is retried until available and registered once.

## Native interpretation

Native health, combat and optional sneaking/detection values enter the shared `LedPolicy` through Skyrim's policy. LE supplies health and combat only. The selected lightbar intent uses shared delivery; native sources never submit output. See [lightbar behavior](../../../docs/leds.md) and [delivery contracts](../../../docs/effects.md#lightbar-policy-and-delivery).

SE/AE native tasks retain the immutable startup LED settings. Disabled LED or stealth skips detection queries; health values already needed by haptics are reused. Detection is sampled only during admitted gameplay while actually sneaking.

Actor-value and state access use CommonLib's runtime-dependent subobject accessors. Ordinary C++ inheritance casts do not describe every supported runtime layout.

Action source identity stays exact. Staff effects use that staff's enchantment; held spell feedback validates the actual caster against hand equipment. A selected spell is not substituted for a weapon source.

Static race and Survival-global bindings are resolved and validated on the first admitted game-task read after DataLoaded. Missing optional plugins are cached as absent. Player state and global values remain live; an invalid value is checked again on the next read. LE caches its optional race IDs and reads no Survival globals.

## Session boundaries

Load and fault transitions invalidate actions, observations, and policy. Menus invalidate gameplay actions while retaining finite neutral body ownership. Resume requires an observation newer than the transition. Old task epochs cannot publish into a new session.

Inbox overflow discards transient history and advances its burst revision; a new observation can reconcile held state. Release and equipment changes cancel held output against observation time, preventing a later tick from rebuilding it from the same sample. Duplicate handled events cannot cancel a recovered hold.

Non-menu invalidation requires the old body owner to clear or expire before reacquisition, even when loss and recovery occur between ticks. A controller change clears bursts and requires a read started after selection. Output is withdrawn from its original target, never redirected to a replacement.

## Output timing and recovery

Observations expire after 150 ms. Trigger and LED requests and body leases last at most 200 ms. Body ownership refreshes every 100 ms during an admitted session, including neutral menus.

Production time is sampled after taking the delivery lock and refreshed after synchronous calls. Maintenance checks current observations; each effect attempt uses its own frozen observation, whose age cannot be refreshed by a later publication. Remaining frame TTL and admission are checked again after runtime calls. An already admitted foreign call cannot be cancelled; its receipt is recorded before exact-owner cleanup or a neutral menu transition. Explicit supplied time supports deterministic tests.

Body PCM is rendered from original cue times. Fallback samples update as their envelope changes. Failed transient PCM is not replayed unchanged after lease expiry; held effects can retry from their original time.

Body ownership binds the exact target and audio instance. Changed audio identity clears the old owner and requires a newer body sample while leaving independent trigger and LED state intact. Neutral classic output can request HD recovery with `STOP` at the refresh cadence.

Paused ticks continue bounded close/ACK maintenance without resolving a target or opening another context.
An early exit uses one cleanup pass, through reconciliation or standalone maintenance. Contexts retired
after that pass wait for the next tick; later admission checks do not repeat a failed close or ACK.
`BUSY` body clear retains its exact owner for retry; other failures retire the binding through context cleanup.
`STALE_LEASE` settles the local lease. Cleanup failure never extends the native lease or discards a close receipt.

Shared delivery contracts are in [effect delivery](../../../docs/effects.md).

## Extending effects

Add game signals in the native observation boundary and cues in recipes/policy, with tests for source identity, timing, cancellation, and recovery. Controller drivers belong in InputNext.

Shared policy components and reset contracts are described in the [shared core](../../skyrim/README.md#integration).
