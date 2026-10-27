# InputNext SDK provenance

The Core and Effects C ABI headers are vendored byte-for-byte from InputNext commit
[`99838c96c015ad74168cf7998ea9f6006210fd6d`](https://github.com/osyka-yuri/InputNext/commit/99838c96c015ad74168cf7998ea9f6006210fd6d):

- `sdk/input_next_core.h` is stored at `include/input_next_core.h`.
- `sdk/input_next_effects.h` is stored at `include/input_next_effects.h`.

[`manifest.json`](manifest.json) records both source and vendored paths and SHA-256 hashes. CMake checks the
required paths and local header bytes on every configure. The optional revision is a source reference; it is not
checked during builds. No sibling checkout, Git history or source archive is required.

When an SDK release is available, verify its archive against the publisher's SHA-256 before extracting the
headers. Record the release version and archive hash here. For an unreleased SDK, record the source revision
as a development reference. Copy both headers without formatting or line-ending conversion, update their
manifest hashes, and run the SDK regression and layout tests. Review ABI changes and qualify the client against
the matching runtime packages on x86 and x64.
