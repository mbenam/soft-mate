# m8_makeprobe

**Source:** [`src/tools/main_makeprobe.cpp`](../../src/tools/main_makeprobe.cpp) (565 lines)
**Build target:** `m8_makeprobe` (CMakeLists.txt)
**Category:** hardware-test fixture generator (writes real `.m8s` files, byte-exact, for loading
onto a real M8 or into `m8_render`)
**Links:** `m8_files_cpp` only (the vendored `m8-files-cxx` library's `Song`/`BinaryWriter`/
`BinaryReader` types). **No SDL, no engine** — it writes the M8 file format directly, it does not
go through `io::SongIO` or the app's `Engine` at all.

## What it does

Generates a **minimal, deterministic `.m8s` probe file**: exactly one configured instrument (at
index 0), one phrase (one sustained note at row 0), one chain (pointing at that phrase), and a
song that repeats that one chain across all 16 rows of track 0 — everything else in the file is
left at empty/default. The point is a fixture with a single, known, isolatable sound: load it on
real hardware or render it offline, and whatever you hear/measure is unambiguously that one
instrument's output.

This is the fixture generator behind the render→capture→analyze/spectrum hardware-parity loop
(see [`M8_HARDWARE_TEST_SPEC.md`](../../M8_HARDWARE_TEST_SPEC.md)): render the probe offline with
[`m8_render`](m8_render.md) as the pitch/health oracle, load the same probe on real hardware and
capture it with [`m8_capture`](m8_capture.md), then compare the two with
[`m8_analyze`](m8_analyze.md)/[`m8_spectrum`](m8_spectrum.md).

## Supported instrument types (`--type`)

| Type | Notes |
|---|---|
| `macrosynth` (default) | The only type this tool's own `verifyRoundTrip` actually round-trip-checks in detail (see Gotchas). |
| `sampler` | Requires `--sample-path`. The one type currently at genuine hardware parity per `M8_HARDWARE_TEST_SPEC.md` — a real timbre gate, not just a pitch/health check. |
| `wavsynth` | Defaults to a 12%-duty pulse-ish shape at fixed size/mult; not independently round-trip-verified (see Gotchas). |
| `fmsynth` | All 4 operators forced to `level=0x80, ratio=1, Sin` regardless of flags — deliberately audible-by-construction (an all-zero-level probe would be silent under every algorithm, since different algorithms use different operators as carriers). Not independently round-trip-verified. |
| `hypersynth` | `default_chord[0]` forced to MIDI 60 (C-4) regardless of the actual note played — an empty chord means zero active notes on this synth (`SynthVoice.cpp` skips any chord slot `<= 0`), so a probe without this would be silent by construction. Not independently round-trip-verified. |

## CLI flags

| Flag | Default | Meaning |
|---|---|---|
| `--type <name>` | `macrosynth` | Instrument type (see table above). |
| `--note <name>` | `C-4` | Note for phrase row 0. Format: `C4`, `C#4`, `Db4`, `C-4` (dash optional/ignored). MIDI = `60 + (octave-4)*12 + semitone`. |
| `--out <path>` | `probe.m8s` | Output file (single-file mode). |
| `--out-dir <dir>` | — | Required for `--sweep`; output directory for the batch of swept files. |
| `--sweep <param>` | — | Sweep mode (see below); `<param>` is `shape`, `timbre`, or `color`. |
| `--sample-path <path>` | — | **Required for `--type sampler`.** M8-absolute path (e.g. `/probes/probe_sine.wav`) — must exist at this path on the SD card for real-hardware loading, and under `--sample-root` for `m8_render`'s local oracle. |
| `--shape <n>` | `0` | MacroSynth shape / WavSynth shape enum value. Accepts `0x`-prefixed hex. |
| `--timbre <n>` | `0x40` | MacroSynth timbre. |
| `--color <n>` | `0x80` | MacroSynth color. |
| `--volume <n>` | `0xE0` | `synth_params.volume` (the mixer/channel-strip level, distinct from the on-screen "AMP" instrument-type field — see Gotchas). |
| `--filter-type <n>` | `0` | `synth_params.filter_type`. |
| `--filter-cutoff <n>` | `0xFF` | `synth_params.filter_cutoff`. |
| `--filter-res <n>` | `0` | `synth_params.filter_res`. |
| `--tempo <bpm>` | `120.0` | Song tempo (float). |
| `--table-tick <n>` | `0xFF` (disabled) | Assigns table 0 to the phrase step via an FX command (library FX byte `0x06`, engine `FxCmd::TBL`) at this tick rate. `0xFF` means "don't assign a table" — setting `table_tick` on the instrument alone does **not** enable table execution; `Engine::tickTable()` is a no-op until a table is actually assigned via this FX command. |
| `--slice <n>` | `0` (off) | **Sampler-only.** `0`=off, `1`=FILE (WAV-embedded slice markers), `2`-`0x80`=N equal divisions. Added for the SLICE note-base hardware investigation (`sampler-slice-repitch-hw` memory). |

