# Fallout 4 native bindings

The native layer targets **Fallout 4 1.11.240.0**, x64, with **F4SE 0.7.9**. Bindings validate executable metadata and relied-on instructions before startup; incompatible images disable the adapter.

| Identity | Value |
| --- | --- |
| Executable SHA-256 | `fdcef37ac1230af6d0b0050eb2142b139ef3a867b37b9211fb6edfcc646072f8` |
| Reference Steam PE timestamp / image size | `0x6a727fcc` / `0x0425a000` |
| PDB identity / age | `90f7c238-fc83-4ce6-b480-28951cf1d740` / `1` |
| Address Library SHA-256 | `65985cc2259384a13cffb538d74776e422e62b0cd3766485a000620242b72b06` |
| CommonLibF4 pin | `7c8c6f8a349ac85abf6fc8a7e5a71732d9339954` |

## Observation dispatch

The worker publishes at most one immutable request. A wrapper replaces the regular loop-head `CALL` in `Main::Run` at RVA `0xc3151e`, targeting the message-pump routine at `0xc336b0`. The call passes `RE::Main*` in RCX and has no consumed return value. The wrapper forwards that pointer to the original exactly once, then invokes the enabled observation callback. The pump dispatches pending Windows messages before the callback; it is nonblocking, and the callback runs only when `Main::Run` reaches this loop head. No cadence is guaranteed while the main thread is stalled or loading.

The call precedes Main's conditional idle/sleep branch. Both the `OnIdle` path and the 50 ms sleep path re-enter the loop head. A completed UI pass is not guaranteed: native observation still requires checked static UI state zero and all private-gate and generation fences to pass.

`WaitForThreads` at `0xbd51c0` joins worker state 1. State 2 handles UI messages and menus independently. Observation eligibility therefore uses checked static state and explicit fences. No owner `AddTask` path is used.

Busy UI state defers the request; stale generation, lifecycle, focus, or closed capture cancels it. Events, observations, cache entries, and worker admission carry the same UI generation.

## Bounded UI transition acquisition

Startup requires messaging interface 1 and trampoline interface 1. A pinned 128-byte F4SE branch pool supports hooks at the UI helper, public `ControlMap::RemapButton`, and Main's message-pump call.

The helper preserves incoming CL, full-width entry RAX, and return value. It counts both transition directions, including no-op and trailing false calls, until the original returns. Public remapping is fenced for its complete synchronous call; nested and concurrent calls remain counted.

Before writing branches, the installer validates all original sites, prepares gateways and pointers, and pins the module. It commits once in helper → remap → Main message-pump order, activating observation only after all sites are ready. Each write is verified with its untouched guard tail, pool, and instruction-cache flush.

An intact original or exact owned branch after a failed write/check/flush leaves capture permanently disabled and resources pinned. Unexpected post-attempt control-flow bytes fail fast. There is no rollback, chaining, or retry.

### Capture fences

The scalar reader makes one nonblocking attempt at each fence, in order:

1. `UI::processMessagesLock`: one owner/count CAS.
2. Private transition SRW lock: try-exclusive with zero in-flight count.
3. Menu-stack `BSReadWriteLock` at `0x3dbd368`: CAS from zero to one at count offset four.
4. Input-context `BSReadWriteLock` at `0x339fb98`: the same read subset.

Release is in reverse order. Contention declines the snapshot without retrying.

Eligibility requires the manager slot to match static storage at `0x32a1320`, UI state zero at `0x32a1388`, shutdown zero at `0x32a138c`, running one at `0x32a138e`, matching nonzero generation, and no transition in flight.

Capture copies at most 64 context IDs, 64 menu facts, VATS mode, and the first matching attack mappings from at most 256 entries. Interns and addresses are prepared beforehand. No engine callback, allocation, queue operation, resource/weapon read, classification, or output occurs under the complete tuple. Invalid or stale data produces neutral state.

Owner admission uses entry checks and later generation checks, not a reservation covering all owner work. Transitions may begin between checks; scalar capture remains protected by the complete tuple. Private, deferred, or third-party mapping mutations are outside the public-remap interception scope.

### Native limits

The binding assumes valid mapped engine locks and successful ambient engine event operations. It borrows no native event handles. Narrow asynchronous unwind windows in relocated helper/remap prologues do not have a general frame-reconstruction guarantee.

Relied-on instructions, guards, offsets, and pool layout are defined in `src/native/bindings.cpp`, `src/native/dispatch.cpp`, and `src/native/ui_capture.cpp`.

## Actual shots

The adapter uses the existing weapon-fired source:

| RVA | Function |
| --- | --- |
| `0x476000` / `0x476020` | Register / unregister weapon-fired sink |
| `0x4799b0` | Weapon fire |
| `0x481880` | Weapon-fired notify |
| `0xe5d350` | VATS weapon-fired callback |

Fire notifies once after its projectile loop, separately from later impacts. The event prefix has weapon instance at offset `0`, shooter at `8`, and projectile collection at `0x10`; the collection has data at `8` and count at `0x18`.

