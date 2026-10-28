# Shared v1 bank fixtures

Each 568-byte fixture is an `INHB` v1 bank. Its 24-byte header stores the magic, then little-endian `u32` version, game tag, record count, payload size and CRC-32 of the payload. Each record stores a little-endian `u16` section-name byte length, its ASCII name, two little-endian `f32` carriers and 32 primary plus 32 secondary little-endian `f32` knots. Values use the format bounds and zero endpoints required by the runtime and baker.

The fixtures use the Skyrim SE game tag. `sorted.audiohaptics` contains `Body.A` (78 Hz) followed by `Body.B` (188 Hz) and is valid. `descending.audiohaptics` reverses those records while retaining a valid payload CRC. `duplicate.audiohaptics` repeats `Body.A` with a different carrier. Both invalid samples isolate strict ordering and uniqueness.

Rust baker tests and C++ runtime parser tests read these same files; they are hand-authored contract fixtures, not shipped bank inputs.