Any unrecognized flag prints `unknown arg: <flag>` and exits 1.

## Sweep mode (`--sweep shape|timbre|color`)

Writes one file per value of the named parameter, stepping `0x00` to `0xF0` in increments of
`0x10` (17 files), named `probe_<param>_<XX>.m8s` in `--out-dir`. **Sweep mode only varies
shape/timbre/color** — it doesn't generalize to other parameters or other instrument types beyond
whatever `--type` was also passed (though only MacroSynth/WavSynth actually use shape as a
meaningful axis). Each written file is round-trip-verified the same way single-file mode is;
the whole sweep aborts (exit 1) on the first verification failure.

## What's baked into every probe (regardless of type)

- **`song.midi_settings` is explicitly zeroed.** The struct has no default initializers and *is*
  serialized — leaving it default would write 25 bytes of uninitialized memory into the MIDI
  block, and garbage there can route tracks to MIDI I/O instead of internal audio, producing a
  **silent** probe. (This was a real bug, found and fixed — see `status.md`.)
- **Song track 0 repeats chain 0 across all 16 rows**, not just row 0 — a single-row song would
  leave the tail of a fixed-length capture window silent (failing `m8_analyze`'s
  `longest_silence` gate). The AHD envelope's long hold (below) means each retrigger lands before
  the previous note decays, so it reads as one continuous tone.
- **A volume-modulation envelope is attached to mod slot 0**: `AHDEnv{dest=1 (VOLUME), amount=0xFF,
  attack=0x01, hold=0x80, decay=0x80}` — at 120 BPM (1 tick = 1000 samples) this holds full volume
  for ~2.67s then decays over ~2.67s (~5s total), comfortably covering the ≥1.5s analysis window
  `M8_HARDWARE_TEST_SPEC.md` §4 requires. **This envelope was hardware-tested 2026-07-18 and found
  to have zero measurable effect on output volume** when the same configuration was replicated on
  a natively-authored instrument — so it is *not* what's causing the near-silent-probe bug
  described below; whatever is causing that is still unknown.
- **Table 0, row 0 is pre-populated** with `transpose=+12, velocity=0xFF` (inert unless
  `--table-tick` also assigns it via the FX command) — harmless, always present, "unused" by
  default like every other field this generator initializes fully rather than leaving default.

## Round-trip verification

After writing, `verifyRoundTrip()` reads the file back and checks: instrument type + type-specific
parameters, that phrase 0 row 0 has instrument `0x00` and a non-empty note, that chain 0 row 0
points at phrase 0, and that song row 0 track 0 points at chain 0. Prints `FAIL: <reason>` and
returns false (which the caller turns into a hard failure, exit 1) on any mismatch.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success (file written and round-trip verified) |
| 1 | Unknown CLI arg; `--type sampler` without `--sample-path`; `--sweep` without `--out-dir`; unknown `--sweep` param; write failure; or round-trip verification failure |

