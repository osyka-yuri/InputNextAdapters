# SDK provenance

The Core and Effects headers are vendored at [input_next_core.h](../third_party/inputnext/include/input_next_core.h) and [input_next_effects.h](../third_party/inputnext/include/input_next_effects.h). CMake verifies their paths and bytes against the [manifest](../third_party/inputnext/manifest.json). Source references are informational; builds require no particular Git revision. See [UPSTREAM.md](../third_party/inputnext/UPSTREAM.md) for the current source and the release archive verification procedure used when updating the SDK.

## Contexts and cleanup

An Effects SDK context identifies one exact native owner by its runtime handle and opaque ID. Reconnect or
foreground revocation requires a fresh context. The 32-context public limit includes closed contexts until their
receipts are acknowledged; TTL expiry removes an intent but does not free its context slot.

Discrete submissions require an increasing positive mutation sequence, a supported nonzero channel mask and a TTL
of 1–60,000 ms. Keep accepted channel revisions for a conditional clear with a later sequence.

Close with `close_request_id=context.id`. `REVOKED` returns the retained receipt; retry an uncertain close with the
same key, then pass the complete receipt to `ack_close`. Native revocation, native drainage, and public
acknowledgement have separate completion boundaries.

## Body PCM and capabilities

Body PCM uses finite normalized 48 kHz mono or stereo float samples, copied by the runtime before the call returns.
Limits are 64 MiB of total sample storage and 64 distinct buffers, including retained owners; there is no separate
duration cap. Acquire a body lease with `lease_id=0` and `sequence=1`; updates use the returned lease/audio identity
and a greater sequence. A new audio incarnation requires a fresh lease, and `clear_body` revokes the exact lease.

Capabilities combine available hardware facts with the exact registered backend. `HD_READY` and a nonzero audio
instance identify qualified observed readiness. Sony Bluetooth remains input-only.

Use [RuntimeClient](runtime-client.md) for direct calls or [effect delivery](effects.md) for shared sessions. Keep game-specific policy and calibration in the adapter.
