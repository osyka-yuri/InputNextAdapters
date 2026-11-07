# Classic native bindings

The native layer targets Skyrim 1.9.32.0 and the official [SKSE 1.7.3 source](https://skse.silverlock.org/). Its typed SDK declarations and layout assertions are compiled for x86. SE/CommonLib offsets do not enter this backend.

| Signal | LE source and supported interpretation |
| --- | --- |
| Action | `SKSEActionEvent` type, slot and scalar actor/source form IDs |
| Hit | `TESHitEvent` target, caster, source, projectile and flags; outgoing contact requires a confirmed melee source |
| Menus | `MenuOpenCloseEvent`, typed `MenuManager` helpers and adapter-local LE pause (`0x01`) and cursor (`0x04`) flags; unreliable tracking suppresses gameplay |
| Actor | Player singleton, typed actor-value/state accessors, life/combat state and equipped objects |
| Movement | Declared `ActorState` gait and swimming flags, stamina and basic cell/worldspace location |
| Beast forms | Loaded-plugin table and exact race form identity: Skyrim local `0xCDD84`, Dawnguard local `0x283A` |
| Load lifecycle | Official messaging interface; `PostLoadGame` encodes its one-byte result in the pointer value, which is never dereferenced |
| Scheduling | Classic `TaskDelegate::Run/Dispose`; the scheduler owns accepted delegates and disposal deletes their concrete type |

The SKSE 1.7.3 header mislabels `0x02` as the cursor flag. The classic [LE `IMenu` declaration](https://github.com/himika/libSkyrim/blob/2559175f7f30189b7d3681d01b3e055505c3e0d7/Skyrim/include/Skyrim/Menus/IMenu.h) identifies `0x02` as persistence on close and `0x04` as cursor display. The adapter uses those verified LE values with the SDK's x86 layout. Persistence alone, including the normal HUD flags, leaves gameplay effects enabled.

Callbacks copy bounded value data. Skyrim forms and observations are resolved in queued main-thread tasks, and no game pointer is retained in the shared coordinator. One worker coalesces requests; at most one task is queued. Both Skyrim frontends use the [shared core's](../../skyrim/README.md) freshness, epoch, serialization and finite-lease rules.

Plugin Query rejects incompatible hosts before initialization. Load validates interfaces and paths, then pins the module before publishing callbacks. Registered callbacks and their owner remain alive for the process, including inert partial-start failures.

## Native limits

The frontend does not supply bow phases, ammunition or actual-shot payloads, caster phases, spell taxonomy, dual casting, blocking, stagger/knockdown/killmove, mount or airborne state, or footstep events. It has no Survival integration. Effects requiring these signals remain inactive; entering or leaving water also produces no feedback.

`BowDraw` and `BowRelease` action events produce short cues. Release does not confirm that an arrow was fired. Spell effects remain inactive because spell type and cast mode are unknown.

The current frontend cannot activate adaptive-trigger resistance or vibration. Its trigger-producing recipes require bow phases, blocking or typed caster state. `BowRelease` can submit a trigger-off command.

The [reference INI](../InputNextSkyrimLE.reference.ini) lists supported settings. The parser still accepts older body and trigger options, including `TriggerScale` and `TriggersEnabled`; they do not enable unavailable effects.

Optional content uses the actual loaded plugin index, bounded plugin name and expected record type/ID. It does not use the SDK's list-position `GetModIndex` or placeholder `ModInfo::IsLoaded`. The backend performs no Survival plugin/global/quest lookup.
