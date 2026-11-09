# Game adapters

Each plugin translates game actions and player state into InputNext effects. Choose a guide for supported versions, installation and settings.

| Game | Architecture | Guide |
| --- | --- | --- |
| Skyrim Special Edition / Anniversary Edition | x64 | [InputNextSkyrim](skyrim_se/README.md) |
| Skyrim Classic / Legendary Edition 1.9.32.0 | x86 | [InputNextSkyrimLE](skyrim_le/README.md) |
| Fallout 4 1.11.240.0 | x64 | [InputNextFallout4](fallout4/README.md) |

Install [InputNext](../docs/runtime-setup.md) before the plugin. VR is unsupported. DLC is optional; absent content disables its related effects. Classic/LE has no Creation Club Survival integration.

## Developing an adapter

A game adapter owns its SDK, callbacks, observations, settings, and effect policy. The shared library supplies the [runtime connection](../docs/runtime-client.md) and [effect delivery](../docs/effects.md).

Keep game pointers inside the native boundary. Copy events into bounded value data, select the controller slot explicitly, and submit finite effects for the current target. Handle callback lifetime using the game's SDK.

Adapters are independent CMake projects. The root build does not build plugins or fetch game SDKs. The Skyrim frontends share policy and configuration in [the Skyrim core](skyrim/README.md); each retains its own native ABI.
