# Changelog

## [0.1.2]

- Remove unnecessary executable checks to improve compatibility with modified Fallout 4 installations.

## [0.1.1]

- Improve compatibility with modified Fallout 4 1.11.240.0 executables by replacing strict PE timestamp and image size checks with instruction signature and address bounds validation.
- Centralize executable validation and static address bounds checking across native bindings, animation events, dispatch hooks, and UI capture.
- Improve diagnostic logging for executable compatibility failures and F4SE trampoline allocation errors.

## [0.1.0]

- Native integration for Fallout 4 1.11.240 with F4SE 0.7.9.
- Adaptive triggers and body feedback for weapon actions, hits, and VATS.
- Native gameplay observation, UI capture, and configurable USB effects.
