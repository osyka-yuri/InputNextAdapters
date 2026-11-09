# CI and release verification

The Rust CLI `inputnext-adapters-ci` selects build profiles, runs checks, validates packages and checks runtime compatibility. CMake owns native targets and installation. PowerShell prepares the verified Windows compiler environment.

## Checks

Pull requests select affected products. Foundation and shared infrastructure changes cover their dependent adapters; shared Skyrim changes cover SE and LE. Unknown paths select the full supported matrix. Pushes to `main`, manual baseline runs and the weekly schedule use the expanded matrix. Branch pushes do not duplicate PR runs.

Host checks run on Windows and Ubuntu 26.04. Native checks run on Windows with architecture-matched MSVC or clang-cl environments. Ordinary PR checks need no InputNext package or sibling checkout. Unsupported sanitizer/native combinations are rejected by the planner.

Tool archives and actions are pinned. Cached archives are rehashed before extraction; executable installations and the CLI are rebuilt in job-local directories. Native command failures retain logs, JUnit and available SARIF. Native test runs fail if required test identities are missing or skipped.

The `plan` command prints build paths and required tests without building:

```powershell
cargo run --locked -p inputnext-adapters-ci -- plan `
  --root . --output build/ci --product skyrim-le --architecture x86 --check native-tests
```

Run native checks from an architecture-matched Developer PowerShell with the prepared `--toolset` report. Use `--help` on `toolchain`, `native`, `package` and `qualify` for input and report paths. `--jobs` overrides build concurrency; the default uses available processors. See [Contributing](../CONTRIBUTING.md) for local verification scripts.

Use a short absolute `--output` path for native Windows builds. MSVC dependency files can exceed its path limit beneath deeply nested workspaces; hosted jobs use temporary build roots.

## Independent releases

Each product keeps its own CMake version and changelog:

| Product | Tag | Changelog |
| --- | --- | --- |
| Shared libraries | `foundation/vX.Y.Z` | [Root](../CHANGELOG.md) |
| Skyrim SE/AE | `skyrim-se/vX.Y.Z` | [Skyrim SE](../adapters/skyrim_se/CHANGELOG.md) |
| Skyrim Classic/LE | `skyrim-le/vX.Y.Z` | [Skyrim LE](../adapters/skyrim_le/CHANGELOG.md) |
| Fallout 4 | `fallout4/vX.Y.Z` | [Fallout 4](../adapters/fallout4/CHANGELOG.md) |

The Release workflow is manually dispatched from `main` with an existing stable product tag. It checks tag ancestry, the selected product's version and curated changelog section. Checks and packages use the qualified tag source, while final release verification runs from the defining main workflow source.

Supply a published InputNext runtime-pair manifest as JSON, or configure the [repository catalog](../.github/ci/runtime-pair.json). The catalog is currently unset, so a release needs an explicit manifest. Compatibility checks use archive identity, SHA-256, architecture and Core/Effects header bytes. A local development package can be tested but cannot replace an identified published release; no particular runtime source commit is required.

Each release checks the real runtime client on x64 and x86 and the selected adapter's package cases. Foundation or complete pairing qualification covers all adapters. These cold-runtime tests establish ABI and request validation; live gameplay and physical feedback require separate checks.

Only the final draft job receives release write permission. It independently checks source and runtime identities, successful platform jobs, current-run artifact IDs and digests, and the exact package inventory. Job and artifact inventories are read across all pages before selecting the current attempt; inconsistent counts, duplicate IDs and failed pages reject admission.

The draft job does not execute downloaded producer tools, DLLs or source scripts. An existing release fails qualification; assets are never overwritten or published automatically. If an upload fails after draft creation, the reported partial draft requires explicit recovery.

Plugin packages contain their own DLL, settings and notices. Install InputNext and game dependencies separately. Binary, symbol and corresponding-source archives have independent manifests and outer SHA-256 hashes. See [product packages](packaging.md) for their contents.