A positive count alone does not establish a launch. Admission requires a nonnull projectile pointer within the first 64 entries. The adapter emits one cue and neither retains nor dereferences projectile pointers.

Notify is synchronous with live borrowed payloads; callback threading is not established. The callback copies bounded player attribution and identity only. Ammo changes, held input, AP expenditure, and firing animation state are not substitutes for an actual shot.

## Hits and weapon families

Extended hit flags are read only with `usesHitData`; prediction flags are rejected. Only `kExplosion` (`0x100000`) selects explosive damage. `kExplodeLimb` (`0x80`), dismemberment, and crippling select limb damage.

The equipped instance's keyword form replaces base keywords when present, including an empty instance form. Combining sets would restore keywords removed by modifications. Base keywords are fallback only when instance data is absent.

## Health and AP

Normalized ratios use current Actor Values divided by `current - GetModifier(kDamage, info)`, with modifier `kDamage = 2`. Nonfinite values and nonpositive denominators are unavailable; valid ratios are clamped to 0–1.

The health HUD also processes active effects and overlays. The adapter's Actor Value ratio is not a complete model of every displayed health-bar frame.

## LED health, stealth, and radiation

`native/led_bindings.*` owns optional LED address resolution and source access. It uses `bindings.*` only for the checked executable base; weapon events and mandatory host checks remain there. `radiation_source.*` owns the event sink and lifecycle cache, while `observation.cpp` copies source values into the shared policy input.

Before optional section parsing, `checked_image_base()` validates the loader-provided host's DOS signature, nonnegative `e_lfanew` with the complete NT header in the first 4 KiB, NT signature, x64 architecture, optional-header magic, non-zero image size, required static address bounds, and verified 1.11.240 instruction signatures. Section-table and address bounds are then checked against that validated size. `REL::Offset2ID` is the public API in the pinned `commonlib-shared` revision `29fbdb0e2dc548c9ab22f6964981d75090dc9094`.

The LED health sample copies the absolute current health and gameplay maximum on the admitted main observation path. Health is read once; both haptics and enabled LED health use that same sample. Its maximum uses the existing Actor Value relation `current - GetModifier(kDamage, info)` and does not change haptic normalization. A maximum change is a new color basis, not evidence of damage or healing. Native LED reads follow the immutable startup settings; disabled stealth skips detection queries.

Combat comes from the actor's `IsInCombat` virtual helper. Sneaking and detection use optional Address Library bindings for `Actor::IsSneaking`, `ProcessLists::Singleton`, and `ProcessLists::RequestHighestDetectionLevelAgainstActor`. Detection remains the returned signed 16-bit raw score. These new IDs are resolved by one temporary public `REL::Offset2ID` inverse-table pass (`load_v5`, then `load_v2` only if empty); each result must land in a readable code or data section of the already checked 1.11.240 executable. Missing IDs or wrong-section addresses disable only the affected optional source. No fatal `ID::offset` lookup or guessed relocation is used for these additions.

Radiation subscribes to CommonLibF4's `CurrentRadiationSourceCount` change source through the same admitted main-thread sink-install path. Its global event source and source slot are optional data bindings and receive the same image-section validation. The callback runs under the engine source lock, captures a lifecycle generation, copies only the uint32 count, and makes one lock-free publication CAS. It performs no engine reads, logging, waiting, or output work. The sink and scalar cache live for the module/process lifetime because the engine global source can delete itself during shutdown.

The library exposes this change-event source but no supported request source for the current value; registration does not replay existing state. Therefore radiation remains unknown after startup/load until a real count event is received. Load/new-game/inert transitions invalidate the generation; the first admitted gameplay observation enables the new generation. Focus, UI, audio, or controller rebinding does not erase the cached game-lifecycle count. Generation exhaustion disables this optional source without wrapping.

Radiation reflects only whether a known current source count is positive. The event does not provide an ambient intensity or current RAD/s value, and stored rads are not substituted.

## Animation contacts

Only `FootLeft` and `FootRight` are accepted from the current player's selected active graph. Foot contacts carry the fresh power-armor and sprint state; they are accepted both in and outside power armor. The policy selects the corresponding [step profile](effects.md#reload-and-power-armor).

Landings instead copy `currentProcess->middleHigh->charController->context.m_currentState`, player position and player/cell form IDs during the existing observation callback. Only on-ground, jumping and in-air physics states are admitted, and swimming is excluded. No controller or cell pointer is retained. `PowerArmorHardLanding` does not emit an animation cue, preventing duplication with the measured landing.

Graph selection uses the manager's update lock, array at `0x40`, and active index at `0xd8`. The source is at graph offset `0x68`; intrusive references use offset `0x08`. Startup checks the corresponding layout and lifetime instructions.

The adapter acquires a graph reference under the manager lock, then registers outside it. It closes callback admission before unregistering and releases the reference only after successful unregister. Failure retains one inactive graph for retry. Callbacks copy known tags without the manager lock; `holderID` is not used for attribution.

See [effect policy](effects.md) for permitted cues and unsupported signals.
