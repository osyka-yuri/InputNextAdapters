# Offline audio haptics baker

The DLLs include 49 source-derived profiles for Skyrim SE, 18 for Skyrim LE, and 30 for Fallout 4.
The current catalogue has 83 base roles; 79 distinct roles have admitted sound mappings.
The three game banks contain 97 entries because supported Skyrim roles occur in both editions. See the
[source coverage summary](../tools/haptics/source-coverage.md) for mapping limits,
explicit fallbacks, archive requirements and edition-specific inactive events.
An optional `.audiohaptics` file beside the adapter INI overrides matching built-in sections and
can add other known transient or supported Skyrim held sections. Unspecified built-in entries remain active. Missing,
corrupt, wrong-game, or unreadable external banks keep the compiled defaults. Setting
`AudioHapticsEnabled=false` disables both compiled and external audio envelopes, returning all
effects to their synthetic waveforms.

The checked-in [bank resources](../resources/audio-haptics/) are compiled into the DLLs by CMake.
Ordinary C++ builds need no game archives or FFmpeg. Expected bank hashes are listed in the
source coverage summary; rebaking is only needed to change or regenerate profiles.

~~~text
cargo run --release -p inputnext-haptics -- inspect --game-data "D:\Games\Skyrim Special Edition\Data" --game skyrim-se
cargo run --release -p inputnext-haptics -- bake --game-data "D:\Games\Skyrim Special Edition\Data" --game skyrim-se --archive "D:\Games\Skyrim Special Edition\Data\ccQDRSSE001-SurvivalMode.bsa" --manifest tools/haptics/manifests/skyrim-se.json --ffmpeg "C:\Tools\ffmpeg\bin\ffmpeg.exe" --output artifacts/InputNextSkyrim.audiohaptics
~~~

~~~text
cargo run --release -p inputnext-haptics -- bake --game-data "D:\Games\Fallout 4\Data" --game fallout4 --manifest tools/haptics/manifests/fallout4.json --ffmpeg "C:\Tools\ffmpeg\bin\ffmpeg.exe" --output artifacts/InputNextFallout4.audiohaptics
~~~

The tool is a build-time utility. It does not run in the game, install or rewrite game resources, bundle audio, download FFmpeg, or include an FFmpeg binary. Supply a trusted FFmpeg executable separately. Windows FFmpeg processes run without a visible console window.

## Inputs and override order

~~~text
inputnext-haptics inspect --game-data <Data-or-game-root> --game <skyrim-se|skyrim-le|fallout4> [--archive <path>]...
inputnext-haptics bake --game-data <Data-or-game-root> --game <game> --manifest <json> --ffmpeg <executable> --output <bank> [--archive <path>]... [--allow-missing]
~~~

--game-data may be the game's Data directory or its parent when that directory contains Data. The default archive is Skyrim - Sounds.bsa for Skyrim SE/LE and Fallout4 - Sounds.ba2 for Fallout 4. Each repeated --archive is applied after the default archive, in command-line order; later archives override earlier archive resources. Loose audio files under Data override all archives. Matching is case-insensitive on Windows-style game resource paths. inspect writes a deterministic JSON inventory of the visible WAV and XWM resources.

The reader indexes BSA v104/v105 and general BA2 v1/v8 directories using bounded seeks. It does not load the multi-gigabyte archive into memory or extract the archive.

Each archive and the loose audio inventory are limited to 100,000 entries. The merged store permits at most 1,000,000 unique paths and 128 MiB of retained metadata text, including resource keys, duplicated names, origins and stored filesystem paths; this is not a process-memory cap. Replacements credit the previous record's text size and preserve override precedence.

BA2 aggregate filename metadata is capped at 16 MiB before filename strings are allocated; BSA folder-name and filename tables are each capped at 16 MiB. Selected resource reads are capped at 32 MiB uncompressed. BSA v104 zlib and BA2 v1/v8 zlib payloads are bounded and decoded. Compressed BSA v105 entries use LZ4 and are explicitly rejected by this build; raw v105 entries remain readable. Other BSA and BA2 versions, BA2 subtypes, path traversal, truncated structures, and payloads outside bounds fail explicitly.

Output bank and report paths must be outside the selected game's Data directory and cannot overwrite an input archive, the manifest, or the FFmpeg executable. The report is written beside the bank as <bank-path>.json. Both outputs are fully staged before replacement; the bank is validated and the report serialized before publication. Source archives and loose resources are opened read-only.

Each file is replaced atomically, with the report committed first. The pair is not a transaction: if report replacement fails, both previous files remain; if bank replacement fails, the new report may accompany the old bank. The command reports this failure and removes its staged files. A process interruption between replacements can leave the same mismatch. Before installing or reusing the pair, compare the bank's SHA-256 with `bank_sha256` in the report; rerun the bake if they differ. There is no automatic pair rollback or power-loss durability guarantee.

By default a missing source match, decode error, or invalid source aborts the bake and leaves the current output files unchanged. --allow-missing writes a valid subset and records skipped profiles, per-resource failures, and coverage. It still fails if no valid profile remains.

## Manifests and templates

A manifest is versioned JSON:

~~~json
{
  "version": 1,
  "game": "skyrim-se",
  "profiles": [{
    "section": "Body.Melee.Unarmed.Hit",
    "duration_ms": 70,
    "frequency": 78.0,
    "second_frequency": 0.0,
    "mix": 0.0,
    "style": "blunt",
    "sources": ["sound/fx/fx/melee/punchmedium/*.wav"]
  }]
}
~~~

