# Shared Skyrim core

Effect policy, waveform generation, configuration, and session handling shared by the SE/AE and Classic/LE plugins.

Native adapters supply value observations. The core keeps no game pointers and resolves no game addresses. Optional content is available only when a frontend supplies its signals; Creation Club Survival is supplied by SE only.

## Build and test

From this directory, use an x86 or x64 Visual Studio Developer PowerShell with CMake 4.4.4 or newer and Ninja:

```powershell
chcp 65001
$env:VSLANG = '1033'
cmake -S . -B ../../build/skyrim-core -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ../../build/skyrim-core
ctest --test-dir ../../build/skyrim-core --output-on-failure
```

This build needs no game SDK.

## Integration

The targets are `skyrim_policy`, `skyrim_session`, and `skyrim_config`. Native frontends include the project with `add_subdirectory` and select tests with `INPUTNEXT_SKYRIM_BUILD_TESTS`.

`Policy` composes `BodyCueHistory` (finite cues and held admission fences), `EffectMixer` (trigger and rumble envelopes), `CriticalHealthHeartbeat` (heartbeat timing), and the shared `LedPolicy` (lightbar selection and timing). These components use value observations and explicit time, with no runtime I/O. Native frontends supply LED capabilities; [shared delivery](../../docs/effects.md#lightbar-policy-and-delivery) owns SDK submission.

`reset_body_history()` clears body cues, rumble, and heartbeat history while preserving trigger overlays and LED state. `reset()` also clears trigger overlays, LED state and event deduplication. Both preserve held admission fences. The coordinator owns session admission and output delivery.

`Coordinator` uses separate ingress and delivery locks. Ingress protects tickets, event publication, controls and diagnostics. The serialized tick path owns the output session under delivery, so runtime calls never hold ingress.

Each delivery uses one frozen observation and bounded action batch. Callbacks arriving during output remain queued for the next tick; hard lifecycle changes and overflow prevent further stale submissions.

Game signals and plugin installation are documented by the [SE/AE adapter](../skyrim_se/README.md) and [Classic/LE adapter](../skyrim_le/README.md).