Parse errors in `parseNote` (`throw std::runtime_error`) are **not caught** — an invalid `--note`
value crashes the process (uncaught exception) rather than printing a clean error and exiting 1.

## Examples

```
# One MacroSynth probe at a specific shape/timbre/color
m8_makeprobe --type macrosynth --shape 0x00 --timbre 0x40 --color 0x80 --note C-4 --out probe_macro_00_40_80.m8s

# A full shape sweep, 17 files
m8_makeprobe --sweep shape --type macrosynth --note C-4 --out-dir probes/

# A sampler probe pointing at a WAV already on the SD card
m8_makeprobe --type sampler --sample-path /probes/probe_sine.wav --out probe_sampler.m8s

# A sampler probe with 4 equal slices (for SLICE note-base testing)
m8_makeprobe --type sampler --sample-path /probes/probe_sine.wav --slice 4 --out probe_slice4.m8s

# Assign table 0 to run every tick
m8_makeprobe --type macrosynth --table-tick 1 --out probe_table.m8s
```

## Gotchas — read before trusting this tool's own "PASS"

- **`--type wavsynth|fmsynth|hypersynth` always "fails" and exits 1 — verified by actually
  running it** (`m8_makeprobe --type fmsynth --out x.m8s` → `FAIL: instrument 0 is not
  MacroSynth` / `round-trip FAILED` / exit 1). `verifyRoundTrip`'s `else` branch (used for every
  type that isn't literally `"sampler"`) hardcodes a MacroSynth check — a real, pre-existing,
  unfixed bug in the tool's own self-check, not in the generated file. **The file is written to
  disk correctly before this check runs and is NOT deleted on failure** — confirmed: the exit-1
  run above still produced a correctly-sized 163840-byte `.m8s` file. So the practical impact is
  narrow but real: the probe file itself is fine, but any script or CI step that gates on this
  tool's exit code will treat every wavsynth/fmsynth/hypersynth probe generation as a failure,
  and the printed `wrote <path> (type=... note=...)` success line never prints for these types
  either (it's after the `if (!verifyRoundTrip(...)) return 1;` check). If you're scripting
  against this tool for those three types, don't gate on the exit code — check that the output
  file exists and has nonzero size instead, or fix `verifyRoundTrip` to be type-aware (mirroring
  its existing `sampler` branch) before relying on it.
- **`--volume` is `synth_params.volume`, not the on-screen "AMP" field you'd naively expect.**
  Both exist and are related but distinct — see `src/io/SongIO.cpp`'s `s.amp = sp.volume;`
  mapping if you need the exact relationship. Don't assume setting `--volume` low is equivalent
  to setting AMP low on the device screen.
- **Known, unresolved, real bug (2026-07-18 hardware finding): Sampler probes generated by this
  tool play back roughly 125× quieter on real hardware than an identically-configured,
  natively-authored (on-device) instrument** (capture peak 82/32768 vs. 10302/32768 for nominally
  the same AMP/sample/settings). The AHD→VOLUME mod envelope was ruled out as the cause
  (replicating it on the native instrument had zero effect). Root cause not found. If you're
  debugging audio-level issues with a `--type sampler` probe, know this going in — it may not be
  your code.
- **`--table-tick` alone does not make a table execute** — you must pass it (any value other than
  the default `0xFF`) for the FX command that actually assigns table 0 to get written; setting
  `table_tick` on the instrument struct without that FX command is inert.
- **The `midi_settings` zero-fill and the AHD-envelope defaults are silent, load-bearing
  assumptions** baked into every generated probe — if you're hand-editing a probe's bytes or
  extending this generator to a new field, be aware that "leave it default" has already bitten
  this exact tool once (the MIDI-routing silent-probe bug).
