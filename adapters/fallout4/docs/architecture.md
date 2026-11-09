# Fallout 4 adapter architecture

The native boundary captures game state and copies events into bounded value data. The policy selects effects; the session serializes their delivery through the shared output layer.

| Component | Responsibility |
| --- | --- |
| `NativeOwner` | Process-lifetime callbacks, startup, and worker scheduling |
| `ProductionOwnerServices` | Runtime loading, F4SE/Win32 startup services, native observations, and diagnostics |
| `NativeOwnerPipeline` | Main/worker coordination, lifecycle admission, observation publication, and event sink registration |
| `OwnerMailbox` | Bounded control/event queues, pending sample, cached player identity, and publication provenance |
| `ObservationRequests` | Request identity allocation and idle/pending/executing transitions |
| UI capture | UI transition state, capture admission, ordered fences, and scalar snapshots |
| UI capture ABI | Gateway machine code, jump encoding/decoding, and supported host checks |
| `Policy` | Observation/event admission, mechanic transitions, trigger decisions, and effect coordination |
| `LandingDetector` | Landing continuity, fall-height tracking, and one-shot landing cue intents |
| `ResourceCueTracker` | Resource baselines, threshold hysteresis, and critical-charge transitions |
| `BodyVoiceMixer` | Five body voices, priority arbitration, stable identities, and classic fallback mixing |
| Shared `LedPolicy` | Health baselines, LED timing, color transitions and indication priorities |
| `Session` | Lifecycle epochs, observation freshness, event admission, and serialized output |

Native LED bindings supply copied facts to `Policy`, which composes the shared `LedPolicy`. The shared `OutputSession` delivers its one selected intent through an independent finite LED cache. Source acquisition, indication selection and SDK ownership remain separate; see [native source contracts](bindings.md#led-health-stealth-and-radiation) and [shared delivery](../../../docs/effects.md#lightbar-policy-and-delivery).

`Policy` owns the sanitized configuration and coordinates copied observations, events, and bounded cue intents. Each detector owns its history; `BodyVoiceMixer` owns the voice pool and sequence counter. Resource cues are offered before event cues, then mechanic and landing cues, preserving arbitration order. Mode and weapon changes clear active voices while preserving the sequence counter; a full effect reset also restarts that counter. LED history follows its independent reset and pause rules.

## Ownership and synchronization

`NativeOwner` remains the callback lifetime owner. Once callbacks escape, the pinned plugin retains an inert owner on partial startup failure. Native SDK reads and animation subscription operations stay in the owner and observation boundary.

`NativeOwnerPipeline::inbox_mutex_` synchronizes mailbox and observation-request state. The worker releases it before calling `Session`; main-thread observations also run outside it and are checked again before publication. Native callbacks copy values into the mailbox rather than calling the output layer.

Lifecycle, focus, UI-generation and inert transitions discard different parts of the mailbox. A pending request may be cancelled; an executing request retains its identity until the matching completion. New requests coalesce while one is pending or executing. Exhausted request identities stop admission rather than wrapping.

UI generation and lifecycle epoch checks surround native observation and publication. An invalidated request cannot publish a sample or reopen event admission. Main-phase SDK work runs outside the inbox lock, with publication checked again afterward.

After processing pending controls and samples, the worker publishes current task provenance and requests a due observation before ticking output. It rechecks stop and generation state before delivery. The main-phase callback publishes to the mailbox, so an already requested observation can finish while body output is blocked; the next worker iteration still waits for that output call.

An unadmitted session withdraws effects and services one bounded cleanup pass per tick. Close receipts and acknowledgements continue progressing while focus or observation freshness prevents new effects. Normal reconciliation and delivery service cleanup themselves.

## UI capture boundary

ABI gateway construction is separate from snapshot admission. Refused capture produces an invalid snapshot without per-attempt dumps or counters.

See [native bindings](bindings.md) for capture fences and compatibility checks, and [validation](validation.md) for build and test commands.
