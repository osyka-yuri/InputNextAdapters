# Game adapters

Each plugin translates game actions and player state into InputNext effects. Choose a guide for supported versions, installation and settings.

| Game | Architecture | Guide | Releases |
| --- | --- | --- | --- |
| Skyrim Special Edition / Anniversary Edition | x64 | [InputNextSkyrim](skyrim_se/README.md) | [Skyrim SE](https://github.com/osyka-yuri/InputNextAdapters/releases?q=skyrim-se) |
| Skyrim Classic / Legendary Edition 1.9.32.0 | x86 | [InputNextSkyrimLE](skyrim_le/README.md) | [Skyrim LE](https://github.com/osyka-yuri/InputNextAdapters/releases?q=skyrim-le) |
| Oblivion Classic 1.2.0.416 | x86 | [InputNextOblivion](oblivion/README.md) | [Local build](oblivion/README.md#build-and-package) |
| Fallout New Vegas 1.4.0.525 | x86 | [InputNextFalloutNewVegas](fallout_new_vegas/README.md) | [Local build](fallout_new_vegas/README.md#build-and-package) |
| Fallout 3 1.7.0.3 | x86 | [FOSE load check](fallout3/README.md) | Test build |
| Fallout 4 1.11.240.0 | x64 | [InputNextFallout4](fallout4/README.md) | [Fallout 4](https://github.com/osyka-yuri/InputNextAdapters/releases?q=fallout4) |

Install the [InputNext runtime](../docs/runtime-setup.md) for your game before adding the plugin. DLC-specific effects require the corresponding content. Skyrim Classic/LE supports body and health feedback. VR is unsupported.

## Developing an adapter

A game adapter owns its SDK, callbacks, observations, settings, and effect policy. The shared library supplies the [runtime connection](../docs/runtime-client.md) and [effect delivery](../docs/effects.md).

Keep game pointers inside the native boundary. Copy events into bounded value data, select the controller slot explicitly, and submit finite effects for the current target. Handle callback lifetime using the game's SDK.

Configure each adapter's CMake project to build its plugin with the game SDK. The Skyrim frontends share policy and configuration and retain separate native ABIs. [Skyrim core](skyrim/README.md)

The [Fallout Gamebryo layer](fallout_gamebryo/) contains the shared effect policy and output session for Fallout 3 and New Vegas. New Vegas uses this layer for gameplay feedback. Fallout 3 currently provides a FOSE load check.
