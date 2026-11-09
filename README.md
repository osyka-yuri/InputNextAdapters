# InputNextAdapters

InputNextAdapters adds adaptive triggers, vibration and lightbar feedback to Skyrim and Fallout 4 through [InputNext](https://github.com/osyka-yuri/InputNext). Weapon actions, hits and player state drive the feedback, with USB HD haptics and classic rumble fallback. [Lightbar indications](docs/leds.md) show temporary health changes and supported stealth and radiation states.

## Game adapters

| Game | Installation and settings | Architecture |
| --- | --- | --- |
| Skyrim Special Edition / Anniversary Edition | [InputNextSkyrim](adapters/skyrim_se/README.md) | x64 |
| Skyrim Classic / Legendary Edition | [InputNextSkyrimLE](adapters/skyrim_le/README.md) | x86 |
| Fallout 4 | [InputNextFallout4](adapters/fallout4/README.md) | x64 |

Start with the guide for your game. It lists supported game versions, dependencies and installation steps. Skyrim LE provides body and health lightbar feedback; adaptive-trigger effects are unavailable. VR is unsupported.

DualSense and DualSense Edge require USB for feedback. HD haptics also require the controller's audio device to be available. Bluetooth currently supports input only. Each plugin provides controller-slot and strength settings.

## For developers

The C++23 library provides a runtime client and shared effect delivery as CMake targets. The root build requires no game SDK; native plugins build separately.

[Documentation](docs/README.md) · [Build and integration](docs/building.md) · [Contributing](CONTRIBUTING.md)

## License

Authored code and the InputNext SDK are [MIT licensed](LICENSE). Native plugins include dependencies with separate terms; see each adapter's notices and the root [third-party notices](THIRD_PARTY_NOTICES).
