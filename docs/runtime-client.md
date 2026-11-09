# Runtime client

`inputnext::adapters::RuntimeClient` owns one DLL reference and a copied Effects ABI 1 table. It is move-only and initially disconnected. Use a runtime matching the calling process: x86 or x64.

## Loading

Call `connect()` outside `DllMain` with an absolute path to a trusted `inputnext_runtime.dll`. Relative paths and embedded NULs are rejected. The loader uses `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`; it does not modify the process search path or search for another runtime.

The client resolves `input_next_runtime_query_effects_api` and validates the exact size, version and all ten callbacks. A successful connection retains the module until disconnect, destruction or move assignment. Failed binding releases its acquired reference. The runtime may retain its own process-lifetime pin.

`connected()` reports a bound table, not controller availability. `resolve_target()` obtains an exact target and current capabilities. Native startup is foreground-gated; Sony Bluetooth currently supports input only.

## Calls and outputs

| Method | Contract |
| --- | --- |
| `resolve_target()` | Resolve a Core selector and return target/capabilities on `OK` |
| `open_context()` | Return a new context on `OK` |
| `query_context()` | Return context information on `OK` or `REVOKED` |
| `submit()` / `submit_body()` | Forward the request and return its receipt on `OK` |
| `clear()` / `clear_body()` | Forward the caller's exact conditional clear |
| `close_context()` | Return the retained close receipt on `REVOKED` |
| `ack_close()` | Acknowledge the exact close receipt |
| `query_progress()` | Return correlated progress on `OK` |

Other statuses preserve output arguments. Disconnected calls return `INPUT_NEXT_STATUS_UNAVAILABLE`. The client initializes output structure sizes, including nested context information; callers initialize request sizes, identities and reserved fields. `make_request()` fills the context, sequence and mask, saturates TTL to the unsigned 32-bit millisecond range, and leaves effect values zero. The runtime validates the request, including the supported TTL range.

Opening is synchronous: only `OK` creates a caller-owned context. A non-`OK` result, including `PENDING`,
does not reserve a context that the caller must close.

Receipts distinguish logical acceptance from native delivery. See [SDK semantics](sdk.md) and [shared effect delivery](effects.md) for leases, PCM and progress.

## Lifetime and threading

Serialize connect, disconnect, moves and destruction against calls. Concurrent const calls on a stable client follow the runtime's table contract; the client adds no lock. Moving leaves the source disconnected and reusable. Disconnect is idempotent.

The caller owns context cleanup. Close each context and retain its receipt until `ack_close()` completes before releasing the client. TTL expiration removes intent but does not release context capacity. Disconnect does not clear effects or acknowledge contexts.

Detach and drain game callbacks before client teardown, outside loader lock. The client supplies no callback barrier or worker thread. The [effects probe](building.md#use-from-source) can inspect a matching runtime without submitting effects.

## Production package check

Set `INPUTNEXTADAPTERS_RUNTIME_DIRECTORY` to the extracted, architecture-matched package's `bin/` during configure. CTest registers `runtime_client.production_package` and `output_session.package.*`. Their executables build with root tests; registration requires the external package. No sibling InputNext checkout is needed.

The ABI probe checks tables, invalid calls, moves and disconnect. Session cases pass generated requests to the real runtime using controlled discovery and receipts. Valid formats return `UNAVAILABLE` from the cold runtime; malformed controls return `INVALID_ARGUMENT` before availability is checked. These checks start no native input and produce no effects. They cover structural validation, not live-context acceptance or playback.

Before releasing an adapter/runtime pair, pass these checks on x64 and x86 against the exact runtime packages being distributed. Record their SHA-256 hashes with the results. Run from the matching Developer PowerShell; replace `x64` with `x86` for that architecture:

```powershell
cmake --preset msvc-x64 -DINPUTNEXTADAPTERS_RUNTIME_DIRECTORY="C:/packages/inputnext-x64/bin"
cmake --build --preset msvc-x64
ctest --preset msvc-x64 -R '^(runtime_client\.production_package|output_session\.package\.)'
```