Each section uses the v1 bank grammar [A-Za-z0-9_.]. Bank records must have unique section names in strictly ascending ASCII order; the Rust and C++ readers enforce the same rule. Durations are 1–1000 ms; target carriers are 20–1000 Hz or zero for the unused secondary oscillator. mix is a 0–1 amplitude weight for the secondary envelope. Styles are blunt, whoosh, or single for a broadband one-channel curve, resonant for upper-band primary plus weighted lower-band secondary, and impact / recoil for lower-band primary plus weighted upper-band secondary. The manifest owns carrier values. They are authored controller parameters and do not represent measured source pitch or mechanical actuator resonance.

The canonical manifests cover the admitted transient, held, and quiet profiles listed in [source coverage](../tools/haptics/source-coverage.md). They keep the catalogue carriers and base durations. Mapping a source does not identify the exact variant, target material, or mod load order at runtime. Quiet profiles passed the absolute delivered-tail RMS budgets; default opt-in settings are unchanged.

Patterns match normalized resource paths. A profile may use up to 32 patterns, each at most 256 UTF-8 bytes; indexed resource paths are limited to 1024 bytes. `*` matches characters in one path segment, `**` may cross separators, and `?` matches one byte within a segment. Matches are case-insensitive and sorted by path. Matching uses an iterative bitset NFA with at most five 64-bit state words per path byte and a 64 Mi word-step budget across one bake. Inputs exceeding a pattern, path, or work bound fail with an error instead of being treated as a nonmatch. At most four variants per profile are selected. No broad archive extraction occurs.

## Decode and envelope transfer

Only declared WAV/XWM files are decoded. The tool writes a bounded selected resource into a private temporary directory, invokes the supplied FFmpeg executable directly (no shell), disables stdin, and deletes the temporary directory when the operation ends. The output is mono float PCM at 16 kHz, restricted to the requested cue duration plus the profile onset-search window (120 ms by default, at most 2000 ms; at most 3000 ms total PCM). Each process has a 10 second timeout and a bounded raw-output size. A process-wait error, timeout or limit violation attempts to terminate and reap the child; cleanup errors do not replace the original failure. Empty, silent, nonfinite, unsupported, or malformed results are reported as failures.

The DSP policy is deterministic and is recorded in each report:

1. Detect onset from 5 ms RMS windows with a 2 ms hop, using max(7% of the search-window peak, 0.0005) as the threshold, within `onset_search_ms` (default 120 ms; valid 5–2000 ms).
2. Align each selected source to that onset, then take the authored cue-duration window. A short input is zero-padded. The rest of a long tail is cropped; audio is never squeezed or stretched to fit.
3. Split mono PCM with a one-pole low-pass at 700 Hz. The low-band signal is the filter output; the high band is the residual.
4. Measure band RMS with 8 ms windows at 32 evenly spaced knots. Average up to four lexically sorted variants after onset alignment.
5. Apply the manifest's secondary mix, scale both bands together so the peak paired sum is 0.88, and set the first and final knots to exactly zero. Reject a silent result.

The JSON report records each chosen resource path, archive/loose origin, input size, SHA-256, detected onset, coverage status, DSP choices, and any missing or failed resources. SHA-256 is over the bounded resource bytes read from the archive or loose path. The report also includes a SHA-256 of the emitted bank.

These envelopes reproduce sampled temporal energy as a bounded haptic shape. They are not audio playback, acoustic-frequency analysis, physical controller calibration, proof of a game-selected sample, or a claim of physical sensation. Transients retain authored levels, gain, limiter, routing and expiry. For Skyrim held profiles, the means P and S of all 32 amplitude knots set a stationary carrier blend: secondary mix S/(P+S) and level multiplied by P+S. This retains the legacy continuous phase, noise, modulation, attack/release and finite horizon; it does not replay a transient envelope or stretch it as the held horizon renews. Synthetic fallbacks remain for explicitly unsupported roles.

`FrequencyHz` sets the primary carrier and preserves the secondary/primary ratio while possible.
After user tuning and Skyrim hit modifiers, active carriers are bounded to 20–1000 Hz; the
secondary stays zero when disabled. Clipping at a boundary can change the ratio while retaining
the source envelope. These are adapter authoring/tuning limits, not a measured controller response.

For transient audio profiles, `DurationScale` is applied once to the delivered voice duration.
The source knots stretch across that lifetime. Carrier pitch and the fixed 1.5 ms edge guards
use real time; the renderer does not apply the duration multiplier a second time.

## Local manifests

- [skyrim-se.json](../tools/haptics/manifests/skyrim-se.json): 49 profiles; include the installed `ccQDRSSE001-SurvivalMode.bsa` with `--archive` when baking this full template.
- [skyrim-le.json](../tools/haptics/manifests/skyrim-le.json): 18 profiles for supported LE events; include `Dawnguard.bsa` with `--archive` for Vampire Lord sources. Unsupported magic, full-draw/held bow phases, crossbow shots, shield blocks, footsteps, mounts, knockdown, killmove and landing profiles are omitted.
- [fallout4.json](../tools/haptics/manifests/fallout4.json): 30 profiles; PowerArmorEnter explicitly uses a 2000 ms onset search for its late-starting recordings.

Install a generated `.audiohaptics` file next to the adapter INI only after reviewing its report. The external bank is optional and game-tagged; an absent, invalid, or wrong-game external bank leaves the compiled profiles active. The opt-out setting disables both compiled and external profiles.
