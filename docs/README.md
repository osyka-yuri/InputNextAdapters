# InputNextAdapters documentation

## Game plugins

Choose a game guide for requirements, installation and settings. All plugins use the same [InputNext runtime setup](runtime-setup.md).

| Game | Installation and settings | Reference |
| --- | --- | --- |
| Skyrim SE/AE | [InputNextSkyrim](../adapters/skyrim_se/README.md) | [Effects](../adapters/skyrim_se/docs/effects.md), [architecture](../adapters/skyrim_se/docs/architecture.md), [build and testing](../adapters/skyrim_se/docs/validation.md) |
| Skyrim Classic/LE | [InputNextSkyrimLE](../adapters/skyrim_le/README.md) | [Native bindings](../adapters/skyrim_le/docs/bindings.md), [build and testing](../adapters/skyrim_le/docs/validation.md) |
| Fallout 4 | [InputNextFallout4](../adapters/fallout4/README.md) | [Effects](../adapters/fallout4/docs/effects.md), [architecture](../adapters/fallout4/docs/architecture.md), [native bindings](../adapters/fallout4/docs/bindings.md), [build and testing](../adapters/fallout4/docs/validation.md) |

See [lightbar feedback](leds.md) for shared colors, priorities and `[LED]` settings.

## Developer guide

| Task | Guide |
| --- | --- |
| Build the library or use its CMake package | [Build and integration](building.md) |
| Create or consume product packages | [Product packages](packaging.md) |
| Connect to InputNext and call its ABI | [Runtime client](runtime-client.md) |
| Deliver triggers, lightbar and finite body effects | [Effect delivery](effects.md) |
| Prepare audio-derived profiles | [Offline audio haptics baker](audio-haptics-baking.md) |
| Check source mappings and fallbacks | [Audio haptics source coverage](../tools/haptics/source-coverage.md) |
| Understand haptic profile design | [Haptic design rationale](haptics-rationale.md) |
| Check or update the pinned SDK | [SDK provenance](sdk.md) |
| Run formatting and source analysis | [Contributing](../CONTRIBUTING.md) |
| Understand CI and product releases | [CI and release verification](ci.md) |
| Work on the shared Skyrim policy | [Shared Skyrim core](../adapters/skyrim/README.md) |

Commands run from the repository root unless a guide specifies an adapter directory.

## Source layout

| Directory | Contents |
| --- | --- |
| `include/inputnext_adapters/` | Public C++ interfaces |
| `src/` | Runtime client and shared effect delivery |
| `third_party/inputnext/` | Pinned C ABI headers and source information |
| `adapters/` | Game policies and native plugins |
| `resources/audio-haptics/` | Embedded haptic profile banks |
| `tests/` | Library and package tests |
| `examples/effects_probe/` | Standalone inspection client |
| `tools/ci/` | Rust build, package and release tooling |
| `tools/haptics/` | Offline bank baking and source manifests |
